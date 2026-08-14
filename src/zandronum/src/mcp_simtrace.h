//
// mcp_simtrace.h -- sim event tracer for the native MCP bridge (features/mcp-bridge). Records
// every damage, kill and spawn with (tic, class, position) while armed, then writes a plain-text
// trace file. Diffing two runs' traces names the exact event that shifts between them -- built to
// corner the Complex Doom kill-storm nondeterminism, where a whole death event moves by a tic
// depending on how many frames ran between sim tics. Compiled only when FUA_MCP_BRIDGE is ON;
// when OFF every anchor is an inline no-op (same pattern as mcp_ticprof.h / mcp_glperf.h), and
// even when ON each anchor is one bool test while disarmed.
//
#ifndef MCP_SIMTRACE_H
#define MCP_SIMTRACE_H

class AActor;

#ifdef FUA_MCP_BRIDGE

#include <string>

void MCP_SimTrace_Arm( int tics, const char *path );
void MCP_SimTrace_TicDone();                       // advances/finishes the capture window
void MCP_SimTrace_Damage( AActor *target, AActor *inflictor, int damage );
void MCP_SimTrace_Kill( AActor *victim, AActor *source );
void MCP_SimTrace_Spawn( AActor *spawned );
bool MCP_SimTrace_ReportReady( std::string &json ); // true once when the file is written

#else

inline void MCP_SimTrace_Damage( AActor *, AActor *, int ) {}
inline void MCP_SimTrace_Kill( AActor *, AActor * ) {}
inline void MCP_SimTrace_Spawn( AActor * ) {}
inline void MCP_SimTrace_TicDone() {}

#endif

#endif
