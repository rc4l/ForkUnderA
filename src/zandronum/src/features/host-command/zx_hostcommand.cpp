// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] fua_hostmap: host the CURRENTLY LOADED wad set on a given map, from the console.
//
// Distinct from fua_host, which hosts a catalogue entry by id and is the right command when the
// thing you want is one of those. This one takes a map and whatever is already loaded, which is what
// a script needs when there is no catalogue involved -- and what makes hosting assertable end to
// end, since checking that leaving a hosted game brings it back requires starting one first.
//
// The parsing, which is the part worth being sure about, lives in
// computation/hostcommand_compute.cpp.

#include "features/host-command/computation/hostcommand_compute.h"
#include "features/continue/zx_continue.h"
#include "features/server-hosting/zx_hosting.h"

#include "c_dispatch.h"
#include "network.h"
#include "w_wad.h"

#include <string>
#include <vector>

CCMD( fua_hostmap )
{
	std::vector<std::string> args;
	for ( int i = 1; i < argv.argc( ); ++i )
		args.push_back( argv[i] );

	zx::HostConfig config;
	std::string error;

	if ( zx::ParseHostCommand( args, config, error ) == false )
	{
		Printf( "fua_hostmap: %s\n", error.c_str( ));
		Printf( "usage: fua_hostmap <map> [name <server name>] [port <n>] [players <n>] [file <wad>]...\n" );
		return;
	}

	// [rc4l] The WAD set comes from what is loaded, never from the line: hosting a set you are not
	// holding is a different feature, and one the menus already own.
	const char *iwad = NETWORK_GetIWAD( );
	if (( iwad != NULL ) && ( *iwad != 0 ))
		config.iwad = iwad;

	if ( config.pwads.empty( ))
	{
		const TArray<NetworkPWAD> &loaded = NETWORK_GetPWADList( );
		for ( unsigned int i = 0; i < loaded.Size( ); ++i )
		{
			if ( loaded[i].name.IsEmpty( ) == false )
				config.pwads.push_back( loaded[i].name.GetChars( ));
		}
	}

	if ( zx::HostStart( config ) == false )
	{
		Printf( "fua_hostmap: could not start the server.\n" );
		return;
	}

	// [rc4l] Starting it is not joining it: the child is not listening yet, and the menu path waits
	// for the ready edge before connecting. Say so, rather than leave somebody wondering why they
	// are still standing in the old map.
	Printf( "fua_hostmap: starting %s on %s; joining when it is ready.\n",
		config.hostName.c_str( ), config.map.c_str( ));
	zx::Continue_JoinHostWhenReady( );
}
