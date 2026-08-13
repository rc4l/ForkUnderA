//
// mcp_bridge.h -- native in-engine control bridge for the ForkUnderA MCP / fuactl.
//
// This is FIRST-CLASS ForkUnderA code (formerly an injected overlay). It is compiled
// ONLY when the CMake option FUA_MCP_BRIDGE is ON -- dev/test builds turn it on, and
// release builds leave it OFF so the shipped binary contains no remote-control surface
// at all. When OFF, the entry points below become inline no-ops, so the two call-site
// anchors (in d_main.cpp and c_console.cpp) stay byte-identical and simply vanish.
//
// At runtime it is additionally opt-in: the listener only starts when the environment
// variable ZANDRONUM_BRIDGE_PORT is set, binds to 127.0.0.1 only, and (v2) may require
// a launch token. See features/mcp-bridge/README.md and PROTOCOL for the wire format.
//
#ifndef __MCP_BRIDGE_H__
#define __MCP_BRIDGE_H__

#ifdef FUA_MCP_BRIDGE

// Called once per frame from D_DoomLoop. Lazily starts the listener on the first call,
// drains queued RPC requests (dispatched here on the game thread), and pumps events.
void MCP_Bridge_Poll();

// Mirrors one line of console output to the connected client (as an "out" event).
void MCP_Bridge_TeeOutput(const char *text);

// Optional explicit shutdown (process exit also closes the socket).
void MCP_Bridge_Shutdown();

// Arm the crash-backtrace handler (implemented in mcp_crash.cpp). Idempotent, opt-in.
void MCP_Crash_Init();

// Marks the sim|render boundary for the coarse perf split (anchored before D_Display in D_DoomLoop).
void MCP_RPC_MarkRender();

#else

// Release build: no bridge. The anchors compile to nothing.
inline void MCP_Bridge_Poll() {}
inline void MCP_Bridge_TeeOutput(const char *) {}
inline void MCP_Bridge_Shutdown() {}
inline void MCP_Crash_Init() {}
inline void MCP_RPC_MarkRender() {}

#endif // FUA_MCP_BRIDGE

#endif // __MCP_BRIDGE_H__
