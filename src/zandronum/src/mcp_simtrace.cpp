//
// mcp_simtrace.cpp -- sim event tracer for the native MCP bridge. See mcp_simtrace.h.
//
// Trace lines are plain text, one event per line, stable across runs of a deterministic sim:
//   <tic> D <targetclass> <x> <y> <z> <damage> <inflictorclass>
//   <tic> K <victimclass> <x> <y> <z> <sourceclass>
//   <tic> S <class> <x> <y> <z>
//   <tic> H <hash>                     the sim's fingerprint once the tic has run (MCP_StateHash, world scope)
// Coordinates are whole map units. The first differing H line names the tic two runs diverged at,
// and the first differing event line the event that shifted.
//

#ifdef FUA_MCP_BRIDGE

#include "mcp_simtrace.h"
#include "actor.h"
#include "doomstat.h"
#include "g_level.h"
#include "c_dispatch.h"
#include "dthinker.h"
#include "m_random.h"
#include "r_defs.h"
#include "r_state.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

namespace
{
	bool g_armed;
	int g_ticsLeft;
	std::string g_path;
	std::vector<std::string> g_lines;
	std::string g_report;
	bool g_reportReady;

	void Append( const char *kind, AActor *a, AActor *other, int damage, bool withDamage )
	{
		if ( !g_armed || a == NULL )
			return;
		char buf[256];
		const char *acls = a->GetClass() ? a->GetClass()->TypeName.GetChars() : "?";
		const char *ocls = ( other != NULL && other->GetClass() ) ? other->GetClass()->TypeName.GetChars() : "-";
		if ( withDamage )
			snprintf( buf, sizeof buf, "%d %s %s %d %d %d %d %s", level.time, kind, acls,
				a->x >> FRACBITS, a->y >> FRACBITS, a->z >> FRACBITS, damage, ocls );
		else
			snprintf( buf, sizeof buf, "%d %s %s %d %d %d %s", level.time, kind, acls,
				a->x >> FRACBITS, a->y >> FRACBITS, a->z >> FRACBITS, ocls );
		g_lines.push_back( buf );
	}
}

unsigned long long MCP_StateHash( bool withRng )
{
	using namespace zx::mcp;
	uint64_t h = FnvInit();
	h = FnvMixU64( h, (uint64_t)level.time );
	if ( withRng )
		h = FnvMixU64( h, (uint64_t)FRandom::StaticSumSeeds() );
	if ( gamestate != GS_LEVEL )
		return h;
	const PClass *lightcls = PClass::FindClass( "DynamicLight" );
	TThinkerIterator<AActor> it;
	AActor *mo;
	while ( ( mo = it.Next() ) != NULL )
	{
		if ( lightcls != NULL && mo->IsKindOf( lightcls ) )
			continue;
		h = FnvMixU64( h, (uint64_t)mo->x );
		h = FnvMixU64( h, (uint64_t)mo->y );
		h = FnvMixU64( h, (uint64_t)mo->z );
		h = FnvMixU64( h, (uint64_t)mo->angle );
		h = FnvMixU64( h, (uint64_t)mo->health );
		h = FnvMixU64( h, (uint64_t)mo->velx );
		h = FnvMixU64( h, (uint64_t)mo->vely );
		h = FnvMixU64( h, (uint64_t)mo->velz );
		h = FnvMixU64( h, (uint64_t)mo->tics );
		h = FnvMixU64( h, (uint64_t)mo->sprite );
		h = FnvMixU64( h, (uint64_t)mo->frame );
	}
	for ( int i = 0; i < numsectors; i++ )
	{
		h = FnvMixU64( h, (uint64_t)sectors[i].floorplane.d );
		h = FnvMixU64( h, (uint64_t)sectors[i].ceilingplane.d );
		h = FnvMixU64( h, (uint64_t)sectors[i].lightlevel );
	}
	return h;
}

void MCP_SimTrace_Arm( int tics, const char *path )
{
	g_ticsLeft = tics < 1 ? 1 : tics;
	g_path = path ? path : "";
	g_lines.clear();
	g_reportReady = false;
	g_armed = true;
}

void MCP_SimTrace_Damage( AActor *target, AActor *inflictor, int damage )
{
	Append( "D", target, inflictor, damage, true );
}

void MCP_SimTrace_Kill( AActor *victim, AActor *source )
{
	Append( "K", victim, source, 0, false );
}

void MCP_SimTrace_Spawn( AActor *spawned )
{
	Append( "S", spawned, NULL, 0, false );
}

void MCP_SimTrace_TicDone()
{
	if ( !g_armed )
		return;
	// Only count tics where the sim actually advanced -- gametic keeps running
	// while paused (menus, stepped-mode gaps) and must not burn the window.
	static int lastLevelTime = -1;
	if ( level.time == lastLevelTime )
		return;
	lastLevelTime = level.time;
	char hash[64];
	snprintf( hash, sizeof hash, "%d H %llu", level.time, MCP_StateHash( false ) );
	g_lines.push_back( hash );
	if ( --g_ticsLeft > 0 )
		return;

	g_armed = false;
	unsigned int written = 0;
	FILE *f = g_path.empty() ? NULL : fopen( g_path.c_str(), "w" );
	if ( f != NULL )
	{
		for ( size_t i = 0; i < g_lines.size(); ++i )
		{
			fputs( g_lines[i].c_str(), f );
			fputc( '\n', f );
		}
		written = (unsigned int)g_lines.size();
		fclose( f );
	}
	char buf[512];
	snprintf( buf, sizeof buf, "{\"events\":%u,\"path\":\"%s\",\"written\":%s}",
		(unsigned int)g_lines.size(), g_path.c_str(), f != NULL ? "true" : "false" );
	g_report = buf;
	g_reportReady = true;
	g_lines.clear();
}

// [rc4l] Arms the trace from the console or the command line, so a run started with +fua_simtrace is traced from its first tic.
CCMD( fua_simtrace )
{
	if ( argv.argc() < 3 )
	{
		Printf( "fua_simtrace <tics> <file>: every damage, kill and spawn and each tic's fingerprint for that many tics\n" );
		return;
	}
	MCP_SimTrace_Arm( atoi( argv[1] ), argv[2] );
}

bool MCP_SimTrace_ReportReady( std::string &json )
{
	if ( !g_reportReady )
		return false;
	json = g_report;
	g_reportReady = false;
	return true;
}

#endif // FUA_MCP_BRIDGE
