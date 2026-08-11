// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/zx_catalogue.h"

#include "cmdlib.h"
#include "c_dispatch.h"
#include "d_main.h"
#include "doomtype.h"
#include "m_misc.h"
#include "templates.h"
#include "v_text.h"
#include "version.h"

#include "features/addon-catalogue/computation/hostplan_compute.h"
#include "features/addon-catalogue/computation/iwadpick_compute.h"
#include "features/addon-catalogue/computation/variantpick_compute.h"
#include "features/server-hosting/zx_hosting.h"
#include "features/server-hosting/zx_reachprobe.h"
#include "features/wad-download/zx_wadsearch.h"

#include <stdio.h>
#include <algorithm>

namespace zx
{

namespace
{

std::vector<CatalogueEntry> g_Entries;

// [rc4l] Read in the same pass as the entries, so an entry and the remixes it names cannot end up
// one reload apart.
std::vector<AddonRemix> g_Remixes;

bool g_Loaded = false;

// How many entries were thrown out on the last read, so the summary at startup can say there were
// any at all. A player who dropped a folder in wants to know it did not take, and a line among the
// hundreds the engine prints while loading is easy to scroll past.
int g_Problems = 0;

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

		// [rc4l] The cfgs an entry PROMISES have to be there, and this is the only moment anybody
		// looks. The json names them; nothing else checks; and a variant naming a cfg that was never
		// shipped fails at the worst possible time -- when a player presses the button to host it --
		// with the server starting on whatever the engine's defaults happen to be. So the promise is
		// checked where it is made.
		//
		// Existence and readability only. A cfg is a list of console commands, and the engine's own
		// exec is what decides whether a line means anything, so claiming to have validated the
		// CONTENTS here would be a lie.
		if ( entry.addon.valid )
		{
			if ( !entry.hasServerCfg )
			{
				entry.addon.valid = false;
				entry.addon.error = "no server.cfg beside addon.json";
			}
			else
			{
				for ( size_t v = 0; v < entry.addon.variants.size( ); ++v )
				{
					const FString cfgPath = path + "/" + entry.addon.variants[v].cfg.c_str( );
					if ( FileExists( cfgPath.GetChars( )))
						continue;

					entry.addon.valid = false;
					entry.addon.error = "the experience variant '" + entry.addon.variants[v].name +
						"' names " + entry.addon.variants[v].cfg + ", which is not in the folder";
					break;
				}
			}
		}

