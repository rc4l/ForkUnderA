// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gameloop/computation/tickerpause_compute.h"

#include <gtest/gtest.h>

using zx::gameloop::ComputeMenuPausesTicker;
using zx::gameloop::TickerPauseIn;

namespace {

// [rc4l] A single-player game with the console down, past its first tic.
const TickerPauseIn kConsole = { false, false, false, true, false, false, false };

} // namespace

TEST(TickerPause, AMenuOrTheConsolePausesASinglePlayerTic)
{
	EXPECT_TRUE(ComputeMenuPausesTicker(kConsole));
	TickerPauseIn in = kConsole;
	in.consoleDown = false; in.menuPauses = true;
	EXPECT_TRUE(ComputeMenuPausesTicker(in));
	in.menuPauses = false;
	EXPECT_FALSE(ComputeMenuPausesTicker(in));
}

// [rc4l] A client, the title map, a demo, a player before its first tic and a wipe still to come all keep the tic running.
TEST(TickerPause, EachExceptionKeepsTheTicRunning)
{
	TickerPauseIn in = kConsole; in.client = true; EXPECT_FALSE(ComputeMenuPausesTicker(in));
	in = kConsole; in.titleLevel = true; EXPECT_FALSE(ComputeMenuPausesTicker(in));
	in = kConsole; in.demo = true; EXPECT_FALSE(ComputeMenuPausesTicker(in));
	in = kConsole; in.viewzUnset = true; EXPECT_FALSE(ComputeMenuPausesTicker(in));
	in = kConsole; in.wipePending = true; EXPECT_FALSE(ComputeMenuPausesTicker(in));
}
