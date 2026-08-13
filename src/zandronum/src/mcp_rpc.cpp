//
// mcp_rpc.cpp -- the "programmable engine" RPC dispatch for the native MCP bridge (ENGINE-FACING).
//
// mcp_bridge.cpp owns the socket and queue and hands each request here on the GAME THREAD (so every
// handler touches engine state safely). Wire framing is the tested pure core in
// features/mcp-bridge/computation/mcprpc_compute. Compiled only when FUA_MCP_BRIDGE is defined.
//
// Command families:
//   ping / capabilities / console.exec
//   sim.tic sim.hash sim.seed sim.pause sim.resume sim.step sim.snapshot sim.restore   (determinism)
//   state.player state.actors                                                          (structured state)
//   input.event                                                                        (input injection)
//
#include "doomtype.h"
#include "doomstat.h"
#include "c_dispatch.h"
#include "d_event.h"
#include "d_player.h"
#include "actor.h"
#include "p_local.h"
#include "m_fixed.h"
#include "tables.h"
#include "g_level.h"
#include "g_game.h"
#include "m_joy.h"
#include "network.h"
#include "r_state.h"
#include "m_random.h"
#include "zstring.h"
#include "mcp_hud.h"

#include "features/mcp-bridge/computation/mcprpc_compute.h"

#include <string>
#include <vector>
#include <chrono>
#include <stdlib.h>

// From mcp_bridge.cpp -- send one framed NDJSON line to the connected client (thread-safe).
void MCP_Bridge_SendJson( const char *json );
// From mcp_event.cpp -- post a synthetic input event.
void MCP_PostInputEvent( int type, int subtype, int data1, int data2 );

extern DWORD rngseed; // m_random.h: the base seed all RNGs derive from

namespace
{
	using namespace zx::mcp;

	// --- sim-step state (advance N tics then refreeze) ----------------------
	bool g_stepping   = false;
	long g_stepTarget = 0;

	// --- perf capture -------------------------------------------------------
	// Frame timing at the D_DoomLoop seam: MCP_RPC_Tick runs at the TOP of each iteration, so the
	// delta between two Ticks is the FULL just-completed frame. MCP_RPC_MarkRender (anchored right
	// before D_Display) splits that into sim vs render coarsely -- one seam each, none in the hot
	// render path, so backports stay clean.
	typedef std::chrono::steady_clock PerfClock;
	PerfClock::time_point g_frameStart;
	PerfClock::time_point g_renderMark;
	bool   g_haveFrameStart = false;
	bool   g_haveRenderMark = false;
	bool   g_perfCapturing  = false;
	int    g_perfWant       = 0;
	int    g_perfWarmup     = 0; // discard the first few frames of a capture (one-time costs)
	std::vector<double> g_perfTotal, g_perfSim, g_perfRender;

	// --- network RECEIVE bandwidth accounting (per server-command / SVC id) --
	// O(1) integer tally at the client parse funnel; read out here off the hot path.
	unsigned long long    g_svcRecvBytes[256] = { 0 };
	unsigned int          g_svcRecvCount[256] = { 0 };
	PerfClock::time_point g_netStart;
	bool                  g_netStarted = false;

	// --- synthetic analog axis override (controller sticks) -----------------
	// Analog sticks/triggers are hardware-polled into G_BuildTiccmd via I_GetAxes; they never reach
	// the event queue, so input.event can't drive them. Instead the bridge holds a set of axis values
	// and MCP_RPC_OverrideAxes (anchored right after I_GetAxes) stamps them in each tic -- a stick
	// "held" at a position. Deterministic: the same held values feed every tic, running the full
	// deadzone/scale/accel pipeline exactly as a real stick would.
	float g_axisOverride[NUM_JOYAXIS] = { 0 };
	bool  g_axisActive = false;

	double MsSince( PerfClock::time_point a, PerfClock::time_point b )
	{
		return std::chrono::duration<double, std::milli>( b - a ).count();
	}
	double MeanOf( const std::vector<double> &v )
	{
		if ( v.empty() ) return 0.0;
		double s = 0.0; for ( double x : v ) s += x; return s / (double)v.size();
	}

	void SendOk( long id, const std::string &body )
	{
		if ( id < 0 ) return; // fire-and-forget request wants no reply
		MCP_Bridge_SendJson( BuildOkResponse( id, body ).c_str() );
	}
	void SendErr( long id, const std::string &msg )
	{
		if ( id < 0 ) return;
		MCP_Bridge_SendJson( BuildErrResponse( id, msg ).c_str() );
	}
	void EmitEvent( const char *name, const std::string &data )
	{
		MCP_Bridge_SendJson( BuildEvent( name, data ).c_str() );
	}

