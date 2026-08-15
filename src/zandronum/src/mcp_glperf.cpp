//
// mcp_glperf.cpp -- GPU render profiler backend for the native MCP bridge. Issues GL_TIMESTAMP timer
// queries around the engine's render passes and reads them back through a multi-frame query ring so
// the CPU never stalls on the GPU pipeline. All the arithmetic (ring slots, span validity, ms
// conversion, the report JSON) is the pure, unit-tested core in glperf_compute; this file is just the
// GL glue plus the capture state machine. Compiled only when FUA_MCP_BRIDGE is ON.
//
#ifdef FUA_MCP_BRIDGE

#include <string>
#include <vector>

#include "mcp_glperf.h"
#include "features/mcp-bridge/computation/glperf_compute.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h" // JsonEscape

using namespace zx::mcp;

#ifndef NO_GL

#include "gl/system/gl_system.h"    // GLEW + GL types (same surface gl_portal.cpp uses for queries)
#include "gl/system/gl_interface.h" // the `gl` RenderContext (vendor string, caps)
#include "gl/utility/gl_clock.h"    // per-frame primitive counters (rendered_lines/flats/sprites/...)

namespace
{
	// Query ring: one set of query objects per in-flight frame. A frame's results are read back RING
	// frames later, by which point the GPU has finished them -- so the read never blocks. Four is
	// plenty of slack for any sane render-ahead depth while staying tiny (RING*MARKERS query objects).
	const int RING    = 4;
	const int MARKERS = 2 + 2 * GLZONE_COUNT; // frame begin/end + begin/end per zone

	int MkFrameBegin()      { return 0; }
	int MkFrameEnd()        { return 1; }
	int MkZoneBegin( int z ) { return 2 + z * 2; }
	int MkZoneEnd( int z )   { return 2 + z * 2 + 1; }

	GLuint   g_query[RING][MARKERS] = {{0}};
	bool     g_haveQueries = false;

	bool     g_checkedSupport = false;
	bool     g_supported      = false;

	// Capture state.
	bool     g_capturing  = false;
	int      g_want       = 0;
	int      g_warmup     = 0;
	uint64_t g_frameIndex = 0;
	bool     g_frameActive = false;
	int      g_frameSlot   = -1;

	bool     g_zoneBegun[GLZONE_COUNT];
	unsigned g_curZoneMask = 0;
	bool     g_slotHasFrame[RING]  = { false };
	unsigned g_slotZoneMask[RING]  = { 0 };

	std::vector<double> g_totalMs;
	std::vector<double> g_zoneMs[GLZONE_COUNT];

	// Draw counters, summed over the capture (CPU-side, immediately available). The engine zeroes these
	// at the top of every RenderView (ResetProfilingData), so by FrameEnd they already hold THIS frame's
	// count -- read the absolute value, no per-frame delta needed.
	long long g_sumWalls, g_sumFlats, g_sumSprites, g_sumPortals, g_sumVerts;
	int      g_counterFrames;

	std::string g_report;
	bool     g_reportReady = false;

	bool CheckSupport()
	{
		if ( g_checkedSupport ) return g_supported;
		// GLEW must be initialised (video up) for these to be meaningful; the caller only reaches here
		// during a live capture, well after startup.
		g_supported = ( GLEW_VERSION_3_3 || glewIsSupported( "GL_ARB_timer_query" ) )
		              && glQueryCounter != NULL && glGetQueryObjectui64v != NULL;
		g_checkedSupport = true;
		return g_supported;
	}

	void EnsureQueries()
	{
		if ( g_haveQueries ) return;
		glGenQueries( RING * MARKERS, &g_query[0][0] );
		g_haveQueries = true;
	}

	void AccumulateCounters()
	{
		// Read at FrameEnd = this frame's per-frame count (the engine reset them at RenderView start).
		g_sumWalls   += rendered_lines;
		g_sumFlats   += rendered_flats;
		g_sumSprites += rendered_sprites;
		g_sumPortals += rendered_portals;
		g_sumVerts   += vertexcount;
		++g_counterFrames;
	}

