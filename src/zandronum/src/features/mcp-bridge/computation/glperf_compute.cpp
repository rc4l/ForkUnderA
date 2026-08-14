// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Implementation of the pure GPU-profiler core. See glperf_compute.h. No engine, GL, or socket
// types here -- only <string>/<vector>/<cstdint> and the sibling pure perf-summary helpers.

#include "glperf_compute.h"
#include "mcprpc_compute.h" // SummarizeFrameTimes / PerfSummaryJson -- shared, pure, already tested

namespace zx { namespace mcp {

const char *GLZoneName( int zone )
{
	switch ( zone )
	{
	case GLZONE_SCENE:       return "scene";
	case GLZONE_TRANSLUCENT: return "translucent";
	case GLZONE_PORTALS:     return "portals";
	case GLZONE_POSTPROCESS: return "postprocess";
	case GLZONE_HUD2D:       return "hud2d";
	default:                 return "?";
	}
}

int GLRingSlot( uint64_t frameIndex, int ringSize )
{
	if ( ringSize <= 0 ) ringSize = 1;
	return (int)( frameIndex % (uint64_t)ringSize );
}

bool GLRingReady( uint64_t frameIndex, int ringSize )
{
	if ( ringSize <= 0 ) ringSize = 1;
	return frameIndex >= (uint64_t)ringSize;
}

double GLNanosToMs( uint64_t ns )
{
	return (double)ns * 1e-6;
}

bool GLSpanValid( uint64_t beginNs, uint64_t endNs )
{
	if ( beginNs == 0 || endNs == 0 ) return false; // a zero endpoint = marker never issued this frame
	return endNs >= beginNs;                         // reversed = wrapped/garbage counter
}

double GLSpanMs( uint64_t beginNs, uint64_t endNs )
{
	if ( !GLSpanValid( beginNs, endNs ) ) return 0.0;
	return GLNanosToMs( endNs - beginNs );
}

GLTimingVerdict GLAssessTiming( const std::vector<double> &totalsMs )
{
	if ( totalsMs.empty() )
		return { false, "no frames captured" };

	for ( double ms : totalsMs )
	{
		if ( ms > 0.0 )
			return { true, std::string() }; // at least one real measurement -> timers work
	}
	return { false, "gpu timer queries returned zero -- this driver likely does not support GL_TIMESTAMP" };
}

std::string GLTimersJson( const GLTimingVerdict &verdict,
                          const std::vector<std::vector<double>> &perZoneMs,
                          const std::vector<double> &totalMs,
                          const std::string &countersJson )
{
	std::string out = "{\"available\":";
	out += verdict.available ? "true" : "false";
	out += ",\"frames\":" + std::to_string( (long long)totalMs.size() );

	if ( !verdict.note.empty() )
	{
		std::string esc;
		JsonEscape( verdict.note, esc );
		out += ",\"note\":\"" + esc + "\"";
	}

	// Whole-frame GPU time.
	out += ",\"total\":" + PerfSummaryJson( SummarizeFrameTimes( totalMs ) );

	// Per-zone breakdown; a zone with no samples is skipped so absent passes don't masquerade as 0ms.
	out += ",\"zones\":{";
	bool first = true;
	for ( int z = 0; z < GLZONE_COUNT; ++z )
	{
		if ( (size_t)z >= perZoneMs.size() || perZoneMs[z].empty() ) continue;
		if ( !first ) out += ",";
		first = false;
		out += "\"";
		out += GLZoneName( z );
		out += "\":" + PerfSummaryJson( SummarizeFrameTimes( perZoneMs[z] ) );
	}
	out += "}";

	if ( !countersJson.empty() )
		out += ",\"counters\":" + countersJson;

	out += "}";
	return out;
}

}} // namespace zx::mcp
