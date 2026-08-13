// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The handshake that replaces logging in.
//
// Nobody types anything and nothing is registered anywhere. The client proves it holds a key, the
// server works out which account that is, and play begins. The whole exchange is two round trips
// and no I/O: keys were loaded at startup, so this costs two signatures and a key agreement.
//
// THE ORDER IS THE SECURITY. The server signs first, before the client has revealed which account
// it is or produced any proof. A server that merely copied a real server's public key cannot
// produce that signature, so the client walks away before it can be used as a relay.

#include "features/identity/zx_identitynet.h"

#include "cl_demo.h"
#include "cl_main.h"
#include "d_netinf.h"
#include "doomtype.h"
#include "network.h"
#include "network/netcommand.h"
#include "network_enums.h"
#include "sv_commands.h"
#include "sv_main.h"
#include "p_local.h"
#include "v_text.h"

#include "features/identity/computation/identity_compute.h"
#include "features/identity/zx_identity.h"

namespace
{

zx::Bytes ReadBytes( BYTESTREAM_s *pByteStream, int count )
{
	zx::Bytes out;
	for ( int i = 0; i < count; ++i )
		out.push_back( static_cast<unsigned char>( pByteStream->ReadByte( )));
	return out;
}

zx::Bytes FromArray( const TArray<unsigned char> &in )
{
	zx::Bytes out;
	for ( unsigned i = 0; i < in.Size( ); ++i )
		out.push_back( in[i] );
	return out;
}

// [rc4l] Our own, rather than sv_auth.cpp's: that file goes away with the account server, and
// this check has to outlive it.
bool AccountAlreadyHere( const char *account, ULONG ulExcept )
{
	for ( ULONG i = 0; i < MAXPLAYERS; ++i )
	{
		if (( i == ulExcept ) || ( SERVER_IsValidClient( i ) == false ))
			continue;

		if ( SERVER_GetClient( i )->username.CompareNoCase( account ) == 0 )
			return true;
	}

	return false;
}

void ToArray( const zx::Bytes &in, TArray<unsigned char> &out )
{
	out.Clear( );
	for ( size_t i = 0; i < in.size( ); ++i )
		out.Push( in[i] );
}

} // namespace

//*****************************************************************************
// SERVER

void SERVERCOMMANDS_FuaAuthChallenge( const ULONG ulClient, const zx::Bytes &serverPublic,
	const zx::Bytes &ephemeralPublic, const zx::Bytes &signature )
{
	if ( SERVER_IsValidClient( ulClient ) == false )
		return;

	NetCommand command( SVC2_FUA_AUTH_CHALLENGE );

	for ( size_t i = 0; i < serverPublic.size( ); ++i )
		command.addByte( serverPublic[i] );
	for ( size_t i = 0; i < ephemeralPublic.size( ); ++i )
		command.addByte( ephemeralPublic[i] );

	command.addByte( static_cast<int>( signature.size( )));
	for ( size_t i = 0; i < signature.size( ); ++i )
		command.addByte( signature[i] );

	command.sendCommandToClients( ulClient, SVCF_ONLYTHISCLIENT );
}

bool SERVER_ProcessFuaAuthCommand( LONG lCommand, BYTESTREAM_s *pByteStream )
{
	const ULONG ulClient = SERVER_GetCurrentClient( );
	if ( SERVER_IsValidClient( ulClient ) == false )
		return true;

	CLIENT_s *pClient = SERVER_GetClient( ulClient );

	switch ( lCommand )
	{
	case CLC_FUA_AUTH_HELLO:
		{
			const zx::Bytes nonce = ReadBytes( pByteStream, 16 );
			const zx::Bytes clientEphemeral = ReadBytes( pByteStream, 32 );

			ToArray( nonce, pClient->fuaClientNonce );
			ToArray( clientEphemeral, pClient->fuaClientEphemeral );

			zx::KeyPair ephemeral;
			if ( !zx::Identity_NewEphemeral( ephemeral ))
				return false;

			ToArray( ephemeral.privateKey, pClient->fuaEphemeralPrivate );

			// [rc4l] Signed over the client's nonce, so the client knows this answer was made for
			// its request and is not a recording of an older one.
			const zx::Bytes &serverPublic = zx::Identity_ServerPublicKey( );
			const std::string message = zx::ServerProofMessage(
				zx::ToHex( nonce ), zx::ToHex( serverPublic ));

			zx::Bytes signature;
			if ( !zx::Identity_SignAsServer( message, signature ))
				return false;

			SERVERCOMMANDS_FuaAuthChallenge( ulClient, serverPublic, ephemeral.publicKey, signature );
		}
		return false;

	case CLC_FUA_AUTH_PROOF:
		{
			const zx::Bytes accountPublic = ReadBytes( pByteStream, 32 );
			const int lenSig = pByteStream->ReadByte( );
			const zx::Bytes signature = ReadBytes( pByteStream, ( lenSig > 0 ) ? lenSig : 0 );

			zx::Bytes session;
			if ( !zx::Identity_SharedSession( FromArray( pClient->fuaEphemeralPrivate ),
				FromArray( pClient->fuaClientEphemeral ), session ))
			{
				return false;
			}

			const std::string message = zx::ClientProofMessage(
				zx::ToHex( session ), zx::ToHex( zx::Identity_ServerPublicKey( )));

			if ( !zx::Identity_Verify( accountPublic, message, signature ))
			{
				// [rc4l] Said plainly rather than dropped quietly: a player whose proof fails has a
				// broken install or a tampering middlebox, and neither is their fault to guess at.
				SERVER_ClientError( ulClient, NETWORK_ERRORCODE_AUTHENTICATIONFAILED );
				return true;
			}

			const std::string account = zx::Identity_AccountName( accountPublic );

			// [rc4l] Refused rather than shared. Two sessions on one account means somebody else
			// holds that key, and letting both in would let the second one act as the first.
			if ( AccountAlreadyHere( account.c_str( ), ulClient ))
			{
				SERVER_ClientError( ulClient, NETWORK_ERRORCODE_AUTHENTICATIONFAILED );
				return true;
			}

			pClient->username = account.c_str( );
			pClient->loggedIn = true;

			// [rc4l] Tell everyone, now, rather than leaving it to the userinfo exchange.
			//
			// That exchange races this one and usually wins, so the account was being established
			// on the server and never reaching a single screen: playerinfo reported the anonymous
			// placeholder for a player who had in fact proved who they were. Found by asking it
			// rather than by trusting that a successful join meant a successful login.
			if ( PLAYER_IsValidPlayer( ulClient ))
				SERVERCOMMANDS_SetPlayerAccountName( ulClient );

			// Nothing further needs the exchange, and a kept private key is a key that can leak.
			pClient->fuaEphemeralPrivate.Clear( );
			pClient->fuaClientEphemeral.Clear( );
			pClient->fuaClientNonce.Clear( );
		}
		return false;
	}

	return false;
}


