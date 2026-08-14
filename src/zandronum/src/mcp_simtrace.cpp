//
// mcp_simtrace.cpp -- sim event tracer for the native MCP bridge. See mcp_simtrace.h.
//
// Trace lines are plain text, one event per line, stable across runs of a deterministic sim:
//   <tic> D <targetclass> <x> <y> <z> <damage> <inflictorclass>
//   <tic> K <victimclass> <x> <y> <z> <sourceclass>
//   <tic> S <class> <x> <y> <z>
// Coordinates are whole map units. The first differing line between two runs is the event
// that shifted.
//

#ifdef FUA_MCP_BRIDGE

#include "mcp_simtrace.h"
#include "actor.h"
#include "doomstat.h"
#include "g_level.h"
#include <stdio.h>
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

bool MCP_SimTrace_ReportReady( std::string &json )
{
	if ( !g_reportReady )
		return false;
	json = g_report;
	g_reportReady = false;
	return true;
}

#endif // FUA_MCP_BRIDGE
