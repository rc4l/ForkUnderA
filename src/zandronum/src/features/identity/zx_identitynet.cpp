// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] See zx_identitynet.h for the shape of the exchange and why the order matters.

#include "features/identity/zx_identitynet.h"

#include "cl_demo.h"
#include "cl_main.h"
#include "d_netinf.h"
#include "doomtype.h"
#include "network.h"
#include "networkshared.h"
#include "network_enums.h"
#include "sv_commands.h"
#include "sv_main.h"
#include "p_local.h"
#include "doomstat.h"
#include "v_text.h"

#include "features/identity/computation/connectchallenge_compute.h"
#include "features/identity/computation/identity_compute.h"
#include "features/identity/zx_identity.h"

namespace
{

zx::Bytes ReadBytes( BYTESTREAM_s *pByteStream, size_t count )
{
	zx::Bytes out;
	for ( size_t i = 0; i < count; ++i )
		out.push_back( static_cast<unsigned char>( pByteStream->ReadByte( )));
	return out;
}

void WriteBytes( BYTESTREAM_s *pByteStream, const zx::Bytes &bytes )
{
	for ( size_t i = 0; i < bytes.size( ); ++i )
		pByteStream->WriteByte( bytes[i] );
}

zx::Bytes FromArray( const TArray<unsigned char> &in )
{
	zx::Bytes out;
	for ( unsigned i = 0; i < in.Size( ); ++i )
		out.push_back( in[i] );
	return out;
}

void ToArray( const zx::Bytes &in, TArray<unsigned char> &out )
{
	out.Clear( );
	for ( size_t i = 0; i < in.size( ); ++i )
		out.Push( in[i] );
}

// [rc4l] Our own rather than sv_auth.cpp's, which went away with the account server.
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

} // namespace

//*****************************************************************************
// SERVER

void SERVER_FuaAuthClearChallenge( ULONG ulClient )
{
	CLIENT_s *pClient = SERVER_GetClient( ulClient );
	if ( pClient == NULL )
		return;

	pClient->fuaHasChallenge = false;
	pClient->fuaClientNonce.Clear( );
	pClient->fuaClientEphemeral.Clear( );
	pClient->fuaEphemeralPrivate.Clear( );
	pClient->fuaServerEphemeralPublic.Clear( );
	pClient->fuaChallengeSignature.Clear( );
}

bool SERVER_FuaAuthReadHello( ULONG ulClient, BYTESTREAM_s *pByteStream )
{
	CLIENT_s *pClient = SERVER_GetClient( ulClient );
	if ( pClient == NULL )
		return false;

	const zx::Bytes nonce = ReadBytes( pByteStream, zx::kNonceBytes );
	const zx::Bytes clientEphemeral = ReadBytes( pByteStream, zx::kKeyBytes );

	if ( !zx::IsWellFormedHello( nonce.size( ), clientEphemeral.size( )))
		return false;

	// [rc4l] Replayed rather than reminted for a client we have already answered, because the
	// connection sequence has no acks and this is the same request arriving again.
	const bool bSameClient = ( FromArray( pClient->fuaClientNonce ) == nonce );

	if ( zx::ChallengeActionForAttempt( pClient->fuaHasChallenge, bSameClient ) == zx::CHALLENGE_REPLAY )
		return true;

	zx::KeyPair ephemeral;
	if ( !zx::Identity_NewEphemeral( ephemeral ))
		return false;

	// [rc4l] Signed over the client's nonce, so this answer cannot be a recording of an older one.
	const zx::Bytes &serverPublic = zx::Identity_ServerPublicKey( );
	const std::string message = zx::ServerProofMessage(
		zx::ToHex( nonce ), zx::ToHex( serverPublic ));

	zx::Bytes signature;
	if ( !zx::Identity_SignAsServer( message, signature ))
		return false;

	ToArray( nonce, pClient->fuaClientNonce );
	ToArray( clientEphemeral, pClient->fuaClientEphemeral );
	ToArray( ephemeral.privateKey, pClient->fuaEphemeralPrivate );
	ToArray( ephemeral.publicKey, pClient->fuaServerEphemeralPublic );
	ToArray( signature, pClient->fuaChallengeSignature );
	pClient->fuaHasChallenge = true;

	return true;
}

void SERVER_FuaAuthWriteChallenge( ULONG ulClient, BYTESTREAM_s *pByteStream )
{
	CLIENT_s *pClient = SERVER_GetClient( ulClient );
	if (( pClient == NULL ) || ( pClient->fuaHasChallenge == false ))
		return;

	WriteBytes( pByteStream, zx::Identity_ServerPublicKey( ));
	WriteBytes( pByteStream, FromArray( pClient->fuaServerEphemeralPublic ));
	WriteBytes( pByteStream, FromArray( pClient->fuaChallengeSignature ));
}

