// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/zx_catalogue.h"

#include "cmdlib.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "m_misc.h"
#include "templates.h"
#include "v_text.h"

#include <stdio.h>

namespace zx
{

namespace
{

std::vector<CatalogueEntry> g_Entries;
bool g_Loaded = false;

bool ReadWholeFile( const char *path, std::string &out )
{
	FILE *f = fopen( path, "rb" );
	if ( f == NULL )
		return false;

	// [rc4l] A catalogue entry is a few hundred bytes. Anything past this is not one, and refusing it
	// unread means a stray gigabyte in the folder cannot stall startup.
	const long kSaneMax = 256 * 1024;

	fseek( f, 0, SEEK_END );
	const long size = ftell( f );
	fseek( f, 0, SEEK_SET );

	if (( size < 0 ) || ( size > kSaneMax ))
	{
		fclose( f );
		return false;
	}

	out.resize( static_cast<size_t>( size ));
	const size_t got = ( size > 0 ) ? fread( &out[0], 1, static_cast<size_t>( size ), f ) : 0;
	fclose( f );

	out.resize( got );
	return true;
}

bool FileExists( const char *path )
{
	FILE *f = fopen( path, "rb" );
	if ( f == NULL )
		return false;
	fclose( f );
	return true;
}

// One root, shipped or the player's. Later roots win on a clashing id, which is what lets someone
// override a shipped entry whose hash has gone stale without waiting for a release.
void LoadRoot( const char *root, bool bShipped, std::vector<CatalogueEntry> &out )
{
	// [rc4l] MUST come first. The Windows ScanDirectory calls I_Error on a directory it cannot open,
	// so handing it a path that is not there is not an empty result -- it is the game dying at
	// startup, and almost nobody has a personal catalogue folder.
	if ( !DirEntryExists( root ))
		return;

	// It builds each entry as `dirpath` + name with nothing between, so the trailing separator is
	// load-bearing rather than tidiness.
	FString rootWithSlash = root;
	FixPathSeperator( rootWithSlash );
	if (( rootWithSlash.Len( ) > 0 ) && ( rootWithSlash[rootWithSlash.Len( ) - 1] != '/' ))
		rootWithSlash += "/";

	TArray<FFileList> found;
	ScanDirectory( found, rootWithSlash );

	for ( unsigned int i = 0; i < found.Size( ); ++i )
	{
		if ( !found[i].isDirectory )
			continue;

		// ScanDirectory hands back full paths; the id is the last component.
		FString path = found[i].Filename;
		FixPathSeperator( path );
		while (( path.Len( ) > 0 ) && ( path[path.Len( ) - 1] == '/' ))
			path.Truncate( path.Len( ) - 1 );

		const long slash = path.LastIndexOf( '/' );
		const FString id = ( slash >= 0 ) ? path.Mid( slash + 1 ) : path;

		if (( id.Len( ) == 0 ) || ( id[0] == '.' ))
			continue;

		// ScanDirectory RECURSES, so it also reports folders inside an entry. Only direct children of
		// the root are entries; anything deeper belongs to one.
		if ( path.Len( ) != rootWithSlash.Len( ) + id.Len( ))
			continue;

		const FString jsonPath = path + "/addon.json";

		std::string json;
		if ( !ReadWholeFile( jsonPath.GetChars( ), json ))
			continue;	// a folder with no addon.json is simply not an entry

		CatalogueEntry entry;
		entry.addon = ParseAddonFile( id.GetChars( ), json );
		entry.dir = path.GetChars( );
		entry.shipped = bShipped;
		entry.hasServerCfg = FileExists(( path + "/server.cfg" ).GetChars( ));

		if ( !entry.addon.valid )
		{
			// Named, not swallowed. One bad entry a player dropped in must not cost them the rest of
			// the catalogue, and it must not fail silently either.
			Printf( TEXTCOLOR_ORANGE "Catalogue: skipping '%s' -- %s\n" TEXTCOLOR_NORMAL,
				id.GetChars( ), entry.addon.error.c_str( ));
			continue;
		}

		// A later root replaces an earlier one with the same id.
		bool bReplaced = false;
		for ( size_t j = 0; j < out.size( ); ++j )
		{
			if ( out[j].addon.id == entry.addon.id )
			{
				out[j] = entry;
				bReplaced = true;
				break;
			}
		}

		if ( !bReplaced )
			out.push_back( entry );
	}
}

} // namespace

std::string CatalogueShippedDir( void )
{
	return std::string(( progdir + "catalogue" ).GetChars( ));
}

// [rc4l] The player's own catalogue, beside the download store rather than somewhere new: that
// directory is already writable on every platform and already falls back to progdir for a portable
// install, which is where a portable install expects its files. GetUserFile is unix-only, so it is
// not the answer here.
std::string CatalogueUserDir( void )
{
	FString dir = M_GetSavegamesPath( );
	if ( dir.IsEmpty( ))
		dir = progdir;

	if (( dir.Len( ) > 0 ) && ( dir[dir.Len( ) - 1] != '/' ) && ( dir[dir.Len( ) - 1] != '\\' ))
		dir += "/";

	dir += "catalogue";
	return std::string( dir.GetChars( ));
}

const std::vector<CatalogueEntry> &CatalogueLoad( bool bForceReload )
{
	if ( g_Loaded && !bForceReload )
		return g_Entries;

	g_Entries.clear( );

	const std::string shipped = CatalogueShippedDir( );
	const std::string user = CatalogueUserDir( );

	LoadRoot( shipped.c_str( ), true, g_Entries );

	// [rc4l] On a portable install M_GetSavegamesPath is empty, so the user directory falls back to
	// progdir and lands on the SAME folder. Reading it twice would re-file every shipped entry as the
	// player's own, which is what the first run of this actually did.
	if ( user != shipped )
		LoadRoot( user.c_str( ), false, g_Entries );

	g_Loaded = true;
	return g_Entries;
}

std::string CatalogueServerCfgPath( const CatalogueEntry &entry )
{
	if ( !entry.hasServerCfg )
		return std::string( );

	return entry.dir + "/server.cfg";
}

} // namespace zx

// [rc4l] Reading the catalogue back out, so a broken entry can be diagnosed without a debugger.
CCMD( fua_catalogue )
{
	const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( true );

	if ( entries.empty( ))
	{
		Printf( "No catalogue entries. Looked in:\n  %s\n  %s\n",
			zx::CatalogueShippedDir( ).c_str( ), zx::CatalogueUserDir( ).c_str( ));
		return;
	}

	Printf( "%d catalogue entries:\n", static_cast<int>( entries.size( )));
	for ( size_t i = 0; i < entries.size( ); ++i )
	{
		const zx::CatalogueEntry &e = entries[i];

		Printf( TEXTCOLOR_GOLD "%s" TEXTCOLOR_NORMAL " -- %s%s\n",
			e.addon.id.c_str( ), e.addon.name.c_str( ),
			e.shipped ? "" : " (yours)" );

		if ( !e.addon.summary.empty( ))
			Printf( "    %s\n", e.addon.summary.c_str( ));
		if ( !e.addon.iwad.empty( ))
			Printf( "    iwad: %s\n", e.addon.iwad.c_str( ));

		for ( size_t f = 0; f < e.addon.files.size( ); ++f )
		{
			Printf( "    file: %-34s %s\n",
				e.addon.files[f].name.c_str( ), e.addon.files[f].md5.c_str( ));
		}

		Printf( "    cfg:  %s\n", e.hasServerCfg ? zx::CatalogueServerCfgPath( e ).c_str( ) : "(none)" );
	}
}