	std::string I( long long v ) { return std::to_string( v ); }
	std::string B( bool v )      { return v ? "true" : "false"; }

	bool InLevel() { return gamestate == GS_LEVEL; }

	// The `paused` flag only freezes the LOCAL single-player P_Ticker. A server/client runs its sim
	// off network tics (TryRunTics), so pause/step can't hold the world still there -- report that
	// plainly instead of silently doing nothing.
	bool IsNetInstance()
	{
		LONG s = NETWORK_GetState();
		return s == NETSTATE_SERVER || s == NETSTATE_CLIENT;
	}

	// Deterministic fingerprint of the sim: level clock + RNG position + every actor's transform &
	// health, mixed in thinker-list order (stable across save/load round-trips). Two instances at the
	// same level.time with identical simulation return the same value; a mismatch is a desync.
	QWORD StateHash()
	{
		uint64_t h = FnvInit();
		h = FnvMixU64( h, (uint64_t)level.time );
		h = FnvMixU64( h, (uint64_t)FRandom::StaticSumSeeds() );
		if ( InLevel() )
		{
			TThinkerIterator<AActor> it;
			AActor *mo;
			while ( ( mo = it.Next() ) != NULL )
			{
				h = FnvMixU64( h, (uint64_t)mo->x );
				h = FnvMixU64( h, (uint64_t)mo->y );
				h = FnvMixU64( h, (uint64_t)mo->z );
				h = FnvMixU64( h, (uint64_t)mo->angle );
				h = FnvMixU64( h, (uint64_t)mo->health );
			}
		}
		return (QWORD)h;
	}

	std::string ActorJson( AActor *mo )
	{
		std::string s = "{\"x\":" + I( (long long)( mo->x >> FRACBITS ) );
		s += ",\"y\":" + I( (long long)( mo->y >> FRACBITS ) );
		s += ",\"z\":" + I( (long long)( mo->z >> FRACBITS ) );
		s += ",\"angle\":" + I( (long long)( mo->angle >> 24 ) ); // ~degrees*256/360; raw hi byte
		s += ",\"health\":" + I( mo->health );
		s += "}";
		return s;
	}
}

// Anchored right before D_Display in D_DoomLoop: marks the sim|render boundary for the coarse split.
void MCP_RPC_MarkRender()
{
	g_renderMark = PerfClock::now();
	g_haveRenderMark = true;
}

// Anchored in the client's command parse loop: tally RECEIVE bytes for one server command.
void MCP_NetProf_Recv( int svc, int bytes )
{
	if ( svc < 0 || svc >= 256 || bytes <= 0 ) return;
	g_svcRecvBytes[svc] += (unsigned long long)bytes;
	g_svcRecvCount[svc]++;
}

// Anchored right after I_GetAxes in G_BuildTiccmd: if the bridge holds synthetic axis values, stamp
// them over the polled (empty) hardware axes so they drive the ticcmd this tic. No-op otherwise.
void MCP_RPC_OverrideAxes( float *axes )
{
	if ( !g_axisActive || axes == NULL ) return;
	for ( int i = 0; i < NUM_JOYAXIS; ++i ) axes[i] = g_axisOverride[i];
}