//*****************************************************************************
// CLIENT

namespace
{

// [rc4l] Held between the two halves of the exchange. The nonce proves the server's answer was made
// for us, and the ephemeral private key turns it into a session only we two share.
zx::Bytes g_ClientNonce;
zx::KeyPair g_ClientEphemeral;

} // namespace

namespace
{

// [rc4l] Written straight onto the outgoing buffer, the way the login messages this replaces were.
void SendBytes( const zx::Bytes &bytes )
{
	for ( size_t i = 0; i < bytes.size( ); ++i )
		CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( bytes[i] );
}

} // namespace

void CLIENTCOMMANDS_FuaAuthHello( const zx::Bytes &nonce, const zx::Bytes &ephemeralPublic )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_FUA_AUTH_HELLO );
	SendBytes( nonce );
	SendBytes( ephemeralPublic );
}

void CLIENTCOMMANDS_FuaAuthProof( const zx::Bytes &accountPublic, const zx::Bytes &signature )
{
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( CLC_FUA_AUTH_PROOF );
	SendBytes( accountPublic );
	CLIENT_GetLocalBuffer( )->ByteStream.WriteByte( static_cast<int>( signature.size( )));
	SendBytes( signature );
}

void CLIENT_FuaAuthSendHello( void )
{
	if ( !zx::Identity_RandomBytes( 16, g_ClientNonce ))
		return;

	if ( !zx::Identity_NewEphemeral( g_ClientEphemeral ))
		return;

	CLIENTCOMMANDS_FuaAuthHello( g_ClientNonce, g_ClientEphemeral.publicKey );
}

void CLIENT_FuaAuthHandleChallenge( BYTESTREAM_s *pByteStream )
{
	const zx::Bytes serverPublic = ReadBytes( pByteStream, 32 );
	const zx::Bytes serverEphemeral = ReadBytes( pByteStream, 32 );
	const int lenSig = pByteStream->ReadByte( );
	const zx::Bytes signature = ReadBytes( pByteStream, ( lenSig > 0 ) ? lenSig : 0 );

	// [rc4l] THE step this design turns on. Verified BEFORE we derive an account or sign anything,
	// so a server that copied a real public key gets nothing it could relay onwards: it cannot
	// produce this signature, and we stop here.
	const std::string expected = zx::ServerProofMessage(
		zx::ToHex( g_ClientNonce ), zx::ToHex( serverPublic ));

	if ( !zx::Identity_Verify( serverPublic, expected, signature ))
	{
		Printf( TEXTCOLOR_RED "This server did not prove its identity, so nothing was sent to it.\n" );
		return;
	}

	zx::Bytes session;
	if ( !zx::Identity_SharedSession( g_ClientEphemeral.privateKey, serverEphemeral, session ))
		return;

	const zx::KeyPair account = zx::Identity_DeriveAccount( serverPublic );
	if ( !account.IsValid( ))
		return;

	const std::string message = zx::ClientProofMessage(
		zx::ToHex( session ), zx::ToHex( serverPublic ));

	zx::Bytes proof;
	if ( !zx::Identity_Sign( account.privateKey, message, proof ))
		return;

	CLIENTCOMMANDS_FuaAuthProof( account.publicKey, proof );
}
