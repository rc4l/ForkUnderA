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
#include "m_cheat.h"
#include "d_protocol.h"
#include "mcp_hud.h"
#include "mcp_glperf.h"
#include "mcp_ticprof.h"
#include "mcp_simtrace.h"
#include "features/server-browser/browser.h"
#include "features/continue/zx_continue.h"
#include "version.h"
#include "network.h"
#include "sv_main.h" // [rc4l] SERVER_SERVERREGISTRY_GetListingProof, for net.hostdiag
#include "mcp_sample.h"
#include "textures/textures.h"
#include "features/damage-tint/damagetint.h"

#include "features/mcp-bridge/computation/mcprpc_compute.h"

#include <string>
#include <vector>
#include <algorithm>
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

	// --- tic-scheduled cheat (sim.cheatat; fired by MCP_SimPreTic) ----------
	bool g_cheatAtArmed = false;
	int g_cheatAtTic    = 0;
	int g_cheatAtCheat  = 0;

	// --- tic-scheduled pause (sim.pauseat; fired by MCP_SimPreTic) ----------
	// The free-running stretch between launch and a wall-clock sim.pause is the
	// nondeterministic catch-up-batching mode, so lockstep scenarios must freeze at an
	// EXACT tic instead.
	bool g_pauseAtArmed = false;
	int g_pauseAtTic    = 0;

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
	std::vector<int> g_perfTics;      // sim tics run inside each captured frame
	int g_ticsThisFrame;              // incremented by MCP_SimPreTic, reset per frame

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

	// [rc4l] A stable token per state, so a test matches on that rather than on English that is free
	// to be reworded.
	const char *HostDiagStateToken( zx::ListingState state )
	{
		switch ( state )
		{
		case zx::ListingState::NeverAnnounced:   return "never_announced";
		case zx::ListingState::AwaitingAnswer:   return "awaiting_answer";
		case zx::ListingState::Refused:          return "refused";
		case zx::ListingState::ListedUnverified: return "listed_unverified";
		case zx::ListingState::ListedVerified:   return "listed_verified";
		case zx::ListingState::ListedStale:      return "listed_stale";
		}
		return "unknown";
	}

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
	// withRng mixes in the sum of ALL FRandom streams -- including sound RNGs (pr_randsound
	// etc.) whose draw count depends on real-time audio channel availability, so it is NOT
	// run-to-run stable on sound-heavy mods even when the world is bit-identical. Use the
	// world-only form (sim.hash {scope:"world"}) for cross-binary determinism gates.
	// Either scope skips attached dynamic-light actors: they are client-side eye candy
	// spawned into the thinker list whose population couples to rendering, and hashing them
	// makes a bit-identical gameplay sim look divergent (proven: identical 65-tic event
	// traces with "diverging" hashes on the Complex Doom kill storm).
	QWORD StateHash( bool withRng )
	{
		uint64_t h = FnvInit();
		h = FnvMixU64( h, (uint64_t)level.time );
		if ( withRng )
			h = FnvMixU64( h, (uint64_t)FRandom::StaticSumSeeds() );
		if ( InLevel() )
		{
			const PClass *lightcls = PClass::FindClass( "DynamicLight" );
			TThinkerIterator<AActor> it;
			AActor *mo;
			while ( ( mo = it.Next() ) != NULL )
			{
				if ( lightcls != NULL && mo->IsKindOf( lightcls ) )
					continue;
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
		std::string s = "{\"c\":\"";
		s += ( mo->GetClass() != NULL ) ? mo->GetClass()->TypeName.GetChars() : "?";
		s += "\",\"x\":" + I( (long long)( mo->x >> FRACBITS ) );
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

// Called at the top of every game tic (anchor in d_net.cpp, before G_Ticker). Fires
// tic-scheduled actions so scenario injection is deterministic -- see sim.cheatat.
void MCP_SimPreTic()
{
	g_ticsThisFrame++; // frame-composition accounting for perf.capture's worst-frame report

	if ( g_pauseAtArmed && gamestate == GS_LEVEL && level.time >= g_pauseAtTic )
	{
		g_pauseAtArmed = false;
		paused = 1;
	}

	if ( g_cheatAtArmed && gamestate == GS_LEVEL && level.time >= g_cheatAtTic )
	{
		g_cheatAtArmed = false;
		if ( consoleplayer >= 0 && consoleplayer < MAXPLAYERS && playeringame[consoleplayer] )
			cht_DoCheat( &players[consoleplayer], g_cheatAtCheat );
	}
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
				g_perfTics.push_back( g_ticsThisFrame );
				if ( (int)g_perfTotal.size() >= g_perfWant )
				{
					std::string data = "{\"total\":" + PerfSummaryJson( SummarizeFrameTimes( g_perfTotal ) );
					data += ",\"sim_mean_ms\":" + std::to_string( MeanOf( g_perfSim ) );
					data += ",\"render_mean_ms\":" + std::to_string( MeanOf( g_perfRender ) );
					// Worst-frame composition: the top frames by total time, each split into
					// sim ms / render ms / sim tics run inside the frame -- answers "is the
					// spike a tic train, one huge tic, or a render stall?" directly.
					{
						std::vector<size_t> idx( g_perfTotal.size() );
						for ( size_t k = 0; k < idx.size(); ++k ) idx[k] = k;
						std::partial_sort( idx.begin(), idx.begin() + std::min<size_t>( 8, idx.size() ), idx.end(),
							[]( size_t a, size_t b ) { return g_perfTotal[a] > g_perfTotal[b]; } );
						data += ",\"worst\":[";
						for ( size_t k = 0; k < std::min<size_t>( 8, idx.size() ); ++k )
						{
							if ( k ) data += ",";
							data += "{\"total\":" + std::to_string( g_perfTotal[idx[k]] );
							data += ",\"sim\":" + std::to_string( g_perfSim[idx[k]] );
							data += ",\"render\":" + std::to_string( g_perfRender[idx[k]] );
							data += ",\"tics\":" + I( g_perfTics[idx[k]] ) + "}";
						}
						data += "]";
					}
					data += "}";
					EmitEvent( "perf", data );
					g_perfCapturing = false;
					g_perfTotal.clear(); g_perfSim.clear(); g_perfRender.clear(); g_perfTics.clear(); g_perfTics.clear();
				}
			}
		}
		g_frameStart = now;
		g_ticsThisFrame = 0;
		g_haveFrameStart = true;
		g_haveRenderMark = false;
	}

	// GPU profiler (gl.timers): the render-thread anchors accumulate timer-query results across frames;
	// when a capture completes, its report is waiting here and goes out as a "glperf" event, mirroring
	// the "perf" event above. Poll is cheap (a bool) when nothing is capturing.
	{
		std::string glperf;
		if ( MCP_GLPerf_ReportReady( glperf ) )
			EmitEvent( "glperf", glperf );
	}

	// Per-tic sim profiler (perf.ticprof): the tic anchors accumulate phase times; when the armed
	// tic count completes, the per-tic report goes out as a "ticprof" event.
	{
		std::string ticprof;
		if ( MCP_TicProf_ReportReady( ticprof ) )
			EmitEvent( "ticprof", ticprof );
	}

	// Sim event tracer (sim.trace): completion notice once the trace file is written.
	{
		std::string trace;
		if ( MCP_SimTrace_ReportReady( trace ) )
			EmitEvent( "trace", trace );
	}

	// Sampling profiler (perf.sample). Publish the clock first so a sample taken this frame is
	// stamped with the tic it actually belongs to, then drain any finished run.
	MCP_Sample_PublishTic( InLevel() ? level.time : 0 );
	{
		std::string sample;
		if ( MCP_Sample_ReportReady( sample ) )
			EmitEvent( "sample", sample );
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
			"\"sim.tic\",\"sim.hash\",\"sim.seed\",\"sim.pause\",\"sim.resume\",\"sim.step\",\"sim.cheatat\",\"sim.pauseat\",\"browser.refresh\",\"browser.list\",\"net.hostdiag\",\"net.clients\",\"sim.rngdump\",\"sim.trace\","
			"\"sim.snapshot\",\"sim.restore\",\"state.player\",\"state.actors\",\"input.event\",\"input.axis\",\"input.look\","
			"\"perf.capture\",\"perf.ticprof\",\"perf.counters\",\"net.bandwidth\",\"gl.timers\",\"renderer.info\","
			"\"world.sectors\",\"player.setpos\""
			"],\"events\":[\"out\",\"stepped\",\"perf\",\"glperf\",\"ticprof\",\"trace\"]}" );
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
		std::string scope;
		GetStr( args, "scope", scope );
		std::string body = "{\"leveltime\":" + I( level.time );
		body += ",\"hash\":\"" + I( (long long)StateHash( scope != "world" ) ) + "\"}"; // string to survive 64-bit in JSON
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
	else if ( cmd == "sim.trace" )
	{
		// Arm the sim event tracer: every damage/kill/spawn for the next `tics` tics is
		// recorded and written to args.path when done (a "trace" event reports completion).
		// Diff two runs' files to find the exact event that shifts between them.
		long tics = 35;
		std::string path;
		GetInt( args, "tics", tics );
		if ( !GetStr( args, "path", path ) || path.empty() ) { SendErr( id, "sim.trace requires args.path" ); return; }
		MCP_SimTrace_Arm( (int)tics, path.c_str() );
		SendOk( id, std::string( "{\"tracing\":true,\"tics\":" ) + I( tics ) + "}" );
	}
	else if ( cmd == "sim.rngdump" )
	{
		// Every FRandom stream's (name CRC, index, first state word). Diff two runs at the
		// same tic to name the exact subsystem whose RNG stream diverged first.
		std::string body = "{\"rngs\":[";
		struct Ctx { std::string *out; bool first; } ctx = { &body, true };
		FRandom::StaticEnumStates( []( DWORD crc, unsigned int idx, DWORD u0, void *vctx )
		{
			Ctx *c = (Ctx *)vctx;
			if ( !c->first ) *c->out += ",";
			c->first = false;
			*c->out += "{\"c\":" + I( (long long)crc ) + ",\"i\":" + I( (long long)idx ) + ",\"u\":" + I( (long long)u0 ) + "}";
		}, &ctx );
		body += "],\"leveltime\":" + I( level.time ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "browser.refresh" )
	{
		// Ask both halves of the browser: the LAN broadcast and the federated registry. A
		// driver polls browser.list afterwards until the replies land.
		BROWSER_RefreshListedServers();
		BROWSER_QueryServerRegistry();
		SendOk( id, "{\"refreshing\":true}" );
	}
	else if ( cmd == "browser.list" )
	{
		// The browser's current server list as data, including the two things the console dump
		// leaves out: whether the entry arrived by LAN broadcast, and the country the registry
		// (or the server) reported for it. Those are exactly what an end-to-end check of
		// "does my server appear, and does it appear the right way" has to assert on.
		std::string body = "{\"servers\":[";
		int n = 0;
		for ( ULONG i = 0; i < MAX_BROWSER_SERVERS; ++i )
		{
			if ( !BROWSER_IsActive( i ) )
				continue;
			if ( n ) body += ",";
			std::string name, addr, country;
			JsonEscape( std::string( BROWSER_GetHostName( i ) ? BROWSER_GetHostName( i ) : "" ), name );
			JsonEscape( std::string( BROWSER_GetAddress( i ).ToString() ), addr );
			JsonEscape( std::string( BROWSER_GetCountryCode( i ) ? BROWSER_GetCountryCode( i ) : "" ), country );
			body += "{\"name\":\"" + name + "\",\"address\":\"" + addr + "\"";
			body += ",\"lan\":" + B( BROWSER_IsLAN( i ) );
			// The reported code is blank whenever a server's own lookup failed (it sends XIP), and
			// the browser then resolves the flag from the address itself. Report both: the claim
			// and the flag actually drawn, or an assertion tests the wrong one.
			std::string flag;
			const char *fromindex = NETWORK_GetCountryCodeFromIndex( BROWSER_GetCountryIndex( i ), true );
			JsonEscape( std::string( fromindex ? fromindex : "" ), flag );
			body += ",\"country\":\"" + country + "\"";
			body += ",\"flag\":\"" + flag + "\"";
			body += ",\"countryIndex\":" + I( (long long)BROWSER_GetCountryIndex( i ) );
			// [rc4l] Whether a PLAYER would see this row, since asserting on what the browser holds
			// would miss the collapse entirely.
			body += ",\"listed\":" + B( BROWSER_IsListable( i ) );
			body += ",\"players\":" + I( BROWSER_GetNumPlayers( i ) );
			body += ",\"ping\":" + I( BROWSER_GetPing( i ) ) + "}";
			++n;
		}
		body += "],\"count\":" + I( n ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "net.clients" )
	{
		// [rc4l] How many peers actually got in, which is the only signal a connection that never
		// happened cannot produce.
		if ( NETWORK_GetState() != NETSTATE_SERVER ) { SendErr( id, "net.clients requires a server" ); return; }
		std::string body = "{\"connected\":" + I( (long long)SERVER_CalcNumConnectedClients() );
		body += ",\"players\":" + I( (long long)SERVER_CountPlayers( false ) ) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "ui.continue" )
	{
		// [rc4l] What the Continue button is doing, so an E2E can assert on the decision rather than
		// on pixels. Reports the record and whether the button is on the bar, which are separate
		// facts: a perfectly good record is still hidden while a game is running.
		std::string kind = "none";
		if ( zx::Continue_RecordKind( ) == 1 ) kind = "single";
		else if ( zx::Continue_RecordKind( ) == 2 ) kind = "server";

		std::string escaped;
		JsonEscape( std::string( zx::Continue_RecordTarget( ) ), escaped );

		std::string tip;
		JsonEscape( std::string( zx::Continue_Tooltip( ) ), tip );

		std::string body = "{\"shown\":" + B( zx::Continue_IsShown( ) );
		body += ",\"kind\":\"" + kind + "\"";
		body += ",\"target\":\"" + escaped + "\"";
		body += ",\"tooltip\":\"" + tip + "\"";
		body += ",\"saveExists\":" + B( zx::Continue_DebugSaveExists( ) );
		body += ",\"saveVersion\":" + I( (long long)zx::Continue_DebugSaveVersion( ) );
		body += ",\"minSaveVersion\":" + I( (long long)MINSAVEVER );
		body += ",\"busy\":" + B( zx::Continue_DebugBusy( ) );
		body += ",\"probe\":" + I( (long long)zx::Continue_DebugProbe( ) );
		body += ",\"probeSlot\":" + I( (long long)zx::Continue_DebugProbeSlot( ) );
		body += "}";
		SendOk( id, body );
	}
	else if ( cmd == "net.hostdiag" )
	{
		// [rc4l] The machine-readable half of fua_hostdiag, so a reachability check can be asserted on
		// rather than eyeballed.
		std::string body = "{\"hosting\":" + B( NETWORK_GetState() == NETSTATE_SERVER );
		body += ",\"port\":" + I( (long long)NETWORK_GetLocalPort() );

		bool registryReplied = false;
		if ( NETWORK_GetState() == NETSTATE_SERVER )
		{
			body += ",\"families\":{";
			for ( int fam = 0; fam < 2; ++fam )
			{
				const zx::ListingProof proof = SERVER_SERVERREGISTRY_GetListingProof( fam == 1 );
				const bool verified = ( proof.state == zx::ListingState::ListedVerified );
				const bool possible = ( fam == 0 ) || SERVER_SERVERREGISTRY_HasV6Registry();
				registryReplied = registryReplied || verified;

				std::string describe;
				JsonEscape( std::string( zx::DescribeListing( proof.state ) ), describe );

				body += std::string( fam ? ",\"ipv6\":{" : "\"ipv4\":{" );
				// [rc4l] False means this family has nowhere to announce to, so an absent listing is
				// expected rather than a fault.
				body += "\"possible\":" + B( possible );
				body += ",\"state\":\"" + std::string( HostDiagStateToken( proof.state ) ) + "\"";
				body += ",\"verified\":" + B( verified );
				body += ",\"secondsSinceVerified\":" + I( proof.secondsSinceVerified );
				// [rc4l] Nothing to attend to when the family was never possible.
				body += ",\"needsAttention\":" + B( possible && zx::ListingNeedsAttention( proof.state ) );
				body += ",\"describe\":\"" + describe + "\"}";
			}
			body += "}";
		}

		// [rc4l] `reachable` is null because the verification arrives through the mapping our own
		// announce opened, so it proves nothing about anyone else reaching us.
		body += ",\"registryReplied\":" + B( registryReplied );
		body += ",\"reachable\":null";
		body += ",\"reachableNote\":\"the registry's reply arrives through the NAT mapping our own announce opened, so it does not prove strangers can reach us\"";
		body += "}";
		SendOk( id, body );
	}
	else if ( cmd == "sim.pauseat" )
	{
		// Freeze the sim at an exact leveltime (see g_pauseAtArmed above).
		if ( IsNetInstance() ) { SendErr( id, "pauseat unsupported in a netgame" ); return; }
		long tic = -1;
		GetInt( args, "tic", tic );
		if ( tic < 0 ) { SendErr( id, "sim.pauseat requires args.tic" ); return; }
		g_pauseAtTic = (int)tic;
		g_pauseAtArmed = true;
		SendOk( id, std::string( "{\"scheduled\":true,\"tic\":" ) + I( tic ) + "}" );
	}
	else if ( cmd == "sim.cheatat" )
	{
		// Schedule a cheat to run at an exact leveltime, bypassing the net-command stream.
		// Console cheats (kill monsters = cheat 19/CHT_MASSACRE) land in the demo stream at a
		// wall-clock-dependent maketic, so the tic they execute on jitters between runs --
		// which breaks cross-binary lockstep determinism gates. This calls cht_DoCheat
		// directly at the start of the scheduled tic: same tic, every run, every binary.
		if ( IsNetInstance() ) { SendErr( id, "cheatat unsupported in a netgame" ); return; }
		long tic = -1, cheat = -1;
		GetInt( args, "tic", tic );
		GetInt( args, "cheat", cheat );
		if ( tic < 0 || cheat < 0 ) { SendErr( id, "sim.cheatat requires args.tic and args.cheat (CHT_* id; massacre is 19)" ); return; }
		g_cheatAtTic = (int)tic;
		g_cheatAtCheat = (int)cheat;
		g_cheatAtArmed = true;
		SendOk( id, std::string( "{\"scheduled\":true,\"tic\":" ) + I( tic ) + ",\"cheat\":" + I( cheat ) + "}" );
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
	else if ( cmd == "perf.ticprof" )
	{
		// Arm the per-tic sim profiler: times the next `tics` game tics with a phase split
		// (G_Ticker total incl. net-command execution, P_Ticker, thinkers, effects, specials).
		// The per-tic array arrives asynchronously as a "ticprof" event. Combine with sim.pause +
		// sim.step to dissect a specific tic (e.g. the one that runs "kill monsters").
		long tics = 35;
		GetInt( args, "tics", tics );
		MCP_TicProf_Arm( (int)tics );
		SendOk( id, std::string( "{\"capturing\":true,\"tics\":" ) + I( tics ) + "}" );
	}
	else if ( cmd == "perf.sample" )
	{
		// Flat self-time profile of the game thread: which functions the CPU was actually inside.
		// Runs on its own thread, so this returns at once and the table arrives as a "sample" event.
		// Bracket it with sim.pause/sim.step to attribute the samples to a known tic range, which is
		// the thing an external sampler cannot do.
		if ( MCP_Sample_Running( ) )
		{
			SendErr( id, "a sample run is already in progress" );
			return;
		}

		long seconds = 2, hz = 1000, top = 20, engineOnly = 0, ticMin = 0, ticMax = 0;
		GetInt( args, "seconds", seconds );
		GetInt( args, "hz", hz );
		GetInt( args, "top", top );
		GetInt( args, "engine", engineOnly );
		// Keep only the samples that landed in a leveltime range (0,0 = all of them). Ask
		// perf.ticprof which tics were expensive, then ask here for exactly those.
		GetInt( args, "tic_min", ticMin );
		GetInt( args, "tic_max", ticMax );

		if ( !MCP_Sample_Arm( (double)seconds, (int)hz, engineOnly != 0, (int)top,
			(int)ticMin, (int)ticMax ) )
		{
			SendErr( id, "no in-engine sampler on this platform (macOS and Linux use fuactl's "
				"external sample/perf backend)" );
			return;
		}

		SendOk( id, std::string( "{\"sampling\":true,\"seconds\":" ) + I( seconds ) +
			",\"hz\":" + I( hz ) + ",\"tic_min\":" + I( ticMin ) + ",\"tic_max\":" + I( ticMax ) + "}" );
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
	else if ( cmd == "world.sectors" )
	{
		// Query the level's sectors -- by default only those with a special or damage set;
		// {damaging:1} narrows to floors that actually hurt. Each row carries a point GUARANTEED
		// inside the sector (centroid of its first GL subsector, which is convex) so a driver can
		// navigate there; the bbox-center soundorg lies OUTSIDE ring-shaped sectors.
		if ( !InLevel() )
		{
			SendErr( id, "not in a level" );
			return;
		}
		long damaging = 0, limit = 64;
		GetInt( args, "damaging", damaging );
		GetInt( args, "limit", limit );

		TArray<int> firstss;
		firstss.Resize( numsectors );
		for ( int i = 0; i < numsectors; ++i ) firstss[i] = -1;
		for ( int i = 0; i < numsubsectors; ++i )
		{
			int s = (int)( subsectors[i].sector - sectors );
			if ( s >= 0 && s < numsectors && firstss[s] < 0 ) firstss[s] = i;
		}

		std::string arr = "[";
		int n = 0;
		for ( int i = 0; i < numsectors && n < limit; ++i )
		{
			int dmg = DamageTint_SectorDamage( &sectors[i] );
			if ( damaging ? dmg <= 0 : ( sectors[i].special == 0 && sectors[i].damage == 0 ))
				continue;

			double cx = 0, cy = 0;
			if ( firstss[i] >= 0 )
			{
				subsector_t &ss = subsectors[firstss[i]];
				for ( DWORD k = 0; k < ss.numlines; ++k )
				{
					cx += FIXED2FLOAT( ss.firstline[k].v1->x );
					cy += FIXED2FLOAT( ss.firstline[k].v1->y );
				}
				cx /= ss.numlines;
				cy /= ss.numlines;
			}

			FTexture *ft = TexMan[sectors[i].GetTexture( sector_t::floor )];
			std::string tn;
			JsonEscape( std::string( ft ? ft->Name : "" ), tn );

			if ( n ) arr += ",";
			arr += "{\"i\":" + I( i ) + ",\"special\":" + I( sectors[i].special )
				+ ",\"damage\":" + I( sectors[i].damage ) + ",\"dmg\":" + I( dmg )
				+ ",\"tex\":\"" + tn + "\",\"x\":" + I( (long long)cx ) + ",\"y\":" + I( (long long)cy ) + "}";
			++n;
		}
		arr += "]";
		SendOk( id, "{\"count\":" + I( n ) + ",\"sectors\":" + arr + "}" );
	}
	else if ( cmd == "player.setpos" )
	{
		// Teleport the console player to (x, y) map units, snapped to the floor. Dev tool for
		// driving verification -- single-player instances only (a netgame would desync).
		if ( !InLevel() )
		{
			SendErr( id, "not in a level" );
			return;
		}
		if ( IsNetInstance() )
		{
			SendErr( id, "single-player instances only" );
			return;
		}
		long x = 0, y = 0;
		if ( !GetInt( args, "x", x ) || !GetInt( args, "y", y ))
		{
			SendErr( id, "need x and y (map units)" );
			return;
		}
		AActor *mo = ( consoleplayer >= 0 && consoleplayer < MAXPLAYERS ) ? players[consoleplayer].mo : NULL;
		if ( mo == NULL )
		{
			SendErr( id, "no player pawn" );
			return;
		}
		P_TeleportMove( mo, fixed_t( x ) << FRACBITS, fixed_t( y ) << FRACBITS, mo->z, true );
		mo->z = mo->floorz;
		mo->velx = mo->vely = mo->velz = 0;
		mo->PrevX = mo->x;
		mo->PrevY = mo->y;
		mo->PrevZ = mo->z;
		std::string body = "{\"x\":" + I( (long long)( mo->x >> FRACBITS )) + ",\"y\":" + I( (long long)( mo->y >> FRACBITS ))
			+ ",\"z\":" + I( (long long)( mo->z >> FRACBITS )) + ",\"sector\":" + I( (int)( mo->Sector - sectors )) + "}";
		SendOk( id, body );
	}
	else if ( cmd == "gl.timers" )
	{
		// Arm a GPU-time capture. The per-zone breakdown (opaque scene / translucent / 2D) + whole-frame
		// GPU ms arrive asynchronously as a "glperf" event once `frames` frames are timed, exactly like
		// perf.capture's "perf" event. Warmup frames are discarded to drop scene-change one-time costs.
		long frames = 120, warmup = 3;
		GetInt( args, "frames", frames );
		GetInt( args, "warmup", warmup );
		if ( frames < 1 ) frames = 1;
		MCP_GLPerf_BeginCapture( (int)frames, warmup > 0 ? (int)warmup : 0 );
		SendOk( id, std::string( "{\"capturing\":true,\"frames\":" ) + I( frames ) + ",\"warmup\":" + I( warmup ) + "}" );
	}
	else if ( cmd == "renderer.info" )
	{
		// Renderer identity + whether GL timer queries are usable on this driver (so a caller knows
		// whether gl.timers will return real numbers before arming a capture).
		SendOk( id, MCP_GLPerf_RendererInfo() );
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
