// [Doruk-Aksoy] Tests for nativemouse_compute: when the Win32 backend hands the pointer back to Windows.
#include <gtest/gtest.h>

#include "features/mouse-capture/computation/nativemouse_compute.h"

using zx::ComputeWantNativeMouse;
using zx::NativeMouseInputs;

namespace {

NativeMouseInputs Playing(bool fullscreen)
{
	NativeMouseInputs in;
	in.fullscreen = fullscreen;
	return in;
}

TEST(NativeMouse, PlayingInFrontGrabsInBothModes)
{
	EXPECT_FALSE(ComputeWantNativeMouse(Playing(true)));
	EXPECT_FALSE(ComputeWantNativeMouse(Playing(false)));
}

TEST(NativeMouse, FullscreenReleasesWhenAnotherWindowIsInFront)
{
	// [Doruk-Aksoy] The multi-monitor report: after alt-tab the grab stayed, so the pointer was stuck on the game's monitor.
	NativeMouseInputs in = Playing(true);
	in.foreground = false;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, FullscreenReleasesWhenFocusIsLost)
{
	NativeMouseInputs in = Playing(true);
	in.preferNative = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, WindowedReleasesOutOfFrontOrOnRequest)
{
	NativeMouseInputs in = Playing(false);
	in.foreground = false;
	EXPECT_TRUE(ComputeWantNativeMouse(in));

	in = Playing(false);
	in.preferNative = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, FullscreenMenuTakesThePointerOnlyWithMenuMouse)
{
	NativeMouseInputs in = Playing(true);
	in.menuOpen = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));

	in.menuMouse = false;
	EXPECT_FALSE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, WindowedWithoutMouseAimNeverGrabs)
{
	NativeMouseInputs in = Playing(false);
	in.useMouse = false;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, WindowedWaitingForAKeyGrabs)
{
	NativeMouseInputs in = Playing(false);
	in.menuWaitKey = true;
	in.captureInGame = false;
	EXPECT_FALSE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, WindowedReleasesOutsideOfAimingPlay)
{
	NativeMouseInputs in = Playing(false);
	in.captureInGame = false;
	EXPECT_TRUE(ComputeWantNativeMouse(in));

	in = Playing(false);
	in.guiCapture = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));

	in = Playing(false);
	in.paused = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));

	in = Playing(false);
	in.demoPlayback = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

TEST(NativeMouse, AHandsOffInstanceNeverGrabs)
{
	NativeMouseInputs in = Playing(true);
	in.inputLocked = true;
	EXPECT_TRUE(ComputeWantNativeMouse(in));
}

} // namespace
