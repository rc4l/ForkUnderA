// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/iwad-registry/zx_iwadregistry.h"

#include "c_dispatch.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "m_misc.h"
#include "v_text.h"

#include "features/iwad-registry/computation/iwadregistry_compute.h"
#include "features/wad-download/zx_filehash.h"

#include <stdio.h>

namespace
{

// [rc4l] Copied in fixed-size pieces rather than read whole, since a copy that allocates the file's
// own size is a copy that fails on exactly the files most worth keeping.
bool CopyFileBytes( const char *from, const char *to )
{
	FILE *in = fopen( from, "rb" );
	if ( in == NULL )
		return false;

	FILE *out = fopen( to, "wb" );
	if ( out == NULL )
	{
		fclose( in );
		return false;
	}

	char buffer[64 * 1024];
	bool bOk = true;

	for ( ;; )
	{
		const size_t got = fread( buffer, 1, sizeof( buffer ), in );
		if ( got == 0 )
		{
			bOk = ( ferror( in ) == 0 );
			break;
		}

		if ( fwrite( buffer, 1, got, out ) != got )
		{
			bOk = false;
			break;
		}
	}

	fclose( in );

	// Checked rather than assumed, because a buffered write fails at close as readily as at write
	// and a truncated copy filed under a whole file's digest is the one outcome to never produce.
	if ( fclose( out ) != 0 )
		bOk = false;

	if ( !bOk )
		remove( to );

	return bOk;
}

} // namespace

namespace zx
{

std::string IwadRegistryRoot( void )
{
	// The same place the registry list already caches into, which is machine-local rather than the
	// roaming profile that would sync tens of megabytes onto every machine an account touches.
	FString dir = M_GetCachePath( true );
	if ( dir.IsEmpty( ))
		return std::string( );

	return std::string( dir.GetChars( ));
}

std::string RegisterIwad( const char *path )
{
	if (( path == NULL ) || ( *path == '\0' ) || !FileExists( path ))
		return std::string( );

	const std::string root = IwadRegistryRoot( );
	if ( root.empty( ))
		return std::string( );

	char hex[80];
	if ( !Sha256OfFile( path, hex, sizeof( hex )))
		return std::string( );

	// The leaf keeps the file's own name, everything before it being where this copy happened to
	// come from rather than anything about what it is.
	FString leaf = path;
	FixPathSeperator( leaf );
	const long slash = leaf.LastIndexOf( '/' );
	if ( slash >= 0 )
		leaf = leaf.Mid( slash + 1 );

	const std::string dest = IwadStorePath( root, hex, std::string( leaf.GetChars( )));
	if ( dest.empty( ))
		return std::string( );

	// Already registered, and since the destination IS the digest this is the whole check, with no
	// list to keep and nothing to go stale if a player empties the folder by hand.
	if ( FileExists( dest.c_str( )))
		return dest;

	const std::string dir = IwadStoreDir( root, hex );
	CreatePath( dir.c_str( ));

	if ( !CopyFileBytes( path, dest.c_str( )))
		return std::string( );

	return dest;
}

} // namespace zx

// [rc4l] Registering a file by hand, which is how somebody points us at a copy they already own
// rather than waiting for the engine to happen across it.
CCMD( fua_iwadregister )
{
	if ( argv.argc( ) < 2 )
	{
		Printf( "fua_iwadregister <path>: copy an IWAD into the shared store.\n" );
		Printf( "Store: %s/ForkUnderA/core/iwads/\n", zx::IwadRegistryRoot( ).c_str( ));
		return;
	}

	const std::string at = zx::RegisterIwad( argv[1] );

	if ( at.empty( ))
		Printf( TEXTCOLOR_RED "Could not register %s\n", argv[1] );
	else
		Printf( "Registered: %s\n", at.c_str( ));
}
