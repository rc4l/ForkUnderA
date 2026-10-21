// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/identity/zx_identity.h"

#include "c_dispatch.h"
#include "cmdlib.h"
#include "m_misc.h"
#include "doomtype.h"
#include "v_text.h"

#include "features/identity/computation/identity_compute.h"

#include "features/identity/zx_identitycrypto.h"
#include "features/wad-download/computation/digest_compute.h"   // [rc4l] SHA-256 without a library

#include <stdio.h>

// [rc4l] The CRT rather than windows.h, which collides with the bundled dxsdk headers here.
#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <share.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{

// [rc4l] Read once at startup and held here, so a handshake never touches the disk.
zx::KeyPair g_ClientKey;
zx::KeyPair g_ServerKey;

const char kSeedTag[] = "seed = ";

// [rc4l] How many copies of the engine one machine may run with accounts of their own.
const int kMaxLocalInstances = 8;

// [rc4l] How far a spare will look for a free slot, well past the copies anyone runs at once,
// because the point is to always find one rather than to ration them.
const int kMaxSpareInstances = 64;

// [rc4l] Held open for the life of the process, which is what reserves this instance's key.
int g_InstanceLock = -1;

// Which key this copy is playing as, so a spare can start looking after it.
int g_Instance = 0;
std::string g_IdentityRoot;

// [rc4l] Claim a key by taking an exclusive lock on a file beside it.
//
// The lock rather than a "does the file exist" check, because two copies launched together would
// both see the same answer and both take the same key. An exclusive open is decided by the OS, so
// exactly one of them wins however close together they ask.
bool ClaimInstance( const std::string &keyPath )
{
	const std::string lockPath = keyPath + ".lock";

#ifdef _WIN32
	// Deny-read-write is the whole mechanism: a second copy asking for the same file is refused
	// by the OS rather than by a check we could lose a race on.
	int fd = -1;
	if ( _sopen_s( &fd, lockPath.c_str( ), _O_RDWR | _O_CREAT, _SH_DENYRW, _S_IREAD | _S_IWRITE ) != 0 )
		return false;

	if ( fd < 0 )
		return false;
#else
	const int fd = open( lockPath.c_str( ), O_RDWR | O_CREAT, 0600 );
	if ( fd < 0 )
		return false;

	if ( flock( fd, LOCK_EX | LOCK_NB ) != 0 )
	{
		close( fd );
		return false;
	}
#endif

	g_InstanceLock = fd;
	return true;
}

void ReleaseClaim( void )
{
	if ( g_InstanceLock < 0 )
		return;

#ifdef _WIN32
	_close( g_InstanceLock );
#else
	close( g_InstanceLock );
#endif

	g_InstanceLock = -1;
}

// [rc4l] The derivation from the master secret to one operator's account, a plain SHA-256 because
// the input already holds 256 bits of entropy and no stretching is called for.
void DeriveSeed( const zx::Bytes &master, const zx::Bytes &serverPublicKey, zx::Bytes &out )
{
	static const char kTag[] = "FUA-IDENTITY-v1-account";

	zx::Sha256State ctx;
	zx::ComputeSha256Init( ctx );
	zx::ComputeSha256Update( ctx, kTag, sizeof( kTag ) - 1 );
	zx::ComputeSha256Update( ctx, &master[0], master.size( ));
	if ( !serverPublicKey.empty( ))
		zx::ComputeSha256Update( ctx, &serverPublicKey[0], serverPublicKey.size( ));

	out.resize( 32 );
	zx::ComputeSha256Final( ctx, &out[0] );
}

// The public half of an Ed25519 key, given its 32-byte seed.
bool PublicFromSeed( const zx::Bytes &seed, zx::Bytes &out )
{
	if ( seed.size( ) != 32 )
		return false;

	out.resize( 32 );
	return zx::crypto::Ed25519PublicFromSeed( &seed[0], &out[0] );
}

