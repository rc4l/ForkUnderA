// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/loop.h"
#include "features/handoff/seam.h"
#include "features/handoff/effects.h"
#include "features/handoff/snapshot.h"
#include "features/handoff/gamethread.h"
#include "features/gameloop/computation/threadedtic_compute.h"
#include "features/gamecopy/gamecopy.h"
#include "features/gamecopy/seen.h"
#include "features/levelmesh/projdecals.h"   // [rc4l] CopyProjectedDecalOwners
#include "features/frame/framedriver.h"   // [rc4l] GLDrawsFrame
#include "features/telemetry/telemetry.h"

#include "am_map.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_main.h"
#include "d_net.h"
#include "d_player.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_system.h"
#include "menu/menu.h"
#include "s_sound.h"
#include "mcp_ticprof.h"   // [ForkUnderA] MCP_SimPreTic, MCP_TicProf_TicDone
#include "mcp_simtrace.h"  // [ForkUnderA] MCP_SimTrace_TicDone

// [rc4l] Runs each tic's playsim on the game thread while the main thread draws, wherever ComputeThreadedTic allows.
CVAR(Bool, fua_gamethread, true, CVAR_ARCHIVE)

extern bool singletics;
extern bool advancedemo;
extern gamestate_t wipegamestate;

namespace zx { namespace gameloop {

using namespace zx::handoff;

namespace {

G_TickerPart g_part;    // [rc4l] the tic in flight between its two halves
int g_inFlightTic = 0;  // [rc4l] gametic when it started
int64_t g_startedNs = 0;   // [rc4l] when it started, for handoff/show_delay
int g_threadedTics = 0, g_heldFrames = 0;   // [rc4l] for fua_gamethread_report

void BeforeTic()
{
	ZX_SEAM("handoff/before_tic", Handoff);
	C_Ticker ();
	M_Ticker ();
	I_GetTime (true);
	MCP_SimPreTic (); // [ForkUnderA] tic-scheduled bridge actions (sim.cheatat); no-op unless FUA_MCP_BRIDGE
	// [rc4l] What the frames since the last tic saw, on the live map before the tic reads it, as bots do.
	zx::gamecopy::ApplySeen ();
	TakeSnapshot ();
}

void TickerBegin()
{
	ZX_SEAM("handoff/ticker_begin", Handoff);
	g_part = G_TickerBegin ();
}

void Playsim()
{
	ZX_SEAM("tic/playsim", GameThread);
	G_TickerPlaysim ();
}

void TickerEnd()
{
	ZX_SEAM("handoff/ticker_end", Handoff);
	G_TickerEnd (g_part);
}

void AfterTic()
{
	ZX_SEAM("handoff/after_tic", Handoff);
	MCP_TicProf_TicDone ();
	zx::telemetry::TicMark ();
	MCP_SimTrace_TicDone ();
	gametic++;
	DrainEffects ();
	NetUpdate ();	// check for new console commands
	// [rc4l] The render copy of what this tic left, before any frame draws beside the next.
	zx::gamecopy::FillMap ();
	zx::levelmesh::CopyProjectedDecalOwners ();
}

// [rc4l] Whether the next tic's playsim may run on the game thread now.
bool ThreadedNow()
{
	const ThreadedTicIn in = { fua_gamethread, gamestate == GS_LEVEL, zx::frame::GLDrawsFrame(), wipegamestate != gamestate || D_WipeRunning(),
		automapactive, menuactive != MENU_Off || ConsoleState != c_up, paused != 0, advancedemo, singletics, zx::gamecopy::CopyCheckOn(),
		setmodeneeded || setsizeneeded };
	return ComputeThreadedTic(in);
}

// [rc4l] The tic in flight taken back: its second half and the handoff's work after it, the frame's blend counted from this tic boundary, where its copy starts showing.
void FinishTic()
{
	// [rc4l] From a tic's start on the game thread to its picture and sounds coming out, a tic later than inline.
	static const zx::telemetry::Counter showDelay = zx::telemetry::Register(zx::telemetry::CounterSpec{ "handoff/show_delay", "ms", zx::telemetry::CounterKind::Time, 0 });
	WaitForGameThread ();
	zx::telemetry::Region (showDelay, g_startedNs, zx::telemetry::NowNs ());
	SetGameRunning (false);
	TakeBack ();
	TickerEnd ();
	AfterTic ();
	I_GetTime (true);
	// [rc4l] The tic's sounds start now, on the boundary its picture starts showing on.
	S_RunDeferredSounds ();
	S_UpdateSounds (players[consoleplayer].camera);	// move positional sounds
}

// [rc4l] Inline tics run to catch up, then their sounds moved and the music fed, as TryRunTics does after its tics.
void CatchUp(int tics)
{
	if (tics <= 0) return;
	while (tics-- > 0) RunTic ();
	S_UpdateSounds (players[consoleplayer].camera);	// move positional sounds
}

} // namespace

void RunTic()
{
	BeforeTic ();
	{
		ZX_ZONE ("sim/gticker"); // [rc4l] the whole tic in features/telemetry, which perf.ticprof reads too
		TickerBegin ();
		if (g_part.playsim)
		{
			NoteGameThread ();   // [rc4l] an inline tic's thread is the main one
			SetGameRunning (true);
			Playsim ();
			SetGameRunning (false);
		}
		TickerEnd ();
	}
	AfterTic ();
}

void FinishInFlight()
{
	if (TicInFlight ()) FinishTic ();
}

bool GameThreadFrame()
{
	if (TicInFlight ())
	{
		if (!ComputeTakeBackDue (GameThreadDone (), I_GetTime (false), g_inFlightTic))
		{
			// [rc4l] A frame beside the tic, from the copy and the HUD the handoff recorded, positions held when the tic runs past its boundary.
			if (I_GetTime (false) >= g_inFlightTic + 2) { ZX_COUNT ("handoff/held_frames", "frames", 1); g_heldFrames++; }
			D_DisplayBesideTic ();
			return true;
		}
		FinishTic ();
		D_RunHandoffWork ();
	}
	if (!ThreadedNow ()) return false;

	// [rc4l] The handoff: input and the clock, inline tics to catch up, then the frame's view of the game and the next tic started.
	const int due = Net_TicsDue ();
	CatchUp (due - 1);
	if (due >= 1 && ThreadedNow ())
	{
		D_PrepareDisplay (false);
		BeforeTic ();
		TickerBegin ();
		if (g_part.playsim && ThreadedNow ())
		{
			g_inFlightTic = gametic;
			g_startedNs = zx::telemetry::NowNs ();
			ZX_COUNT ("handoff/threaded_tics", "tics", 1);
			g_threadedTics++;
			SetGameRunning (true);
			StartOnGameThread (Playsim);
			D_DisplayBesideTic ();
			return true;
		}
		if (g_part.playsim)
		{
			NoteGameThread ();
			SetGameRunning (true);
			Playsim ();
			SetGameRunning (false);
		}
		TickerEnd ();
		AfterTic ();
		S_UpdateSounds (players[consoleplayer].camera);	// move positional sounds
	}
	else
	{
		CatchUp (due >= 1 ? 1 : 0);
	}
	D_PrepareDisplay (false);
	D_DisplayBesideTic ();
	return true;
}

int ThreadedTics()
{
	return g_threadedTics;
}

int HeldFrames()
{
	return g_heldFrames;
}

void FrameBegins()
{
	SetMainInHandoff (false);
}

void FrameEnds()
{
	SetMainInHandoff (true);
}

}} // namespace zx::gameloop

// [rc4l] How many tics ran on the game thread and how many frames beside one held positions, since the start.
CCMD(fua_gamethread_report)
{
	Printf ("game thread: %d tics on its own thread, %d frames held\n", zx::gameloop::ThreadedTics (), zx::gameloop::HeldFrames ());
}