	std::string BuildCountersJson()
	{
		if ( g_counterFrames <= 0 ) return std::string();
		const long long n = g_counterFrames;
		std::string s = "{";
		s += "\"walls\":"    + std::to_string( g_sumWalls   / n );
		s += ",\"flats\":"   + std::to_string( g_sumFlats   / n );
		s += ",\"sprites\":" + std::to_string( g_sumSprites / n );
		s += ",\"portals\":" + std::to_string( g_sumPortals / n );
		s += ",\"vertices\":" + std::to_string( g_sumVerts  / n );
		s += ",\"note\":\"per-frame averages over the capture\"}";
		return s;
	}

	void Finish()
	{
		GLTimingVerdict v = GLAssessTiming( g_totalMs );
		std::vector<std::vector<double>> zones( GLZONE_COUNT );
		for ( int z = 0; z < GLZONE_COUNT; ++z ) zones[z] = g_zoneMs[z];
		g_report      = GLTimersJson( v, zones, g_totalMs, BuildCountersJson() );
		g_reportReady = true;
		g_capturing   = false;
	}

	void MaybeFinish()
	{
		if ( (int)g_totalMs.size() >= g_want ) Finish();
	}

	void CollectSlot( int slot )
	{
		GLuint avail = 0;
		glGetQueryObjectuiv( g_query[slot][MkFrameEnd()], GL_QUERY_RESULT_AVAILABLE, &avail );
		if ( !avail ) return; // never block; a not-yet-ready slot just costs us one sample

		GLuint64 fb = 0, fe = 0;
		glGetQueryObjectui64v( g_query[slot][MkFrameBegin()], GL_QUERY_RESULT, &fb );
		glGetQueryObjectui64v( g_query[slot][MkFrameEnd()],   GL_QUERY_RESULT, &fe );
		double total = GLSpanMs( (uint64_t)fb, (uint64_t)fe );

		double zoneMs[GLZONE_COUNT];
		const unsigned mask = g_slotZoneMask[slot];
		for ( int z = 0; z < GLZONE_COUNT; ++z )
		{
			zoneMs[z] = 0.0;
			if ( !( mask & ( 1u << z ) ) ) continue;
			GLuint64 zb = 0, ze = 0;
			glGetQueryObjectui64v( g_query[slot][MkZoneBegin( z )], GL_QUERY_RESULT, &zb );
			glGetQueryObjectui64v( g_query[slot][MkZoneEnd( z )],   GL_QUERY_RESULT, &ze );
			zoneMs[z] = GLSpanMs( (uint64_t)zb, (uint64_t)ze );
		}

		if ( !g_capturing ) return;
		if ( g_warmup > 0 ) { --g_warmup; return; } // discard scene-change one-time costs

		g_totalMs.push_back( total );
		for ( int z = 0; z < GLZONE_COUNT; ++z )
			if ( mask & ( 1u << z ) ) g_zoneMs[z].push_back( zoneMs[z] ); // only frames where the pass ran
		MaybeFinish();
	}
} // namespace

void MCP_GLPerf_FrameBegin()
{
	if ( !g_capturing ) return;
	CheckSupport();
	for ( int z = 0; z < GLZONE_COUNT; ++z ) g_zoneBegun[z] = false;
	g_curZoneMask = 0;
	g_frameActive = true;

	if ( !g_supported ) { g_frameSlot = -1; return; }

	EnsureQueries();
	int slot = GLRingSlot( g_frameIndex, RING );
	if ( GLRingReady( g_frameIndex, RING ) && g_slotHasFrame[slot] )
		CollectSlot( slot ); // read the frame that last used this slot (finished long ago) -- no stall
	g_frameSlot = slot;
	g_slotHasFrame[slot] = false; // re-armed at FrameEnd once fully issued
	glQueryCounter( g_query[slot][MkFrameBegin()], GL_TIMESTAMP );
}

void MCP_GLPerf_ZoneBegin( int zone )
{
	if ( !g_capturing || !g_frameActive || !g_supported ) return;
	if ( zone < 0 || zone >= GLZONE_COUNT ) return;
	if ( g_zoneBegun[zone] ) return; // first-wins: portal recursion re-enters a pass; keep the outer span
	g_zoneBegun[zone] = true;
	glQueryCounter( g_query[g_frameSlot][MkZoneBegin( zone )], GL_TIMESTAMP );
}

