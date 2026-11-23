// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/loop.h"
#include "features/dev-mode/devmode.h"   // [Doruk-Aksoy] fua_debug_steptic
#include "features/handoff/seam.h"
#include "features/handoff/effects.h"
#include "features/handoff/snapshot.h"
#include "features/handoff/gamethread.h"
#include "features/gameloop/computation/threadedtic_compute.h"
#include "features/gamecopy/gamecopy.h"
#include "features/gamecopy/seen.h"
#include "features/frame/framedriver.h"   // [rc4l] GLDrawsFrame
#include "features/telemetry/telemetry.h"
#include "features/frame/framestall.h"   // [rc4l] DebugPlaysimStall
#include "features/tic-pacing/pacing.h"   // [Kaori-Chani] fua_tictime_report's measurement

#include <atomic>
#include <cstdint>

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
#include "p_tick.h"   // [rc4l] p_NoInterpolate
#include "r_utility.h"   // [rc4l] r_NoInterpolate
#include "v_font.h"   // [rc4l] V_FontsForTic
#include "mcp_ticprof.h"   // [ForkUnderA] MCP_SimPreTic, MCP_TicProf_TicDone
#include "mcp_simtrace.h"  // [ForkUnderA] MCP_SimTrace_TicDone
#include "features/net-snapshot/ghostactor.h"   // [Kaori-Chani] the client census scope

// [rc4l] Runs each tic's playsim on the game thread while the main thread draws, wherever ComputeThreadedTic allows.
CVAR(Bool, fua_gamethread, true, CVAR_ARCHIVE)

extern bool singletics;
extern bool advancedemo;
extern gamestate_t wipegamestate;

// [Doruk-Aksoy] Hold the game between two tics, for a test that draws one moment at several fractions.
CVAR(Bool, fua_debug_holdtics, false, CVAR_FUA_DEV_MODE)
// [Doruk-Aksoy] One tic let through the hold, for a test that holds each tic in turn.
static int g_stepTics = 0;
DEV_MODE_CCMD(fua_debug_steptic) { g_stepTics = 1; }

