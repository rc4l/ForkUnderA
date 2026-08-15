//
// mcp_ticprof.h -- per-tic sim profiler for the native MCP bridge (features/mcp-bridge). Times the
// next N game tics with a coarse phase split (whole G_Ticker incl. net-command execution -- where
// e.g. "kill monsters" actually runs -- then P_Ticker, thinkers, particle effects, specials), so a
// driver can ask "what is inside the 60 ms tics of this kill storm?" without external samplers.
// Armed by the perf.ticprof RPC; results go out as a "ticprof" event. Compiled only when
// FUA_MCP_BRIDGE is ON; when OFF every anchor below is an inline no-op and release builds are
// byte-identical (same pattern as mcp_glperf.h).
//
#ifndef MCP_TICPROF_H
#define MCP_TICPROF_H

#include <string>

enum
{
	MCP_TPZ_GTICKER  = 0, // whole G_Ticker: net commands (cheats, kill) + everything below
	MCP_TPZ_PTICKER  = 1, // P_Ticker total
	MCP_TPZ_THINKERS = 2, // DThinker::RunThinkers (actors, ACS scripts, lights)
	MCP_TPZ_EFFECTS  = 3, // P_RunEffects
	MCP_TPZ_SPECIALS = 4, // P_UpdateSpecials
	MCP_TPZ_COUNT    = 5
};

#ifdef FUA_MCP_BRIDGE

void MCP_TicProf_Arm( int tics );
void MCP_TicProf_Begin( int zone );
void MCP_TicProf_End( int zone );
void MCP_TicProf_TicDone();                     // call once per completed game tic
bool MCP_TicProf_ReportReady( std::string &json ); // true once when a capture finishes

// Per-tic bridge pump (implemented in mcp_rpc.cpp): fires tic-scheduled actions such as
// sim.cheatat. Console/cheat commands issued over RPC land in the net-command stream at a
// wall-clock-dependent maketic, so the tic they execute on jitters run to run -- fatal for
// cross-binary lockstep gates. sim.cheatat instead calls the cheat handler directly at the
// scheduled tic, deterministically.
void MCP_SimPreTic();

#else

inline void MCP_TicProf_Arm( int ) {}
inline void MCP_TicProf_Begin( int ) {}
inline void MCP_TicProf_End( int ) {}
inline void MCP_TicProf_TicDone() {}
inline bool MCP_TicProf_ReportReady( std::string & ) { return false; }
inline void MCP_SimPreTic() {}

#endif

#endif
