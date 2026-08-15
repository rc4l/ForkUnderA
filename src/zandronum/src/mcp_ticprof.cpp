//
// mcp_ticprof.cpp -- per-tic sim profiler for the native MCP bridge. See mcp_ticprof.h.
//
// Dormant until armed: the anchors cost one bool test per call when no capture is running.
//

#ifdef FUA_MCP_BRIDGE

#include "mcp_ticprof.h"
#include <chrono>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	bool g_armed;
	int g_wantTics;
	Clock::time_point g_zoneStart[MCP_TPZ_COUNT];
	double g_zoneMs[MCP_TPZ_COUNT];

	struct TicRecord { double ms[MCP_TPZ_COUNT]; };
	std::vector<TicRecord> g_tics;
	std::string g_report;
	bool g_reportReady;
}

void MCP_TicProf_Arm( int tics )
{
	g_wantTics = tics < 1 ? 1 : ( tics > 2000 ? 2000 : tics );
	g_tics.clear();
	for ( int z = 0; z < MCP_TPZ_COUNT; ++z )
		g_zoneMs[z] = 0.0;
	g_reportReady = false;
	g_armed = true;
}

void MCP_TicProf_Begin( int zone )
{
	if ( !g_armed || zone < 0 || zone >= MCP_TPZ_COUNT )
		return;
	g_zoneStart[zone] = Clock::now();
}

void MCP_TicProf_End( int zone )
{
	if ( !g_armed || zone < 0 || zone >= MCP_TPZ_COUNT )
		return;
	g_zoneMs[zone] += std::chrono::duration<double, std::milli>( Clock::now() - g_zoneStart[zone] ).count();
}

void MCP_TicProf_TicDone()
{
	if ( !g_armed )
		return;

	TicRecord r;
	for ( int z = 0; z < MCP_TPZ_COUNT; ++z )
	{
		r.ms[z] = g_zoneMs[z];
		g_zoneMs[z] = 0.0;
	}
	g_tics.push_back( r );

	if ( (int)g_tics.size() < g_wantTics )
		return;

	g_armed = false;
	std::string out = "{\"tics\":[";
	for ( size_t i = 0; i < g_tics.size(); ++i )
	{
		if ( i ) out += ",";
		out += "{\"total\":" + std::to_string( g_tics[i].ms[MCP_TPZ_GTICKER] );
		out += ",\"pticker\":" + std::to_string( g_tics[i].ms[MCP_TPZ_PTICKER] );
		out += ",\"thinkers\":" + std::to_string( g_tics[i].ms[MCP_TPZ_THINKERS] );
		out += ",\"effects\":" + std::to_string( g_tics[i].ms[MCP_TPZ_EFFECTS] );
		out += ",\"specials\":" + std::to_string( g_tics[i].ms[MCP_TPZ_SPECIALS] ) + "}";
	}
	out += "]}";
	g_report = out;
	g_reportReady = true;
	g_tics.clear();
}

bool MCP_TicProf_ReportReady( std::string &json )
{
	if ( !g_reportReady )
		return false;
	json = g_report;
	g_reportReady = false;
	return true;
}

#endif // FUA_MCP_BRIDGE