void MCP_RPC_Tick()
{
	MCP_HUD_BeginFrame(); // snapshot the frame the engine just drew (for screenshots/HUD reads)

	// Perf frame accounting. This Tick runs at the TOP of D_DoomLoop, i.e. the end of the frame that
	// just finished, so record that frame's total/sim/render times if a capture is running.
	{
		PerfClock::time_point now = PerfClock::now();
		if ( g_haveFrameStart && g_perfCapturing )
		{
			double total  = MsSince( g_frameStart, now );
			double render = g_haveRenderMark ? MsSince( g_renderMark, now ) : 0.0;
			double sim    = g_haveRenderMark ? MsSince( g_frameStart, g_renderMark ) : total;
			if ( g_perfWarmup > 0 )
			{
				--g_perfWarmup; // discard warm-up frames (one-time costs after a scene change)
			}
			else
			{
				g_perfTotal.push_back( total );
				g_perfSim.push_back( sim );
				g_perfRender.push_back( render );
				if ( (int)g_perfTotal.size() >= g_perfWant )
				{
					std::string data = "{\"total\":" + PerfSummaryJson( SummarizeFrameTimes( g_perfTotal ) );
					data += ",\"sim_mean_ms\":" + std::to_string( MeanOf( g_perfSim ) );
					data += ",\"render_mean_ms\":" + std::to_string( MeanOf( g_perfRender ) ) + "}";
					EmitEvent( "perf", data );
					g_perfCapturing = false;
					g_perfTotal.clear(); g_perfSim.clear(); g_perfRender.clear();
				}
			}
		}
		g_frameStart = now;
		g_haveFrameStart = true;
		g_haveRenderMark = false;
	}

	// Drive a scheduled step. Force paused=0 EVERY frame while stepping so the single-player
	// focus-loss auto-pause (S_SetSoundPaused sets paused=-1 on a backgrounded window) can't stall
	// a controlled advance -- the whole point of headless determinism. Refreeze at the target tic.
	if ( g_stepping )
	{
		if ( StepComplete( level.time, g_stepTarget ) )
		{
			paused = 1;
			g_stepping = false;
			EmitEvent( "stepped", std::string( "{\"leveltime\":" ) + I( level.time ) + "}" );
		}
		else
		{
			paused = 0;
		}
	}
}