// [rc4l] The key file is text so a player can back it up by copying the line out of it.
bool WriteKeyFile( const char *path, const zx::Bytes &seed )
{
	FString dir = path;
	FixPathSeperator( dir );
	const long slash = dir.LastIndexOf( '/' );
	if ( slash >= 0 )
	{
		dir.Truncate( slash );
		CreatePath( dir.GetChars( ));
	}

	FILE *f = fopen( path, "wb" );
	if ( f == NULL )
		return false;

	fprintf( f, "# ForkUnderA identity, version 1\n" );
	fprintf( f, "# Every account you own is derived from this line.\n" );
	fprintf( f, "# Anyone holding it is you. Back it up. There is no recovery.\n" );
	fprintf( f, "%s%s\n", kSeedTag, zx::ToHex( seed ).c_str( ));

	return ( fclose( f ) == 0 );
}

bool ReadKeyFile( const char *path, zx::Bytes &seed )
{
	FILE *f = fopen( path, "rb" );
	if ( f == NULL )
		return false;

	char line[256];
	bool bFound = false;

	while ( fgets( line, sizeof( line ), f ) != NULL )
	{
		if ( strncmp( line, kSeedTag, sizeof( kSeedTag ) - 1 ) != 0 )
			continue;

		std::string hex( line + sizeof( kSeedTag ) - 1 );
		while ( !hex.empty( ) && (( hex[hex.size( ) - 1] == '\n' ) || ( hex[hex.size( ) - 1] == '\r' )))
			hex.erase( hex.size( ) - 1 );

		bFound = zx::FromHex( hex, seed ) && ( seed.size( ) == 32 );
		break;
	}

	fclose( f );
	return bFound;
}

// Load `path`, or create it with a fresh secret, where `what` names the file in the notice.
bool LoadOrCreate( const std::string &path, const char *what, zx::KeyPair &out )
{
	if ( path.empty( ))
		return false;

	zx::Bytes seed;

	if ( !ReadKeyFile( path.c_str( ), seed ))
	{
		if ( !zx::Identity_RandomBytes( 32, seed ))
			return false;

		if ( !WriteKeyFile( path.c_str( ), seed ))
		{
			Printf( TEXTCOLOR_RED "Could not write %s\n", path.c_str( ));
			return false;
		}

		// [rc4l] Said once and loudly, because no server holds a copy and a lost file is a lost
		// account.
		Printf( TEXTCOLOR_GOLD "Created your %s.\n" TEXTCOLOR_NORMAL
			"%s\nBack it up. Anyone holding it is you, and losing it cannot be undone.\n",
			what, path.c_str( ));
	}

	out.privateKey = seed;
	return PublicFromSeed( seed, out.publicKey );
}

} // namespace

