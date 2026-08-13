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
#include "m_random.h"
#include "zstring.h"
#include "mcp_hud.h"

#include "features/mcp-bridge/computation/mcprpc_compute.h"

#include <string>
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

void MCP_RPC_Tick()
{
	MCP_HUD_BeginFrame(); // snapshot the frame the engine just drew (for screenshots/HUD reads)

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
			"\"sim.snapshot\",\"sim.restore\",\"state.player\",\"state.actors\",\"input.event\""
			"],\"events\":[\"out\",\"stepped\"]}" );
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
		paused = 1;
		SendOk( id, "{\"paused\":1}" );
	}
	else if ( cmd == "sim.resume" )
	{
		paused = 0;
		g_stepping = false;
		SendOk( id, "{\"paused\":0}" );
	}
	else if ( cmd == "sim.step" )
	{
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
	else
	{
		SendErr( id, std::string( "unknown command: " ) + cmd );
	}
}