void MCP_RPC_Dispatch( long id, const char *cmdC, const char *argsC )
{
	std::string cmd = cmdC ? cmdC : "";
	std::string args = argsC ? argsC : "{}";

	if ( cmd == "ping" )
	{
		SendOk( id, "{\"pong\":true,\"bridge\":\"2.0.0\"}" );
	}
	else if ( cmd == "capabilities" )
	{
		SendOk( id, "{\"commands\":["
			"\"ping\",\"capabilities\",\"console.exec\","
			"\"sim.tic\",\"sim.hash\",\"sim.seed\",\"sim.pause\",\"sim.resume\",\"sim.step\","
			"\"sim.snapshot\",\"sim.restore\",\"state.player\",\"state.actors\",\"input.event\",\"input.axis\",\"input.look\","
			"\"perf.capture\",\"perf.counters\",\"net.bandwidth\""
			"],\"events\":[\"out\",\"stepped\",\"perf\"]}" );
	}
	else if ( cmd == "console.exec" )
	{
		std::string text;
		if ( !GetStr( args, "text", text ) || text.empty() )
		{
			SendErr( id, "console.exec requires args.text" );
			return;
		}
		AddCommandString( const_cast<char *>( text.c_str() ), 0 );
		SendOk( id, "{\"executed\":true}" );
	}
	else if ( cmd == "sim.tic" )
	{
		std::string body = "{\"gametic\":" + I( gametic );
		body += ",\"leveltime\":" + I( level.time );
		body += ",\"gamestate\":" + I( (int)gamestate );
		body += ",\"inlevel\":" + B( InLevel() );
		body += ",\"paused\":" + I( paused ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "sim.hash" )
	{
		std::string body = "{\"leveltime\":" + I( level.time );
		body += ",\"hash\":\"" + I( (long long)StateHash() ) + "\"}"; // string to survive 64-bit in JSON
		SendOk( id, body );
	}
	else if ( cmd == "sim.seed" )
	{
		std::string op;
		GetStr( args, "op", op );
		if ( op == "set" )
		{
			long v = 0;
			if ( !GetInt( args, "value", v ) ) { SendErr( id, "sim.seed set requires args.value" ); return; }
			rngseed = (DWORD)v;
			FRandom::StaticClearRandom(); // re-seed every RNG from the new base
		}
		SendOk( id, std::string( "{\"rngseed\":" ) + I( (long long)(unsigned long)rngseed ) + "}" );
	}
	else if ( cmd == "sim.pause" )
	{
		if ( IsNetInstance() ) { SendErr( id, "pause unsupported in a netgame (server/client sim runs on network tics, not the local paused flag)" ); return; }
		paused = 1;
		SendOk( id, "{\"paused\":1}" );
	}
	else if ( cmd == "sim.resume" )
	{
		if ( IsNetInstance() ) { SendErr( id, "resume unsupported in a netgame (nothing to unpause -- the net sim was never frozen)" ); return; }
		paused = 0;
		g_stepping = false;
		SendOk( id, "{\"paused\":0}" );
	}
	else if ( cmd == "sim.step" )
	{
		if ( IsNetInstance() ) { SendErr( id, "step unsupported in a netgame (deterministic stepping is single-player only)" ); return; }
		long tics = 1;
		GetInt( args, "tics", tics );
		g_stepTarget = StepTarget( level.time, (int)tics );
		g_stepping = true;
		paused = 0; // let the world advance; MCP_RPC_Tick refreezes at the target and emits "stepped"
		SendOk( id, std::string( "{\"target\":" ) + I( g_stepTarget ) + "}" );
	}
	else if ( cmd == "sim.snapshot" )
	{
		long slot = 0;
		GetInt( args, "slot", slot );
		FString path = G_BuildSaveName( "fua_mcp_snap", (int)slot );
		G_SaveGame( path.GetChars(), "mcp snapshot" ); // deferred: runs at the next G_Ticker
		std::string esc; JsonEscape( std::string( path.GetChars() ), esc );
		SendOk( id, std::string( "{\"slot\":" ) + I( slot ) + ",\"path\":\"" + esc + "\",\"scheduled\":true}" );
	}
	else if ( cmd == "sim.restore" )
	{
		long slot = 0;
		GetInt( args, "slot", slot );
		FString path = G_BuildSaveName( "fua_mcp_snap", (int)slot );
		G_LoadGame( path.GetChars(), true );
		std::string esc; JsonEscape( std::string( path.GetChars() ), esc );
		SendOk( id, std::string( "{\"slot\":" ) + I( slot ) + ",\"path\":\"" + esc + "\",\"scheduled\":true}" );
	}
	else if ( cmd == "state.player" )
	{
		AActor *mo = ( consoleplayer >= 0 && consoleplayer < MAXPLAYERS ) ? players[consoleplayer].mo : NULL;
		if ( mo == NULL ) { SendErr( id, "no console player pawn" ); return; }
		SendOk( id, ActorJson( mo ) );
	}
	else if ( cmd == "state.actors" )
	{
		long limit = 256;
		GetInt( args, "limit", limit );
		std::string body = "{\"actors\":[";
		int n = 0;
		if ( InLevel() )
		{
			TThinkerIterator<AActor> it;
			AActor *mo;
			while ( ( mo = it.Next() ) != NULL && n < limit )
			{
				if ( n ) body += ",";
				body += ActorJson( mo );
				++n;
			}
		}
		body += "],\"count\":" + I( n ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "input.event" )
	{
		long ev = 0, sub = 0, d1 = 0, d2 = 0;
		GetInt( args, "evtype", ev );
		GetInt( args, "subtype", sub );
		GetInt( args, "data1", d1 );
		GetInt( args, "data2", d2 );
		MCP_PostInputEvent( (int)ev, (int)sub, (int)d1, (int)d2 );
		SendOk( id, "{\"posted\":true}" );
	}
	else if ( cmd == "input.axis" )
	{
		// Synthetic analog stick: hold named axes at float values in [-1,1]. Any subset may be given;
		// unspecified axes keep their held value. {"clear":true} releases the override entirely.
		long clear = 0;
		GetInt( args, "clear", clear );
		if ( clear )
		{
			g_axisActive = false;
			for ( int i = 0; i < NUM_JOYAXIS; ++i ) g_axisOverride[i] = 0.0f;
			SendOk( id, "{\"active\":false}" );
		}
		else
		{
			struct { const char *key; int idx; } map[] = {
				{ "yaw", JOYAXIS_Yaw }, { "pitch", JOYAXIS_Pitch },
				{ "forward", JOYAXIS_Forward }, { "side", JOYAXIS_Side }, { "up", JOYAXIS_Up },
			};
			double v = 0.0;
			for ( unsigned k = 0; k < sizeof( map ) / sizeof( map[0] ); ++k )
			{
				if ( GetFloat( args, map[k].key, v ) )
				{
					if ( v > 1.0 ) v = 1.0; else if ( v < -1.0 ) v = -1.0;
					g_axisOverride[map[k].idx] = (float)v;
				}
			}
			g_axisActive = true;
			std::string body = "{\"active\":true,\"yaw\":" + std::to_string( g_axisOverride[JOYAXIS_Yaw] )
				+ ",\"pitch\":" + std::to_string( g_axisOverride[JOYAXIS_Pitch] )
				+ ",\"forward\":" + std::to_string( g_axisOverride[JOYAXIS_Forward] )
				+ ",\"side\":" + std::to_string( g_axisOverride[JOYAXIS_Side] )
				+ ",\"up\":" + std::to_string( g_axisOverride[JOYAXIS_Up] ) + "}";
			SendOk( id, body );
		}
	}
	else if ( cmd == "input.look" )
	{
		// Precise relative view rotation, in DEGREES. yaw>0 = left, pitch>0 = down (intuitive), which
		// map to the engine's own turn functions (Button_Left is -yaw, Button_LookDown is -pitch).
		// These drive LocalViewAngle through the ticcmd, so it works single-player AND as a netclient;
		// the view updates on the next tic (advance one tic, or let a running game apply it).
		double yaw = 0.0, pitch = 0.0;
		bool haveYaw = GetFloat( args, "yaw", yaw );
		bool havePitch = GetFloat( args, "pitch", pitch );
		if ( haveYaw && yaw != 0.0 ) G_AddViewAngle( (int)( -DegreesToViewUnits( yaw ) ) );
		if ( havePitch && pitch != 0.0 ) G_AddViewPitch( (int)( -DegreesToViewUnits( pitch ) ) );
		// Report the pawn's CURRENT facing (pre-tic) in degrees so a caller can diff across a step.
		AActor *mo = ( consoleplayer >= 0 && consoleplayer < MAXPLAYERS ) ? players[consoleplayer].mo : NULL;
		double angleDeg = mo ? ( (double)(unsigned long)mo->angle * 360.0 / 4294967296.0 ) : 0.0;
		std::string body = "{\"requested\":{\"yaw\":" + std::to_string( yaw ) + ",\"pitch\":" + std::to_string( pitch )
			+ "},\"angle_deg\":" + std::to_string( angleDeg ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "perf.capture" )
	{
		long frames = 120, warmup = 3;
		GetInt( args, "frames", frames );
		GetInt( args, "warmup", warmup );
		if ( frames < 1 ) frames = 1;
		g_perfWant = (int)frames;
		g_perfWarmup = warmup > 0 ? (int)warmup : 0;
		g_perfTotal.clear(); g_perfSim.clear(); g_perfRender.clear();
		g_perfCapturing = true;
		// The summary arrives asynchronously as a "perf" event once `frames` frames are collected.
		SendOk( id, std::string( "{\"capturing\":true,\"frames\":" ) + I( frames ) + ",\"warmup\":" + I( warmup ) + "}" );
	}
	else if ( cmd == "perf.counters" )
	{
		int actors = 0;
		if ( InLevel() )
		{
			TThinkerIterator<AActor> it;
			while ( it.Next() != NULL ) ++actors;
		}
		std::string body = "{\"actors\":" + I( actors );
		body += ",\"numsegs\":" + I( numsegs );
		body += ",\"leveltime\":" + I( level.time ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "net.bandwidth" )
	{
		std::string op;
		GetStr( args, "op", op );
		if ( op == "reset" )
		{
			for ( int i = 0; i < 256; ++i ) { g_svcRecvBytes[i] = 0; g_svcRecvCount[i] = 0; }
			g_netStart = PerfClock::now();
			g_netStarted = true;
			SendOk( id, "{\"reset\":true}" );
			return;
		}
		double elapsed = g_netStarted ? ( MsSince( g_netStart, PerfClock::now() ) / 1000.0 ) : 0.0;
		unsigned long long total = 0;
		for ( int i = 0; i < 256; ++i ) total += g_svcRecvBytes[i];
		long topN = 12;
		GetInt( args, "top", topN );
		// Selection of the top-N commands by bytes (256 is tiny; no need for a real sort).
		bool used[256] = { false };
		std::string arr = "[";
		int listed = 0;
		for ( long k = 0; k < topN; ++k )
		{
			int best = -1;
			unsigned long long bestB = 0;
			for ( int i = 0; i < 256; ++i )
				if ( !used[i] && g_svcRecvBytes[i] > bestB ) { bestB = g_svcRecvBytes[i]; best = i; }
			if ( best < 0 ) break;
			used[best] = true;
			if ( listed ) arr += ",";
			arr += "{\"svc\":" + I( best ) + ",\"bytes\":" + I( (long long)g_svcRecvBytes[best] )
				+ ",\"count\":" + I( (long long)g_svcRecvCount[best] ) + "}";
			++listed;
		}
		arr += "]";
		std::string body = "{\"dir\":\"recv\",\"elapsed_s\":" + std::to_string( elapsed );
		body += ",\"total_bytes\":" + I( (long long)total );
		body += ",\"bytes_per_s\":" + std::to_string( elapsed > 0 ? (double)total / elapsed : 0.0 );
		body += ",\"top\":" + arr + "}";
		SendOk( id, body );
	}
	else
	{
		SendErr( id, std::string( "unknown command: " ) + cmd );
	}
}
