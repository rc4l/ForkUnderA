//
// mcp_ticprof.cpp -- per-tic sim profiler for the native MCP bridge. See mcp_ticprof.h.
//
// [rc4l] Dormant until armed, and then it reads the sim zones' totals once a tic; ticprof_compute does the arithmetic.
//

#ifdef FUA_MCP_BRIDGE

#include "mcp_ticprof.h"
#include "features/mcp-bridge/computation/ticprof_compute.h"
#include "features/telemetry/telemetry.h"
#include <vector>

namespace
{
	bool g_armed;
	int g_wantTics;
	zx::telemetry::Counter g_zones[zx::mcp::kTicZoneCount];
	int64_t g_last[zx::mcp::kTicZoneCount];
	std::vector<zx::mcp::TicPhases> g_tics;
	std::string g_report;
	bool g_reportReady;

	// [rc4l] Each zone's nanosecond total now, found by name the first time it has run.
	void ReadZones( int64_t *out )
	{
		for ( int z = 0; z < zx::mcp::kTicZoneCount; ++z )
		{
			if ( g_zones[z] < 0 )
				g_zones[z] = zx::telemetry::Find( zx::mcp::kTicZones[z] );
			out[z] = zx::telemetry::TimedNs( g_zones[z] );
		}
	}
}

void MCP_TicProf_Arm( int tics )
{
	g_wantTics = tics < 1 ? 1 : ( tics > 2000 ? 2000 : tics );
	g_tics.clear();
	for ( int z = 0; z < zx::mcp::kTicZoneCount; ++z )
		g_zones[z] = -1;
	ReadZones( g_last );
	g_reportReady = false;
	g_armed = true;
}

void MCP_TicProf_TicDone()
{
	if ( !g_armed )
		return;

	int64_t now[zx::mcp::kTicZoneCount];
	ReadZones( now );
	g_tics.push_back( zx::mcp::ComputeTicPhases( g_last, now ));
	for ( int z = 0; z < zx::mcp::kTicZoneCount; ++z )
		g_last[z] = now[z];

	if ( (int)g_tics.size() < g_wantTics )
		return;

	g_armed = false;
	g_report = zx::mcp::ComputeTicProfJson( g_tics );
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
