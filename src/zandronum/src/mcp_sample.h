//
// mcp_sample.h -- sampling profiler for the native MCP bridge (features/mcp-bridge).
//
// [rc4l] "Which functions are hot" was the one perf question the bridge could not answer. fuactl
// shelled out to macOS `sample` and Linux `perf`, and on Windows there is no equivalent worth
// scripting: wpr.exe needs elevation and WPA or tracerpt to symbolise, xperf needs the ADK, and
// everything else is a third-party install. So the engine samples itself.
//
// A sampler thread interrupts the game thread `hz` times a second and writes down its instruction
// pointer; when the run ends it resolves those addresses through dbghelp -- already linked and
// already used by mcp_crash.cpp -- and reports the hottest functions. That is a flat self-time
// profile: where the CPU actually was, not who called it.
//
// Better than the external tools in one way that matters here: we own the sim clock, so a sample
// run can be bracketed by sim.pause/sim.step and attributed to a known tic range. `sample` and
// `perf` have no idea what a tic is.
//
// Armed by the perf.sample RPC; the report goes out as a "sample" event. Compiled only when
// FUA_MCP_BRIDGE is ON; when OFF every entry point below is an inline no-op, so release builds
// carry no sampler and no dbghelp calls (same pattern as mcp_glperf.h / mcp_ticprof.h).
//
#ifndef MCP_SAMPLE_H
#define MCP_SAMPLE_H

#include <string>

#ifdef FUA_MCP_BRIDGE

// Start a run against the CALLING thread, which must be the game thread -- the RPC dispatch runs
// there, which is the whole reason the handle is taken here rather than guessed later. Returns
// false if a run is already going or the platform has no sampler.
//
// ticMin/ticMax (0,0 = the whole run) keep only the samples that landed in a leveltime range. This
// is the thing an external sampler cannot do, and the reason to have written our own: a six-second
// sample of a one-second spike reports the steady state, which is how a stall that was 90% thinkers
// first read as renderer-bound. Pair it with perf.ticprof to find the expensive tics, then re-ask
// for just those.
bool MCP_Sample_Arm( double seconds, int hz, bool engineOnly, int top, int ticMin, int ticMax );

// Publish the current leveltime for the sampler thread to stamp onto samples. Called from the game
// thread once per bridge tick; the sampler only ever READS it, through an atomic, because the
// threading contract forbids the worker from touching engine state.
void MCP_Sample_PublishTic( int leveltime );

// True once, when a run has finished and its report is ready to emit.
bool MCP_Sample_ReportReady( std::string &json );

// True while a run is in progress, so a second arm can be refused with a reason.
bool MCP_Sample_Running();

#else

inline bool MCP_Sample_Arm( double, int, bool, int, int, int ) { return false; }
inline void MCP_Sample_PublishTic( int ) {}
inline bool MCP_Sample_ReportReady( std::string & ) { return false; }
inline bool MCP_Sample_Running() { return false; }

#endif

#endif