void MCP_GLPerf_ZoneEnd( int zone )
{
	if ( !g_capturing || !g_frameActive || !g_supported ) return;
	if ( zone < 0 || zone >= GLZONE_COUNT ) return;
	if ( !g_zoneBegun[zone] ) return;
	glQueryCounter( g_query[g_frameSlot][MkZoneEnd( zone )], GL_TIMESTAMP ); // last-wins: extend to the outer end
	g_curZoneMask |= ( 1u << zone );
}

void MCP_GLPerf_FrameEnd()
{
	if ( !g_capturing || !g_frameActive ) return;
	g_frameActive = false;
	AccumulateCounters();

	if ( !g_supported )
	{
		// No usable timers: still advance the capture so it terminates and reports "unavailable" honestly.
		if ( g_warmup > 0 ) { --g_warmup; return; }
		g_totalMs.push_back( 0.0 );
		MaybeFinish();
		return;
	}

	glQueryCounter( g_query[g_frameSlot][MkFrameEnd()], GL_TIMESTAMP );
	g_slotZoneMask[g_frameSlot] = g_curZoneMask;
	g_slotHasFrame[g_frameSlot] = true;
	++g_frameIndex;
}

void MCP_GLPerf_BeginCapture( int frames, int warmup )
{
	g_want   = frames > 0 ? frames : 1;
	g_warmup = warmup > 0 ? warmup : 0;
	g_totalMs.clear();
	for ( int z = 0; z < GLZONE_COUNT; ++z ) g_zoneMs[z].clear();
	g_frameIndex = 0;
	for ( int i = 0; i < RING; ++i ) g_slotHasFrame[i] = false; // don't read stale slots from a prior capture
	g_sumWalls = g_sumFlats = g_sumSprites = g_sumPortals = g_sumVerts = 0;
	g_counterFrames = 0;
	g_reportReady = false;
	g_frameActive = false;
	g_capturing   = true;
}

bool MCP_GLPerf_ReportReady( std::string &jsonOut )
{
	if ( !g_reportReady ) return false;
	jsonOut = g_report;
	g_reportReady = false;
	return true;
}

std::string MCP_GLPerf_RendererInfo()
{
	if ( gl.vendorstring == NULL )
		return "{\"available\":false,\"reason\":\"gl not initialised\"}";

	std::string vendor, renderer, version, glsl;
	JsonEscape( gl.vendorstring ? gl.vendorstring : "", vendor );
	const char *r = (const char *)glGetString( GL_RENDERER );
	const char *v = (const char *)glGetString( GL_VERSION );
	const char *g = (const char *)glGetString( GL_SHADING_LANGUAGE_VERSION );
	JsonEscape( r ? r : "", renderer );
	JsonEscape( v ? v : "", version );
	JsonEscape( g ? g : "", glsl );

	std::string out = "{\"available\":true";
	out += ",\"vendor\":\"" + vendor + "\"";
	out += ",\"renderer\":\"" + renderer + "\"";
	out += ",\"version\":\"" + version + "\"";
	out += ",\"glsl\":\"" + glsl + "\"";
	out += ",\"max_texture_size\":" + std::to_string( gl.max_texturesize );
	out += ",\"timer_query\":" + std::string( CheckSupport() ? "true" : "false" );
	out += "}";
	return out;
}

#else // NO_GL -- server build: the bridge is present but there is no renderer to profile.

void MCP_GLPerf_FrameBegin() {}
void MCP_GLPerf_ZoneBegin( int ) {}
void MCP_GLPerf_ZoneEnd( int ) {}
void MCP_GLPerf_FrameEnd() {}
void MCP_GLPerf_BeginCapture( int, int ) {}
bool MCP_GLPerf_ReportReady( std::string & ) { return false; }
std::string MCP_GLPerf_RendererInfo() { return "{\"available\":false,\"reason\":\"server build (no gl)\"}"; }

#endif // NO_GL

#endif // FUA_MCP_BRIDGE