namespace zx { namespace gameloop {

using namespace zx::handoff;

namespace {

G_TickerPart g_part;    // [rc4l] the tic in flight between its two halves
int g_startedAt = 0;  // [rc4l] the clock tic it started in, which the take-back and the frame's blend are both counted on
int64_t g_startedNs = 0;   // [rc4l] when it started, for handoff/show_delay
int g_threadedTics = 0, g_heldFrames = 0;   // [rc4l] for fua_gamethread_report
std::atomic<int64_t> g_lastPlaysimNs(0);   // [rc4l] the last tic's playsim, whichever thread ran it, for the inline catch-up's budget

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
	p_NoInterpolate = r_NoInterpolate;
	V_FontsForTic ();
}

// [rc4l] The playsim's tic, and when a frame draws it the render copy's scan at its end.
void PlaysimTic(bool drawn)
{
	ZX_SEAM("tic/playsim", GameThread);
	const int64_t start = zx::telemetry::NowNs ();
	zx::frame::DebugPlaysimStall ();
	// [Kaori-Chani] features/net-snapshot: the ticker's cost for the client's ghostframe line, the
	// playsim being the part of this tree's three-way G_Ticker the figure is about.
	{
		const zx::netsnapshot::CensusScope gtick( zx::netsnapshot::SNAPCENSUS_GTICKER );
		G_TickerPlaysim ();
	}
	g_lastPlaysimNs.store (zx::telemetry::NowNs () - start, std::memory_order_relaxed);
	// [rc4l] Found while the frames still draw, so the handoff takes only what changed.
	if (drawn) zx::gamecopy::ScanAtTicEnd ();
}

// [rc4l] A tic on the game thread, which a frame always draws.
void Playsim()
{
	PlaysimTic (true);
}

void TickerEnd()
{
	ZX_SEAM("handoff/ticker_end", Handoff);
	// [rc4l] Whether the tic moved anything, given to the renderer only now: written from the game thread it froze a frame drawn beside the tic at the full blend, and the view lurched a tic and stepped back (queue #51).
	if (g_part.playsim) r_NoInterpolate = p_NoInterpolate;
	G_TickerEnd (g_part);
}

void AfterTic(bool drawn)
{
	ZX_SEAM("handoff/after_tic", Handoff);
	MCP_TicProf_TicDone ();
	zx::telemetry::TicMark ();
	MCP_SimTrace_TicDone ();
	gametic++;
	DrainEffects ();
	// [Kaori-Chani] features/net-snapshot: the per-tic NetUpdate, which the ghostframe line
	// reports beside the parse so a client's send cost reads apart from its receive.
	{
		const zx::netsnapshot::CensusScope netupd( zx::netsnapshot::SNAPCENSUS_NETUPDATE );
		NetUpdate ();	// check for new console commands
	}
	// [rc4l] The render copy of what this tic left, before any frame draws beside the next; a tic no frame draws leaves it to the next, which takes every change since.
	if (drawn) zx::gamecopy::FillMap ();
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
	AfterTic (true);
	I_GetTime (true);
	// [rc4l] The tic's sounds start now, on the boundary its picture starts showing on.
	S_RunDeferredSounds ();
	S_UpdateSounds (players[consoleplayer].camera);	// move positional sounds
}

// [rc4l] Inline tics run to catch up, then their sounds moved and the music fed, as TryRunTics does after its tics.
void CatchUp(int tics)
{
	if (tics <= 0) return;
	while (tics-- > 0) RunTic (ComputeTicDrawn (tics));
	S_UpdateSounds (players[consoleplayer].camera);	// move positional sounds
}

} // namespace

void RunTic(bool drawn)
{
	BeforeTic ();
	{
		ZX_ZONE ("sim/gticker"); // [rc4l] the whole tic in features/telemetry, which perf.ticprof reads too
		TickerBegin ();
		if (g_part.playsim)
		{
			NoteGameThread ();   // [rc4l] an inline tic's thread is the main one
			SetGameRunning (true);
			PlaysimTic (drawn);
			SetGameRunning (false);
		}
		TickerEnd ();
	}
	AfterTic (drawn);
}

void FinishInFlight()
{
	if (TicInFlight ()) FinishTic ();
}

bool GameThreadFrame()
{
	// [Kaori-Chani] Measured from here as well as from TryRunTics, which a threaded frame returns
	// before reaching, so the report covers the loop a default client actually runs.
	zx::ticpacing::Tick ((unsigned int)gametic);

	if (TicInFlight ())
	{
		ServeAsks ();
		const bool finished = GameThreadDone ();
		const int now = I_GetTime (false);
		if (!ComputeTakeBackDue (finished, now, g_startedAt))
		{
			// [rc4l] A frame beside the tic, from the copy and the HUD the handoff recorded, positions held when the tic runs past its boundary.
			if (ComputeHeldFrame (finished, now, g_startedAt)) { ZX_COUNT ("handoff/held_frames", "frames", 1); g_heldFrames++; }
			D_DisplayBesideTic ();
			return true;
		}
		FinishTic ();
		D_RunHandoffWork ();
	}
	if (!ThreadedNow ()) return false;

	// [rc4l] The handoff: input and the clock, the catch-up tics that fit kInlineCatchUpMs inline, then the frame's view of the game and the next tic started; the rest wait for the game thread, a handoff each, with frames drawn beside them.
	// [Doruk-Aksoy] Fua_debug_holdtics runs no tic while frames go on, the last tic's motion kept, unlike a pause.
	const int ticsDue = Net_TicsDue ();
	const int due = !fua_debug_holdtics ? ticsDue : g_stepTics > 0 && ticsDue > 0 ? 1 : 0;
	if (fua_debug_holdtics && due > 0) g_stepTics = 0;
	ZX_COUNT ("handoff/backlog_tics", "tics", due);
	const int inlineTics = ComputeInlineCatchUp (due - 1, g_lastPlaysimNs.load (std::memory_order_relaxed) / 1e6, kInlineCatchUpMs);
	ZX_COUNT ("handoff/inline_catchup", "tics", inlineTics);
	CatchUp (inlineTics);
	if (due >= 1 && ThreadedNow ())
	{
		D_PrepareDisplay (false);
		BeforeTic ();
		TickerBegin ();
		if (g_part.playsim && ThreadedNow ())
		{
			g_startedAt = I_GetTime (false);
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
		AfterTic (true);
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

bool TicsRunThreaded()
{
	return ThreadedNow ();
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
DEV_MODE_CCMD(fua_gamethread_report)
{
	Printf ("game thread: %d tics on its own thread, %d frames held, %llu calls asked of the main thread\n", zx::gameloop::ThreadedTics (),
		zx::gameloop::HeldFrames (), (unsigned long long)zx::handoff::AsksServed ());
}