namespace zx
{

std::string Identity_ServerRegistryId( int port )
{
	// [rc4l] No identity means no grouping, since an id derived from nothing would be the same on
	// every such server.
	if ( g_ServerKey.privateKey.empty( ) || ( port <= 0 ))
		return std::string( );

	static const char kTag[] = "FUA-REGISTRY-ID-v1";

	char szPort[16];
	snprintf( szPort, sizeof( szPort ), "%d", port );

	Sha256State ctx;
	ComputeSha256Init( ctx );
	ComputeSha256Update( ctx, kTag, sizeof( kTag ) - 1 );

	// [rc4l] The SECRET, not the public key, which every player who joins already holds and could
	// therefore recompute and claim.
	ComputeSha256Update( ctx, &g_ServerKey.privateKey[0], g_ServerKey.privateKey.size( ));
	ComputeSha256Update( ctx, szPort, strlen( szPort ));

	Bytes digest;
	digest.resize( 32 );
	ComputeSha256Final( ctx, &digest[0] );

	return ToHex( digest );
}

bool Identity_RandomBytes( size_t count, Bytes &out )
{
	out.resize( count );
	return ( count == 0 ) || crypto::RandomBytes( &out[0], count );
}

bool Identity_InitClient( const char *configRoot, int instance )
{
	return LoadOrCreate( ClientAuthKeyPath( configRoot ? configRoot : "", instance ),
		"client identity", g_ClientKey );
}

int Identity_InitClientHere( const char *configRoot )
{
	const std::string root = configRoot ? configRoot : "";

	for ( int instance = 0; instance < kMaxLocalInstances; ++instance )
	{
		const std::string path = ClientAuthKeyPath( root, instance );
		if ( path.empty( ))
			break;

		if ( !ClaimInstance( path ))
			continue;

		if ( Identity_InitClient( configRoot, instance ))
		{
			g_Instance = instance;
			g_IdentityRoot = root;

			if ( instance > 0 )
			{
				Printf( "Identity: this is copy %d on this machine, so it plays as its own account.\n",
					instance + 1 );
			}

			return instance;
		}

		ReleaseClaim( );
	}

	// [rc4l] Every numbered key is spoken for, so fall back to the first one and let the server
	// refuse the duplicate rather than starting with no identity at all.
	Identity_InitClient( configRoot, 0 );
	return 0;
}

bool Identity_SwitchToSpare( void )
{
	// [rc4l] Take the next key nothing else holds, so being locked out of an account is not being
	// locked out of the game.
	//
	// A whole account rather than a one-off name, because a throwaway that changed every session
	// would lose the player's progress on every server they went on to play, which is the thing
	// this was meant to save.
	for ( int instance = g_Instance + 1; instance < kMaxSpareInstances; ++instance )
	{
		const std::string path = ClientAuthKeyPath( g_IdentityRoot, instance );
		if ( path.empty( ))
			break;

		const int held = g_InstanceLock;
		g_InstanceLock = -1;

		if ( !ClaimInstance( path ))
		{
			g_InstanceLock = held;
			continue;
		}

		if ( !Identity_InitClient( g_IdentityRoot.c_str( ), instance ))
		{
			ReleaseClaim( );
			g_InstanceLock = held;
			continue;
		}

		// Only now is the old one given up, so a failure above leaves this copy as it was.
		if ( held >= 0 )
		{
#ifdef _WIN32
			_close( held );
#else
			close( held );
#endif
		}

		g_Instance = instance;
		return true;
	}

	return false;
}

bool Identity_InitServer( const char *configRoot )
{
	// [rc4l] Worth its own warning, because losing this one orphans every player's progression
	// rather than one person's.
	return LoadOrCreate( ServerAuthKeyPath( configRoot ? configRoot : "" ),
		"server identity (every account on your server is derived from it)", g_ServerKey );
}

const Bytes &Identity_ServerPublicKey( void )
{
	return g_ServerKey.publicKey;
}

KeyPair Identity_DeriveAccount( const Bytes &serverPublicKey )
{
	KeyPair out;

	if ( !g_ClientKey.IsValid( ))
		return out;

	Bytes seed;
	DeriveSeed( g_ClientKey.privateKey, serverPublicKey, seed );

	if ( !PublicFromSeed( seed, out.publicKey ))
		return KeyPair( );

	out.privateKey = seed;
	return out;
}

std::string Identity_AccountName( const Bytes &publicKey )
{
	if ( publicKey.empty( ))
		return std::string( );

	Bytes digest( 32 );
	Sha256State ctx;
	ComputeSha256Init( ctx );
	ComputeSha256Update( ctx, &publicKey[0], publicKey.size( ));
	ComputeSha256Final( ctx, &digest[0] );

	return AccountNameFromDigest( digest );
}

bool Identity_Sign( const Bytes &privateKey, const std::string &message, Bytes &signatureOut )
{
	if ( privateKey.size( ) != 32 )
		return false;

	signatureOut.resize( 64 );
	return crypto::Ed25519Sign( &privateKey[0], reinterpret_cast<const unsigned char *>( message.data( )), message.size( ), &signatureOut[0] );
}

bool Identity_Verify( const Bytes &publicKey, const std::string &message, const Bytes &signature )
{
	if (( publicKey.size( ) != 32 ) || signature.empty( ))
		return false;

	return crypto::Ed25519Verify( &publicKey[0], reinterpret_cast<const unsigned char *>( message.data( )), message.size( ),
		&signature[0], signature.size( ));
}

bool Identity_SignAsServer( const std::string &message, Bytes &signatureOut )
{
	if ( !g_ServerKey.IsValid( ))
		return false;

	return Identity_Sign( g_ServerKey.privateKey, message, signatureOut );
}

bool Identity_NewEphemeral( KeyPair &out )
{
	out.publicKey.resize( 32 );
	out.privateKey.resize( 32 );
	return crypto::X25519NewKey( &out.publicKey[0], &out.privateKey[0] );
}

bool Identity_SharedSession( const Bytes &ourPrivate, const Bytes &theirPublic, Bytes &sessionIdOut )
{
	if (( ourPrivate.size( ) != 32 ) || ( theirPublic.size( ) != 32 ))
		return false;

	unsigned char shared[32];
	if ( !crypto::X25519Shared( &ourPrivate[0], &theirPublic[0], shared ))
		return false;

	// [rc4l] Hashed rather than used raw, so the shared secret never appears in a
	// signed message that could be logged.
	sessionIdOut.resize( 32 );
	Sha256State ctx;
	ComputeSha256Init( ctx );
	ComputeSha256Update( ctx, shared, sizeof( shared ));
	ComputeSha256Final( ctx, &sessionIdOut[0] );
	return true;
}

namespace
{

} // namespace

int Identity_Instance( void )
{
	return g_Instance;
}

std::string Identity_ConfigRoot( void )
{
	// [rc4l] The same folder the IWAD store uses, so a player's account and their games sit
	// together rather than in two unrelated corners of the profile.
	return std::string( M_GetFuaUserPath( ).GetChars( ));
}

} // namespace zx

