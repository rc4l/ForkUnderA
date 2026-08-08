// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_wadsearch.h. Main thread only -- it reads and writes GameConfig.

#include <sys/types.h>
#include <sys/stat.h>

#include "cmdlib.h"
#include "doomtype.h"
#include "gameconfigfile.h"
#include "i_system.h"		// I_GetSteamPath
#include "zstring.h"

#include "zx_waddownload_lists.h"	// kKnownWadDirs, generated from waddirectories.txt

#include "features/wad-download/zx_wadsearch.h"

namespace
{

// dir + "/" + name, tolerating a directory that already ends in a separator.
FString JoinPath( const FString &dir, const char *name )
{
	FString path = dir;
	if ( path.IsNotEmpty( ))
	{
		const char last = path[path.Len( ) - 1];
		if (( last != '/' ) && ( last != '\\' ))
			path += "/";
	}
	path += name;
	return path;
}

bool SectionAlreadyHas( const char *section, const char *value )
{
	if ( !GameConfig->SetSection( section ))
		return false;

	const char *key, *existing;
	while ( GameConfig->NextInSection( key, existing ))
	{
		if (( stricmp( key, "Path" ) == 0 ) && ( stricmp( existing, value ) == 0 ))
			return true;
	}
	return false;
}

// Append `value` as a Path to `section` unless it is already there. Stored UNEXPANDED so the entry
// still reads as "$LOCALAPPDATA/doomseeker" in the ini and keeps working if the profile moves.
void AddPathOnce( const char *section, const char *value )
{
	if ( SectionAlreadyHas( section, value ))
		return;
	if ( !GameConfig->SetSection( section, true ))
		return;
	GameConfig->SetValueForKey( "Path", value, true );
}

} // namespace

namespace zx
{

void FindAllIwadsInEngineSearchPaths( const char *name, TArray<FString> &out )
{
	out.Clear( );

	if (( name == NULL ) || ( name[0] == '\0' ) || ( GameConfig == NULL ))
		return;

	// The config's IWAD list first, in its own order -- the same walk IdentifyVersion does.
	if ( GameConfig->SetSection( "IWADSearch.Directories" ))
	{
		const char *key, *value;
		while ( GameConfig->NextInSection( key, value ))
		{
			if ( stricmp( key, "Path" ) != 0 )
				continue;

			FString dir = NicePath( value );
			if ( dir.IsEmpty( ))
				continue;
			FixPathSeperator( dir );

			const FString candidate = JoinPath( dir, name );
			if ( DirEntryExists( candidate ))
				out.Push( candidate );
		}
	}

	// Then Steam. This is the one that actually matters in practice: it is where a bought copy of
	// Doom II lives, and nothing else in either search list points at it.
	TArray<FString> steam = I_GetSteamPath( );
	for ( unsigned i = 0; i < steam.Size( ); ++i )
	{
		FString dir = steam[i];
		FixPathSeperator( dir );

		const FString candidate = JoinPath( dir, name );
		if ( DirEntryExists( candidate ))
			out.Push( candidate );
	}
}

void FindAllFilesInEngineSearchPaths( const char *name, TArray<FString> &out )
{
	out.Clear( );

	if (( name == NULL ) || ( name[0] == '\0' ))
		return;

	// The name as given, which covers an absolute path and a file beside the exe.
	if ( DirEntryExists( name ))
		out.Push( FString( name ));

	if ( GameConfig == NULL )
		return;

	// Then the -file search path, which is where a download lands and where the spawned server will
	// look. Same list, same order, so this answer and the server's agree by construction.
	if ( GameConfig->SetSection( "FileSearch.Directories" ))
	{
		const char *key, *value;
		while ( GameConfig->NextInSection( key, value ))
		{
			if ( stricmp( key, "Path" ) != 0 )
				continue;

			FString dir = NicePath( value );
			if ( dir.IsEmpty( ))
				continue;
			FixPathSeperator( dir );

			const FString candidate = JoinPath( dir, name );
			if ( DirEntryExists( candidate ))
				out.Push( candidate );
		}
	}
}

FString FindFileInEngineSearchPaths( const char *name )
{
	TArray<FString> all;
	FindAllFilesInEngineSearchPaths( name, all );
	return ( all.Size( ) > 0 ) ? all[0] : FString( );
}

unsigned long long FileSizeOnDisk( const char *path )
{
	if (( path == NULL ) || ( path[0] == 0 ))
		return 0;

	// 64-bit, because a resource pack can exceed what the 32-bit stat reports.
#ifdef _WIN32
	struct _stat64 info;
	if ( _stat64( path, &info ) != 0 )
		return 0;
#else
	struct stat info;
	if ( stat( path, &info ) != 0 )
		return 0;
#endif

	if ( info.st_size < 0 )
		return 0;

	return static_cast<unsigned long long>( info.st_size );
}

FString FindIwadInEngineSearchPaths( const char *name )
{
	TArray<FString> all;
	FindAllIwadsInEngineSearchPaths( name, all );
	return ( all.Size( ) > 0 ) ? all[0] : FString( );
}

void RegisterKnownWadDirectories( void )
{
	if ( GameConfig == NULL )
		return;

	for ( int i = 0; i < kKnownWadDirCount; ++i )
	{
		// Only register what exists. An entry for a tool the player does not have would otherwise sit
		// in their ini forever being checked and missing on every single lookup.
		FString resolved = NicePath( kKnownWadDirs[i] );
		if ( resolved.IsEmpty( ))
			continue;
		FixPathSeperator( resolved );
		if ( !DirEntryExists( resolved ))
			continue;

		AddPathOnce( "FileSearch.Directories", kKnownWadDirs[i] );
		AddPathOnce( "IWADSearch.Directories", kKnownWadDirs[i] );
	}
}

} // namespace zx
