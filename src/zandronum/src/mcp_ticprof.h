//
// mcp_ticprof.h -- per-tic sim profiler for the native MCP bridge (features/mcp-bridge). Times the
// next N game tics with a coarse phase split (whole G_Ticker incl. net-command execution -- where
// e.g. "kill monsters" actually runs -- then P_Ticker, thinkers, particle effects, specials), so a
// driver can ask "what is inside the 60 ms tics of this kill storm?" without external samplers.
// Armed by the perf.ticprof RPC; results go out as a "ticprof" event. Compiled only when
// FUA_MCP_BRIDGE is ON; when OFF every entry point below is an inline no-op.
// [rc4l] The phases are features/telemetry's sim zones (computation/ticprof_compute.h names them), read once a tic.
//
#ifndef MCP_TICPROF_H
#define MCP_TICPROF_H

#include <string>

#ifdef FUA_MCP_BRIDGE

void MCP_TicProf_Arm( int tics );
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
inline void MCP_TicProf_TicDone() {}
inline bool MCP_TicProf_ReportReady( std::string & ) { return false; }
inline void MCP_SimPreTic() {}

#endif

#endif