		if ( !entry.addon.valid )
		{
			// Named, not swallowed. One bad entry a player dropped in must not cost them the rest of
			// the catalogue, and it must not fail silently either.
			Printf( TEXTCOLOR_RED "Catalogue: skipping '%s' -- %s\n" TEXTCOLOR_NORMAL,
				id.GetChars( ), entry.addon.error.c_str( ));
			++g_Problems;
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

// [rc4l] The remix pool under one root: remix/<id>/remix.json.
//
// Inside the catalogue folder rather than beside it, because it IS catalogue data and travels with
// it. The entry scan is unbothered by it for free: `remix` has no addon.json so it is not an entry,
// and everything under it is too deep for the direct-children rule.
void LoadRemixRoot( const char *root, std::vector<AddonRemix> &out )
{
	FString dir = root;
	FixPathSeperator( dir );
	if (( dir.Len( ) > 0 ) && ( dir[dir.Len( ) - 1] != '/' ))
		dir += "/";
	dir += "remix/";

	if ( !DirEntryExists( dir.GetChars( )))
		return;

	TArray<FFileList> found;
	ScanDirectory( found, dir );

	for ( unsigned int i = 0; i < found.Size( ); ++i )
	{
		if ( !found[i].isDirectory )
			continue;

		FString path = found[i].Filename;
		FixPathSeperator( path );
		while (( path.Len( ) > 0 ) && ( path[path.Len( ) - 1] == '/' ))
			path.Truncate( path.Len( ) - 1 );

		const long slash = path.LastIndexOf( '/' );
		const FString id = ( slash >= 0 ) ? path.Mid( slash + 1 ) : path;

		if (( id.Len( ) == 0 ) || ( id[0] == '.' ))
			continue;
		if ( path.Len( ) != dir.Len( ) + id.Len( ))
			continue;			// ScanDirectory recurses; only direct children are remixes

		std::string json;
		if ( !ReadWholeFile(( path + "/remix.json" ).GetChars( ), json ))
			continue;

		AddonRemix remix = ParseRemixFile( id.GetChars( ), json );

		// Same promise, same moment: a remix naming a cfg nobody shipped would fail when somebody
		// picks it, which is the worst time to find out.
		if ( remix.valid && !remix.cfg.empty( ) &&
			!FileExists(( path + "/" + remix.cfg.c_str( )).GetChars( )))
		{
			remix.valid = false;
			remix.error = "names " + remix.cfg + ", which is not in the folder";
		}

		if ( !remix.valid )
		{
			Printf( TEXTCOLOR_RED "Catalogue: skipping remix '%s' -- %s\n" TEXTCOLOR_NORMAL,
				id.GetChars( ), remix.error.c_str( ));
			++g_Problems;
			continue;
		}

		bool bReplaced = false;
		for ( size_t j = 0; j < out.size( ); ++j )
		{
			if ( out[j].id == remix.id )
			{
				out[j] = remix;
				bReplaced = true;
				break;
			}
		}

		if ( !bReplaced )
			out.push_back( remix );
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
	g_Problems = 0;

	const std::string shipped = CatalogueShippedDir( );
	const std::string user = CatalogueUserDir( );

	LoadRoot( shipped.c_str( ), true, g_Entries );

	// [rc4l] On a portable install M_GetSavegamesPath is empty, so the user directory falls back to
	// progdir and lands on the SAME folder. Reading it twice would re-file every shipped entry as the
	// player's own, which is what the first run of this actually did.
	if ( user != shipped )
		LoadRoot( user.c_str( ), false, g_Entries );

	// [rc4l] Curated entries float to the top. STABLE, so everything at the default 0 keeps the folder
	// order it has always had rather than being reshuffled by a field it never sets.
	std::stable_sort( g_Entries.begin( ), g_Entries.end( ),
		[]( const CatalogueEntry &a, const CatalogueEntry &b ) {
			return a.addon.order > b.addon.order;
		} );

	// The pool is read in the same pass, so an entry and the remixes it names can never be one reload
	// out of step with each other.
	g_Remixes.clear( );
	LoadRemixRoot( shipped.c_str( ), g_Remixes );
	if ( user != shipped )
		LoadRemixRoot( user.c_str( ), g_Remixes );

	g_Loaded = true;
	return g_Entries;
}

const std::vector<AddonRemix> &CatalogueRemixes( bool bForceReload )
{
	CatalogueLoad( bForceReload );		// fills both; see above
	return g_Remixes;
}

std::string CatalogueRemixCfgPath( const std::string &remixId )
{
	const std::vector<AddonRemix> &pool = CatalogueRemixes( );

	for ( size_t i = 0; i < pool.size( ); ++i )
	{
		if (( pool[i].id != remixId ) || pool[i].cfg.empty( ))
			continue;

		// Rebuilt from the roots rather than remembered per remix, the same way an entry's cfg path
		// is: the shipped copy and the player's can hold the same id, and the one that wins is the
		// one the loader kept.
		const std::string user = CatalogueUserDir( );
		const std::string shipped = CatalogueShippedDir( );

		std::string at = user + "/remix/" + remixId + "/" + pool[i].cfg;
		if ( FileExists( at.c_str( )))
			return at;

		return shipped + "/remix/" + remixId + "/" + pool[i].cfg;
	}

	return std::string( );
}

//*****************************************************************************
//
// [rc4l] Read the catalogue AT STARTUP, so a broken entry is reported while the player is still
// looking at the console rather than the first time they open the host screen.
//
// It used to be read lazily, which meant a folder somebody had just added could be silently absent
// for as long as they did not go looking for it -- and when they did, the reason scrolled past in a
// list they had no reason to be reading. Nothing here is expensive: a handful of small files.
void CatalogueCheckAtStartup( void )
{
	const std::vector<CatalogueEntry> &entries = CatalogueLoad( );

	if ( g_Problems <= 0 )
		return;

	// Said again, after the individual reasons, because the reasons are printed one per entry among
	// everything else the engine says while it starts.
	Printf( TEXTCOLOR_RED "Catalogue: %d entr%s could not be used and %s been skipped. "
		"See the lines above, or type fua_catalogue.\n" TEXTCOLOR_NORMAL,
		g_Problems, ( g_Problems == 1 ) ? "y" : "ies", ( g_Problems == 1 ) ? "has" : "have" );

	Printf( "Catalogue: %d usable entr%s.\n",
		static_cast<int>( entries.size( )), ( entries.size( ) == 1 ) ? "y" : "ies" );
}

std::string CatalogueServerCfgPath( const CatalogueEntry &entry, const std::string &variantId )
{
	// [rc4l] Which cfg, not whether there is one. PickVariant answers with server.cfg for an entry
	// that has no variants, so the plain case comes out exactly as it did before this existed.
	const VariantPick pick = PickVariant( entry.addon, variantId );

	// hasServerCfg is still about server.cfg specifically, because that is the file the scan looked
	// for and the one an entry is required to have. A variant's cfg sits beside it in the same
	// folder, so an entry with no server.cfg has no variants worth running either.
	if ( !entry.hasServerCfg )
		return std::string( );

	return entry.dir + "/" + pick.cfg;
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

	const std::vector<zx::AddonRemix> &remixes = zx::CatalogueRemixes( );

	if ( !remixes.empty( ))
	{
		Printf( "%d remix%s:\n", static_cast<int>( remixes.size( )),
			( remixes.size( ) == 1 ) ? "" : "es" );

		for ( size_t i = 0; i < remixes.size( ); ++i )
		{
			Printf( TEXTCOLOR_GOLD "    %s" TEXTCOLOR_NORMAL " -- %s\n",
				remixes[i].id.c_str( ), remixes[i].name.c_str( ));

			if ( !remixes[i].cfg.empty( ))
				Printf( "      cfg:  %s\n", zx::CatalogueRemixCfgPath( remixes[i].id ).c_str( ));

			for ( size_t f = 0; f < remixes[i].files.size( ); ++f )
			{
				Printf( "      file: %-32s %s\n",
					remixes[i].files[f].name.c_str( ), remixes[i].files[f].md5.c_str( ));
			}
		}
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

		if ( !e.addon.remixes.empty( ))
		{
			FString offers;
			for ( size_t m = 0; m < e.addon.remixes.size( ); ++m )
			{
				if ( m > 0 )
					offers += ", ";
				offers += e.addon.remixes[m].c_str( );
			}
			Printf( "    plays with: %s\n", offers.GetChars( ));
		}
		if ( !e.addon.iwad.empty( ))
			Printf( "    iwad: %s\n", e.addon.iwad.c_str( ));

		for ( size_t f = 0; f < e.addon.files.size( ); ++f )
		{
			Printf( "    file: %-34s %s\n",
				e.addon.files[f].name.c_str( ), e.addon.files[f].md5.c_str( ));
		}

		// [rc4l] The ways of playing, each with the cfg and the files peculiar to it. Listing only the
		// entry's own used to be the whole story; for a pack whose ways of playing share no base it
		// prints an experience that appears to load nothing at all.
		if ( e.addon.variants.empty( ))
		{
			Printf( "    cfg:  %s\n",
				e.hasServerCfg ? zx::CatalogueServerCfgPath( e ).c_str( ) : "(none)" );
			continue;
		}

		for ( size_t v = 0; v < e.addon.variants.size( ); ++v )
		{
			const zx::AddonVariant &variant = e.addon.variants[v];

			Printf( "    %s%s [%s]\n", variant.name.c_str( ),
				variant.isDefault ? " (default)" : "",
				zx::DescribeVariantKind( variant.kind ));

			for ( size_t f = 0; f < variant.files.size( ); ++f )
			{
				Printf( "      file: %-32s %s\n",
					variant.files[f].name.c_str( ), variant.files[f].md5.c_str( ));
			}

			Printf( "      cfg:  %s\n", zx::CatalogueServerCfgPath( e, variant.id ).c_str( ));

			if ( !variant.remixes.empty( ))
			{
				FString offers;
				for ( size_t m = 0; m < variant.remixes.size( ); ++m )
				{
					if ( m > 0 )
						offers += ", ";
					offers += variant.remixes[m].c_str( );
				}
				Printf( "      plays with: %s\n", offers.GetChars( ));
			}
		}
	}
}

// [rc4l] Hosting a catalogue entry from the console. The whole chain in one place -- read the entry,
// pick an IWAD, plan, start -- so it can be exercised before any of it has a menu, and so a player
// on a headless box has a way in that never depended on one.
CCMD( fua_host )
{
	if ( argv.argc( ) < 2 )
	{
		Printf( "fua_host <id> [variant]: host a catalogue entry. fua_catalogue lists them.\n" );
		return;
	}

	const std::vector<zx::CatalogueEntry> &entries = zx::CatalogueLoad( );

	const zx::CatalogueEntry *chosen = NULL;
	for ( size_t i = 0; i < entries.size( ); ++i )
	{
		if ( stricmp( entries[i].addon.id.c_str( ), argv[1] ) == 0 )
		{
			chosen = &entries[i];
			break;
		}
	}

	if ( chosen == NULL )
	{
		Printf( TEXTCOLOR_ORANGE "No catalogue entry called '%s'. Try fua_catalogue.\n" TEXTCOLOR_NORMAL,
			argv[1] );
		return;
	}

	// Which way of playing, and so what actually loads. An entry whose variants carry their own wads
	// has no single file list, so everything below asks the pick rather than the entry.
	const std::string variantId = ( argv.argc( ) >= 3 ) ? argv[2] : std::string( );
	const zx::VariantPick variant = zx::PickVariant( chosen->addon, variantId );

	// What the player already has, in the two places a file can be: the ordinary search path and the
	// by-hash store the downloader fills. Asking both is what stops us fetching something twice.
	std::vector<std::string> have;
	for ( size_t i = 0; i < variant.files.size( ); ++i )
	{
		const std::string &name = variant.files[i].name;

		TArray<FString> resolved;
		if ( D_AddFile( resolved, name.c_str( ), false ) && ( resolved.Size( ) > 0 ))
			have.push_back( name );
	}

	// Every IWAD the engine can actually see, which is a wider list than the working directory:
	// Steam libraries and the launcher's own folders count.
	std::vector<std::string> iwads;
	{
		const char *const kCandidates[] = {
			"doom2.wad", "doom.wad", "freedoom2.wad", "freedoom1.wad", "freedm.wad",
			"tnt.wad", "plutonia.wad", "heretic.wad", "hexen.wad", "strife1.wad",
		};

		for ( size_t i = 0; i < sizeof( kCandidates ) / sizeof( kCandidates[0] ); ++i )
		{
			if ( zx::FindIwadInEngineSearchPaths( kCandidates[i] ).IsNotEmpty( ))
				iwads.push_back( kCandidates[i] );
		}
	}

	const zx::IwadPick pick = zx::PickIwad( chosen->addon.iwad, iwads );

	zx::HostChoices choices;
	choices.serverName = zx::ComposeServerName( chosen->addon.name, variant.name, FUA_NAME );
	choices.maxPlayers = 8;
	choices.port = 0;
	choices.advertise = false;

	// [rc4l] No remixes from the console. fua_host names an entry and a variant and nothing else, so
	// there is no axis to resolve; the panel is where a remix gets chosen.
	const zx::HostPlan plan = zx::BuildHostPlan( chosen->addon, variant.files, pick,
		zx::CatalogueServerCfgPath( *chosen, variantId ), std::vector<std::string>( ), variant.map,
		choices, have );

	if ( !plan.blocker.empty( ))
	{
		Printf( TEXTCOLOR_ORANGE "Cannot host %s: %s\n" TEXTCOLOR_NORMAL,
			chosen->addon.name.c_str( ), plan.blocker.c_str( ));
		return;
	}

	if ( pick.choice == zx::IwadChoice::Substitute )
	{
		Printf( TEXTCOLOR_GOLD "%s wants %s, which you do not have. Hosting on %s instead.\n"
			TEXTCOLOR_NORMAL, chosen->addon.name.c_str( ), pick.wanted.c_str( ), pick.iwad.c_str( ));
	}

	if ( !plan.ready )
	{
		Printf( TEXTCOLOR_ORANGE "Not hosting yet -- %d file(s) still to fetch:\n" TEXTCOLOR_NORMAL,
			static_cast<int>( plan.missing.size( )));
		for ( size_t i = 0; i < plan.missing.size( ); ++i )
			Printf( "    %s\n", plan.missing[i].c_str( ));
		return;
	}

	zx::HostConfig config;
	config.hostName = plan.serverName;
	config.iwad = plan.iwad;
	config.pwads = plan.pwads;
	config.execCfg = plan.execCfg;
	config.execRemixCfgs = plan.execRemixCfgs;
	config.maxPlayers = plan.maxPlayers;
	config.port = plan.port;
	config.advertise = plan.advertise;
	config.serveWads = true;

	// [rc4l] Same rule as the menu: the entry's own opening map first, then the cfg's rotation, then
	// map01 only when there is neither.
	config.map = plan.map;
	if ( config.map.empty( ) && plan.execCfg.empty( ))
		config.map = "map01";

	Printf( "Hosting " TEXTCOLOR_GOLD "%s" TEXTCOLOR_NORMAL " on %s\n",
		chosen->addon.name.c_str( ), config.iwad.c_str( ));
	for ( size_t i = 0; i < config.pwads.size( ); ++i )
		Printf( "    -file %s\n", config.pwads[i].c_str( ));
	if ( !config.execCfg.empty( ))
		Printf( "    +exec %s\n", config.execCfg.c_str( ));

	zx::ReachProbeRelease( );

	if ( zx::HostStart( config ) == false )
		Printf( TEXTCOLOR_ORANGE "The server did not start.\n" TEXTCOLOR_NORMAL );
}

