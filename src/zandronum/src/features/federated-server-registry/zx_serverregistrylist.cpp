// [rc4l] See zx_serverregistrylist.h. Fetch, cache, and merge the client's server registry list.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/federated-server-registry/zx_serverregistrylist.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <thread>

#include "c_cvars.h"
#include "cmdlib.h"     // CreatePath
#include "doomtype.h"   // Printf
#include "m_misc.h"     // M_GetCachePath
#include "zstring.h"
#include "features/net/zx_httpsget.h"

// [rc4l] Where the shipped list is fetched from, as "<host>/<path>". A fork points this at its own
// file and inherits nothing of ours -- which is the whole reason it is a CVAR and not a #define.
//
// Deliberately NOT the same hostname as any server registry itself: this one is HTTPS and wants to be
// behind a CDN, while a server registry is UDP and must never be proxied. Names one letter apart
// invite exactly the mix-up that fails silently.
CVAR( String, cl_fua_serverregistrylist_url, "registrylist.cantstopscrolling.net/serverregistries.txt",
      CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

// [rc4l] Off switch for the fetch. The compiled-in default and the player's own CVAR still work, so
// turning this off means "do not talk to the network about this", not "have no server registries".
CVAR( Bool, cl_fua_serverregistrylist_fetch, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

namespace zx
{

namespace
{

// [rc4l] The floor: what a fresh install queries before it has ever reached the network. Kept in sync
// with config/serverregistries.txt by hand -- there is exactly one line, and a build-time
// generator for one line would cost more than it saves.
const char *const kBuiltinList = "registry.cantstopscrolling.net    rc4l\n";

// 6 hours, matching the CDN's own refresh, so the list is never more than ~12h stale end to end.
const long kRefreshSeconds = 6 * 60 * 60;

// Big enough for a list far larger than we will ever ship; a body that overflows it is truncated,
// and a truncated body loses its last line rather than the whole file.
const int kMaxBodyBytes = 65536;

// Set by the worker, read and reported by the main thread on the next MaybeRefresh.
// -1 = nothing to report; 0 = updated; 1 = fetch failed; 2 = fetched but unusable.
std::atomic<int> g_lastOutcome{ -1 };
std::atomic<bool> g_busy{ false };

FString CachePath( void )
{
	FString path = M_GetCachePath( true );
	path += "/zandrox-serverregistries.txt";
	return path;
}

std::string ReadWholeFile( const char *path )
{
	std::string out;
	FILE *f = std::fopen( path, "rb" );
	if ( f == NULL )
		return out;

	char buf[4096];
	size_t n;
	while (( n = std::fread( buf, 1, sizeof buf, f )) > 0 )
		out.append( buf, n );
	std::fclose( f );
	return out;
}

// Age of the cache in seconds, or -1 when there is no cache at all.
long CacheAgeSeconds( const char *path )
{
	struct stat st;
	if ( stat( path, &st ) != 0 )
		return -1;

	const double age = std::difftime( std::time( NULL ), st.st_mtime );
	return ( age < 0 ) ? 0 : static_cast<long>( age );
}

// Write via a temp file and rename, so a crash or a full disk mid-write cannot leave a half-written
// list that parses to a few entries and looks legitimate.
bool WriteCacheAtomically( const FString &finalPath, const std::string &body )
{
	FString tmp = finalPath;
	tmp += ".tmp";

	FILE *f = std::fopen( tmp.GetChars( ), "wb" );
	if ( f == NULL )
		return false;

	const size_t written = body.empty( ) ? 0 : std::fwrite( body.data( ), 1, body.size( ), f );
	const bool ok = ( std::fclose( f ) == 0 ) && ( written == body.size( ) );
	if ( ok && std::rename( tmp.GetChars( ), finalPath.GetChars( )) == 0 )
		return true;

	std::remove( tmp.GetChars( ));
	return false;
}

// The worker. MUST NOT touch Printf, CVARs, or any engine global that is not thread-safe -- it
// records an outcome code and the main thread does the talking.
void FetchWorker( std::string host, std::string path, FString cachePath )
{
	char *body = new char[kMaxBodyBytes];
	const bool ok = HttpsGet( host.c_str( ), path.c_str( ), body, kMaxBodyBytes );

	int outcome = 1; // fetch failed
	if ( ok )
	{
		const std::string text( body );

		// The gate: a body that yields no entries is an error page, a challenge, or garbage. It never
		// replaces a cache that currently works.
		if ( ParseServerRegistryList( text ).empty( ))
			outcome = 2;
		else
			outcome = WriteCacheAtomically( cachePath, text ) ? 0 : 1;
	}

	delete[] body;
	g_lastOutcome.store( outcome, std::memory_order_release );
	g_busy.store( false, std::memory_order_release );
}

void ReportLastOutcome( void )
{
	const int outcome = g_lastOutcome.exchange( -1, std::memory_order_acq_rel );
	switch ( outcome )
	{
	case 0:
		Printf( "Server registry list updated.\n" );
		break;
	case 1:
		Printf( "Warning: couldn't refresh the server registry list -- using the cached one.\n" );
		break;
	case 2:
		// Worth a distinct message: this is what a CDN bot-challenge or a 404 looks like from here,
		// and "the response wasn't a list" points at the server side rather than the player's network.
		Printf( "Warning: the server registry list URL didn't return a list -- using the cached one.\n" );
		break;
	default:
		break;
	}
}

} // namespace

std::vector<ServerRegistryEntry> ServerRegistryList_Resolve( const char *userCSV )
{
	const std::vector<ServerRegistryEntry> user =
		ParseServerRegistryCSV( userCSV != NULL ? userCSV : "" );

	// [rc4l] The deliberate opt-out: fetching off AND a list of your own means we add nothing at all,
	// not even the built-in default. Someone running a private network has to be able to leave ours
	// behind entirely, or "federated" is just branding.
	//
	// It takes two steps on purpose. Emptying one CVAR by accident is easy, and if that alone stranded
	// a player with no server registries the symptom would be an empty browser with nothing to
	// explain it. Turning the fetch off is a deliberate act; a typo in a host name is not.
	if (( cl_fua_serverregistrylist_fetch == false ) && ( user.empty( ) == false ))
		return user;

	std::vector<ServerRegistryEntry> shipped = ParseServerRegistryList( ReadWholeFile( CachePath( ).GetChars( )));
	if ( shipped.empty( ))
		shipped = ParseServerRegistryList( kBuiltinList );

	return MergeServerRegistryLists( user, shipped );
}

void ServerRegistryList_MaybeRefresh( void )
{
	ReportLastOutcome( );

	if ( cl_fua_serverregistrylist_fetch == false )
		return;

	// One fetch at a time. Opening the browser twice in a row must not start two.
	if ( g_busy.load( std::memory_order_acquire ))
		return;

	const FString cachePath = CachePath( );
	CreatePath( M_GetCachePath( true ).GetChars( ));

	const long age = CacheAgeSeconds( cachePath.GetChars( ));
	if (( age >= 0 ) && ( age < kRefreshSeconds ))
		return;

	// Split "<host>/<path>". No slash means the whole string is a host and we ask for the root.
	std::string url = *cl_fua_serverregistrylist_url;
	if ( url.empty( ))
		return;

	const std::string::size_type slash = url.find( '/' );
	const std::string host = ( slash == std::string::npos ) ? url : url.substr( 0, slash );
	const std::string path = ( slash == std::string::npos ) ? std::string( "/" ) : url.substr( slash );
	if ( host.empty( ))
		return;

	g_busy.store( true, std::memory_order_release );
	std::thread( FetchWorker, host, path, cachePath ).detach( );
}

} // namespace zx
