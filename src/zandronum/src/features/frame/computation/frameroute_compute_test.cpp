// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/frameroute_compute.h"

#include <gtest/gtest.h>

using namespace zx::frame;

namespace {

// [rc4l] A GL launch with the backend on, embedded and standalone, its scene up and presenting: what players run.
FrameRouteIn Playing()
{
	FrameRouteIn in;
	in.hasGL = true; in.hasBackend = true; in.backendOn = true; in.sceneUp = true; in.backendPresenting = true;
	in.standalone = true; in.embedded = true; in.checking = false;
	return in;
}

} // namespace

TEST(FrameRoute, PlayersSeeTheBackendAloneAndSaveItsPicture)
{
	const FrameRoute r = ComputeFrameRoute(Playing());
	EXPECT_TRUE(r.backendDraws);
	EXPECT_FALSE(r.glDraws);
	EXPECT_TRUE(r.savePicFromBackend);
}

TEST(FrameRoute, GLDrawsWhenItsOwnPictureIsWanted)
{
	FrameRouteIn in = Playing();
	in.standalone = false;
	EXPECT_TRUE(ComputeFrameRoute(in).glDraws);
	in = Playing();
	in.embedded = false;   // [rc4l] side by side: both windows are live
	EXPECT_TRUE(ComputeFrameRoute(in).glDraws);
	in = Playing();
	in.checking = true;
	EXPECT_TRUE(ComputeFrameRoute(in).glDraws);
	EXPECT_TRUE(ComputeFrameRoute(in).backendDraws);
}

TEST(FrameRoute, GLDrawsUntilTheBackendsSceneCoversItsWindow)
{
	FrameRouteIn in = Playing();
	in.sceneUp = false;
	FrameRoute r = ComputeFrameRoute(in);
	EXPECT_TRUE(r.glDraws);
	EXPECT_FALSE(r.savePicFromBackend);
	// [rc4l] A scene up with no swap chain to show it covered nothing, yet GL drew nothing and presented a stale frame.
	in = Playing();
	in.backendPresenting = false;
	EXPECT_TRUE(ComputeFrameRoute(in).glDraws);
}

TEST(FrameRoute, TheBackendSwitchedOffLeavesGLAlone)
{
	FrameRouteIn in = Playing();
	in.backendOn = false; in.sceneUp = false; in.backendPresenting = false;
	const FrameRoute r = ComputeFrameRoute(in);
	EXPECT_FALSE(r.backendDraws);
	EXPECT_TRUE(r.glDraws);
	EXPECT_FALSE(r.savePicFromBackend);
	in.hasBackend = false; in.backendOn = true;
	EXPECT_FALSE(ComputeFrameRoute(in).backendDraws);
}

TEST(FrameRoute, ALaunchWithNoGLDrawsAndSavesTheBackendOnly)
{
	FrameRouteIn in = Playing();
	in.hasGL = false; in.standalone = false;
	const FrameRoute r = ComputeFrameRoute(in);
	EXPECT_FALSE(r.glDraws);
	EXPECT_TRUE(r.backendDraws);
	EXPECT_TRUE(r.savePicFromBackend);
}

// [rc4l] Each kind draws on its own half of the route, and the save takes the backend's picture exactly when the route says so.
TEST(FrameRoute, EachKindDrawsOnItsHalfOfTheRoute)
{
	FrameRoute r;
	r.glDraws = true; r.backendDraws = false; r.savePicFromBackend = false;
	EXPECT_TRUE(ComputeKindDraws(r, kRendererGL));
	EXPECT_FALSE(ComputeKindDraws(r, kRendererBackend));
	EXPECT_EQ(kRendererGL, ComputeSavePicKind(r));
	r.glDraws = false; r.backendDraws = true; r.savePicFromBackend = true;
	EXPECT_FALSE(ComputeKindDraws(r, kRendererGL));
	EXPECT_TRUE(ComputeKindDraws(r, kRendererBackend));
	EXPECT_EQ(kRendererBackend, ComputeSavePicKind(r));
}