// [rc4l] Proves the crypto end to end without a server, so "authentication is broken" is a short
// way from knowing which of the five steps broke.
CCMD( fua_identity )
{
	const std::string root = zx::Identity_ConfigRoot( );

	if ( !zx::Identity_InitClient( root.c_str( ), 0 ))
	{
		Printf( TEXTCOLOR_RED "Could not load or create the client identity.\n" );
		return;
	}

	zx::Bytes fakeServer;
	zx::Identity_RandomBytes( 32, fakeServer );

	const zx::KeyPair account = zx::Identity_DeriveAccount( fakeServer );
	Printf( "account here: %s\n", zx::Identity_AccountName( account.publicKey ).c_str( ));

	// The same server twice must give the same account, or no progression survives a reconnect.
	const zx::KeyPair again = zx::Identity_DeriveAccount( fakeServer );
	Printf( "stable across derivations: %s\n",
		( account.publicKey == again.publicKey ) ? "yes" : TEXTCOLOR_RED "NO" );

	// A different server must give a different account, or every operator sees the same player.
	zx::Bytes otherServer;
	zx::Identity_RandomBytes( 32, otherServer );
	Printf( "different per server: %s\n",
		( zx::Identity_DeriveAccount( otherServer ).publicKey != account.publicKey )
			? "yes" : TEXTCOLOR_RED "NO" );

	const std::string message = zx::ClientProofMessage( "0011", "2233" );
	zx::Bytes sig;

	Printf( "sign: %s\n", zx::Identity_Sign( account.privateKey, message, sig ) ? "ok" : TEXTCOLOR_RED "FAILED" );
	Printf( "verify: %s\n",
		zx::Identity_Verify( account.publicKey, message, sig ) ? "ok" : TEXTCOLOR_RED "FAILED" );

	// The one that matters, since a signature must not verify against another message.
	Printf( "rejects a forgery: %s\n",
		zx::Identity_Verify( account.publicKey, message + "x", sig ) ? TEXTCOLOR_RED "NO" : "yes" );

	zx::KeyPair a, b;
	zx::Bytes sessionA, sessionB;

	if ( zx::Identity_NewEphemeral( a ) && zx::Identity_NewEphemeral( b )
		&& zx::Identity_SharedSession( a.privateKey, b.publicKey, sessionA )
		&& zx::Identity_SharedSession( b.privateKey, a.publicKey, sessionB ))
	{
		Printf( "session agreed by both ends: %s\n",
			( sessionA == sessionB ) ? "yes" : TEXTCOLOR_RED "NO" );
	}
	else
		Printf( TEXTCOLOR_RED "session exchange FAILED\n" );
}