bool SERVER_FuaAuthReadProof( ULONG ulClient, BYTESTREAM_s *pByteStream )
{
	CLIENT_s *pClient = SERVER_GetClient( ulClient );
	if ( pClient == NULL )
		return false;

	const zx::Bytes accountPublic = ReadBytes( pByteStream, zx::kKeyBytes );
	const zx::Bytes signature = ReadBytes( pByteStream, zx::kSignatureBytes );

	if ( zx::ProofReadiness( pClient->fuaHasChallenge, accountPublic.size( ), signature.size( ))
		!= zx::PROOF_JUDGE )
	{
		return false;
	}

	// [rc4l] Already established, so this is a retry of a message we answered and must answer the
	// same way rather than judging it twice.
	if ( pClient->loggedIn )
		return true;

	zx::Bytes session;
	if ( !zx::Identity_SharedSession( FromArray( pClient->fuaEphemeralPrivate ),
		FromArray( pClient->fuaClientEphemeral ), session ))
	{
		return false;
	}

	const std::string message = zx::ClientProofMessage(
		zx::ToHex( session ), zx::ToHex( zx::Identity_ServerPublicKey( )));

	if ( !zx::Identity_Verify( accountPublic, message, signature ))
		return false;

	const std::string account = zx::Identity_AccountName( accountPublic );

	// [rc4l] Refused rather than shared, because letting both in would let the second one act as
	// the first.
	if ( AccountAlreadyHere( account.c_str( ), ulClient ))
	{
		SERVER_ClientError( ulClient, NETWORK_ERRORCODE_IDENTITYINUSE );
		return false;
	}

	pClient->username = account.c_str( );
	pClient->loggedIn = true;

	// A kept private key is a key that can leak, and the exchange is over.
	pClient->fuaEphemeralPrivate.Clear( );

	return true;
}

//*****************************************************************************
// CLIENT

namespace
{

// [rc4l] Minted once per connection and reused across retries, for the same reason the server
// replays its challenge: a nonce that changed between attempts would leave the server's signature
// covering something we no longer remember asking.
zx::Bytes g_ClientNonce;
zx::KeyPair g_ClientEphemeral;

// What the challenge worked out to, kept so a resent CLCC_ATTEMPTAUTHENTICATION carries the same
// bytes as the first one.
zx::Bytes g_AccountPublic;
zx::Bytes g_Proof;

} // namespace

void CLIENT_FuaAuthReset( void )
{
	g_ClientNonce.clear( );
	g_ClientEphemeral.publicKey.clear( );
	g_ClientEphemeral.privateKey.clear( );
	g_AccountPublic.clear( );
	g_Proof.clear( );
}

void CLIENT_FuaAuthWriteHello( BYTESTREAM_s *pByteStream )
{
	if ( g_ClientNonce.empty( ) || !g_ClientEphemeral.IsValid( ))
	{
		if ( !zx::Identity_RandomBytes( zx::kNonceBytes, g_ClientNonce ))
			return;

		if ( !zx::Identity_NewEphemeral( g_ClientEphemeral ))
			return;
	}

	WriteBytes( pByteStream, g_ClientNonce );
	WriteBytes( pByteStream, g_ClientEphemeral.publicKey );
}

bool CLIENT_FuaAuthReadChallenge( BYTESTREAM_s *pByteStream )
{
	const zx::Bytes serverPublic = ReadBytes( pByteStream, zx::kKeyBytes );
	const zx::Bytes serverEphemeral = ReadBytes( pByteStream, zx::kKeyBytes );
	const zx::Bytes signature = ReadBytes( pByteStream, zx::kSignatureBytes );

	// [rc4l] THE step this design turns on, verified before we derive an account or sign anything,
	// so a server that only copied a public key gets nothing it could relay onwards.
	const std::string expected = zx::ServerProofMessage(
		zx::ToHex( g_ClientNonce ), zx::ToHex( serverPublic ));

	if ( !zx::Identity_Verify( serverPublic, expected, signature ))
	{
		Printf( TEXTCOLOR_RED "This server did not prove its identity, so nothing was sent to it.\n" );
		return false;
	}

	zx::Bytes session;
	if ( !zx::Identity_SharedSession( g_ClientEphemeral.privateKey, serverEphemeral, session ))
		return false;

	const zx::KeyPair account = zx::Identity_DeriveAccount( serverPublic );
	if ( !account.IsValid( ))
		return false;

	const std::string message = zx::ClientProofMessage(
		zx::ToHex( session ), zx::ToHex( serverPublic ));

	if ( !zx::Identity_Sign( account.privateKey, message, g_Proof ))
		return false;

	g_AccountPublic = account.publicKey;
	return true;
}

void CLIENT_FuaAuthWriteProof( BYTESTREAM_s *pByteStream )
{
	WriteBytes( pByteStream, g_AccountPublic );
	WriteBytes( pByteStream, g_Proof );
}
