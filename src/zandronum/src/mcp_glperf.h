//
// mcp_glperf.h -- GPU render profiler for the native MCP bridge (features/mcp-bridge). Brackets the
// engine's render passes with GL_TIMESTAMP timer queries so a driver can be asked "on this laggy map,
// how many GPU-milliseconds go to opaque world geometry vs the translucent/overdraw pass vs the 2D
// HUD?" -- something the CPU sampler cannot see. Compiled only when FUA_MCP_BRIDGE is ON.
//
// When OFF, every entry point below is an inline no-op, so the render-pass anchors (single lines in
// gl/scene/gl_scene.cpp and gl/system/gl_framebuffer.cpp) compile to nothing and release builds are
// byte-identical -- no timer queries, no query objects, no per-frame cost, nothing in the hot path.
//
// Even when built ON the queries are dormant until a capture is armed (gl.timers RPC), so an ON dev
// build that isn't profiling also issues zero GL query calls per frame.
//
#ifndef MCP_GLPERF_H
#define MCP_GLPERF_H

#include <string>

// Zone ids for the render-pass anchors. Kept as plain ints (mirroring zx::mcp::GLZone) so the gl/*.cpp
// call sites need no bridge namespace. See features/mcp-bridge/computation/glperf_compute.h.
enum
{
	MCP_GLZ_SCENE       = 0, // opaque BSP: walls, flats, sprites
	MCP_GLZ_TRANSLUCENT = 1, // blended/additive pass: smoke, translucent sprites (fill-rate)
	MCP_GLZ_PORTALS     = 2, // reserved
	MCP_GLZ_POSTPROCESS = 3, // reserved
	MCP_GLZ_HUD2D       = 4  // status bar / menu / console 2D
};

#ifdef FUA_MCP_BRIDGE

// --- Render-thread anchors (called from the GL renderer) --------------------
void MCP_GLPerf_FrameBegin();     // earliest GL work of a frame (top of FGLRenderer::RenderView)
void MCP_GLPerf_ZoneBegin( int zone );
void MCP_GLPerf_ZoneEnd( int zone );
void MCP_GLPerf_FrameEnd();       // after 2D, before the buffer swap (OpenGLFrameBuffer::Update)

// --- Control (called from mcp_rpc.cpp on the game thread) -------------------
// Arm a capture of `frames` presented frames, discarding the first `warmup` (one-time scene-change
// costs). The report is delivered asynchronously via MCP_GLPerf_ReportReady, polled once per tic.
void MCP_GLPerf_BeginCapture( int frames, int warmup );

// If a capture has finished since the last poll, moves its report JSON into `jsonOut` and returns true
// (once). Returns false otherwise. Polled from the per-tic bridge pump, same as the perf.capture event.
bool MCP_GLPerf_ReportReady( std::string &jsonOut );

// Synchronous renderer identity + timer-query capability, as a JSON object. Safe any time.
std::string MCP_GLPerf_RendererInfo();

#else

inline void MCP_GLPerf_FrameBegin() {}
inline void MCP_GLPerf_ZoneBegin( int ) {}
inline void MCP_GLPerf_ZoneEnd( int ) {}
inline void MCP_GLPerf_FrameEnd() {}
inline void MCP_GLPerf_BeginCapture( int, int ) {}
inline bool MCP_GLPerf_ReportReady( std::string & ) { return false; }
inline std::string MCP_GLPerf_RendererInfo() { return "{}"; }

#endif // FUA_MCP_BRIDGE

#endif // MCP_GLPERF_H
