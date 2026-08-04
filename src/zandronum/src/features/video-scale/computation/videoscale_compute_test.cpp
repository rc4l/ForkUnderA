// [rc4l] Tests for videoscale_compute -- faithful against upstream vScaleTable values.
// 100% coverage of the pure scale math.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "videoscale_compute.h"
#include "gtest/gtest.h"

using namespace zx;

namespace
{
	// Convenience wrapper with the common defaults (no custom, no crop, standard 320x200 floor).
	ScaledViewport Scale(int cw, int ch, int mode, float factor = 1.0f)
	{
		return ComputeScaledViewport(cw, ch, mode, factor,
			/*customW*/1600, /*customH*/900, /*customPA*/1.5f,
			/*crop*/false, /*ratio*/0.f,
			VID_SCALE_MIN_WIDTH, VID_SCALE_MIN_HEIGHT);
	}
}

TEST(VideoScale, ModeValidity)
{
	EXPECT_FALSE(VideoScaleModeValid(-1));
	EXPECT_TRUE(VideoScaleModeValid(0));
	EXPECT_TRUE(VideoScaleModeValid(VID_SCALEMODE_COUNT - 1));
	EXPECT_FALSE(VideoScaleModeValid(VID_SCALEMODE_COUNT));
	EXPECT_FALSE(VideoScaleModeValid(99));
}

TEST(VideoScale, NativeReturnsClientSize)
{
	ScaledViewport v = Scale(1920, 1080, VID_SCALEMODE_NATIVE);
	EXPECT_EQ(1920, v.width);
	EXPECT_EQ(1080, v.height);
	EXPECT_FLOAT_EQ(1.0f, v.pixelAspect);
}

TEST(VideoScale, InvalidModeFallsBackToNative)
{
	ScaledViewport v = Scale(1920, 1080, 99);
	EXPECT_EQ(1920, v.width);
	EXPECT_EQ(1080, v.height);
	EXPECT_FLOAT_EQ(1.0f, v.pixelAspect);
}

TEST(VideoScale, FixedModesAreConstant)
{
	ScaledViewport m2 = Scale(1920, 1080, VID_SCALEMODE_640x400);
	EXPECT_EQ(640, m2.width);  EXPECT_EQ(400, m2.height);  EXPECT_FLOAT_EQ(1.2f, m2.pixelAspect);

	ScaledViewport m3 = Scale(1920, 1080, VID_SCALEMODE_960x600);
	EXPECT_EQ(960, m3.width);  EXPECT_EQ(600, m3.height);  EXPECT_FLOAT_EQ(1.2f, m3.pixelAspect);

	ScaledViewport m4 = Scale(1920, 1080, VID_SCALEMODE_1280x800);
	EXPECT_EQ(1280, m4.width); EXPECT_EQ(800, m4.height);  EXPECT_FLOAT_EQ(1.2f, m4.pixelAspect);

	ScaledViewport m6 = Scale(1920, 1080, VID_SCALEMODE_320x200);
	EXPECT_EQ(320, m6.width);  EXPECT_EQ(200, m6.height);  EXPECT_FLOAT_EQ(1.2f, m6.pixelAspect);
}

TEST(VideoScale, MinFillCleanCase)
{
	// 1280x800: 640/1280 == 400/800 == 0.5, so minimum-to-fill is exactly 0.5 -> 640x400.
	ScaledViewport v = Scale(1280, 800, VID_SCALEMODE_MIN_FILL);
	EXPECT_EQ(640, v.width);
	EXPECT_EQ(400, v.height);
	EXPECT_FLOAT_EQ(1.0f, v.pixelAspect);
}

TEST(VideoScale, MinFill1_2CleanCase)
{
	// 1280x800 through the 1.2-biased fill: width 1280*0.5*1.2*1.2=921, height 800*0.5=400.
	ScaledViewport v = Scale(1280, 800, VID_SCALEMODE_MIN_FILL_1_2);
	EXPECT_EQ(921, v.width);
	EXPECT_EQ(400, v.height);
	EXPECT_FLOAT_EQ(1.2f, v.pixelAspect);
}

TEST(VideoScale, CustomModeUsesCustomFields)
{
	ScaledViewport v = Scale(1920, 1080, VID_SCALEMODE_CUSTOM);
	EXPECT_EQ(1600, v.width);
	EXPECT_EQ(900, v.height);
	EXPECT_FLOAT_EQ(1.5f, v.pixelAspect);
}

TEST(VideoScale, ScaleFactorMultiplies)
{
	ScaledViewport half = Scale(1000, 1000, VID_SCALEMODE_NATIVE, 0.5f);
	EXPECT_EQ(500, half.width);
	EXPECT_EQ(500, half.height);
}

TEST(VideoScale, FlooredAtMinimum)
{
	// A tiny client can't drop below the 320x200 floor.
	ScaledViewport v = Scale(100, 100, VID_SCALEMODE_NATIVE);
	EXPECT_EQ(VID_SCALE_MIN_WIDTH, v.width);
	EXPECT_EQ(VID_SCALE_MIN_HEIGHT, v.height);
}

TEST(VideoScale, UiFloorRaisesMinimum)
{
	// When the high-res UI font floor (640x400) is passed as the minimum, it wins.
	ScaledViewport v = ComputeScaledViewport(500, 300, VID_SCALEMODE_NATIVE, 1.0f,
		1600, 900, 1.5f, false, 0.f,
		VID_SCALE_UI_MIN_WIDTH, VID_SCALE_UI_MIN_HEIGHT);
	EXPECT_EQ(640, v.width);
	EXPECT_EQ(400, v.height);
}

TEST(VideoScale, CropAspectCropsToRatio)
{
	// 1920x1080 cropped to 4:3 (1.3333) -> width narrows to 1440, height stays 1080.
	ScaledViewport v = ComputeScaledViewport(1920, 1080, VID_SCALEMODE_NATIVE, 1.0f,
		1600, 900, 1.5f, /*crop*/true, /*ratio*/4.0f / 3.0f,
		VID_SCALE_MIN_WIDTH, VID_SCALE_MIN_HEIGHT);
	EXPECT_EQ(1440, v.width);
	EXPECT_EQ(1080, v.height);
}

TEST(VideoScale, CropAspectTallClientCropsHeight)
{
	// A client taller than the ratio crops height instead: 1000x1000 to 4:3 -> 1000x750.
	ScaledViewport v = ComputeScaledViewport(1000, 1000, VID_SCALEMODE_NATIVE, 1.0f,
		1600, 900, 1.5f, true, 4.0f / 3.0f,
		VID_SCALE_MIN_WIDTH, VID_SCALE_MIN_HEIGHT);
	EXPECT_EQ(1000, v.width);
	EXPECT_EQ(750, v.height);
}

TEST(VideoScale, DegenerateClientDoesNotDivideByZero)
{
	// Zero height must not crash the minimum-to-fill math; result is just the floor. Cover both the
	// plain (MIN_FILL -> MinimumToFill) and 1.2-biased (MIN_FILL_1_2 -> MinimumToFill2) divide guards.
	ScaledViewport v = Scale(0, 0, VID_SCALEMODE_MIN_FILL);
	EXPECT_EQ(VID_SCALE_MIN_WIDTH, v.width);
	EXPECT_EQ(VID_SCALE_MIN_HEIGHT, v.height);

	ScaledViewport v2 = Scale(0, 0, VID_SCALEMODE_MIN_FILL_1_2);
	EXPECT_EQ(VID_SCALE_MIN_WIDTH, v2.width);
	EXPECT_EQ(VID_SCALE_MIN_HEIGHT, v2.height);
}

namespace
{
	ScalePresentPlan Plan(int cw, int ch, int mode, float factor)
	{
		return ComputeScalePresentPlan(cw, ch, mode, factor,
			1600, 900, 1.5f, false, 0.f,
			VID_SCALE_MIN_WIDTH, VID_SCALE_MIN_HEIGHT);
	}
}

TEST(ScalePresent, NativeIsInactiveAndFullClient)
{
	// No scaling requested -> render straight to the backbuffer, no FBO, dest fills the client.
	ScalePresentPlan p = Plan(1920, 1080, VID_SCALEMODE_NATIVE, 1.0f);
	EXPECT_FALSE(p.active);
	EXPECT_EQ(1920, p.virtualWidth);
	EXPECT_EQ(1080, p.virtualHeight);
	EXPECT_EQ(0, p.destX);    EXPECT_EQ(0, p.destY);
	EXPECT_EQ(1920, p.destW); EXPECT_EQ(1080, p.destH);
}

TEST(ScalePresent, HalfFactorRendersSmallUpscalesToFill)
{
	// vid_scalefactor 0.5 -> render at half res, blit stretched across the full client.
	ScalePresentPlan p = Plan(1920, 1080, VID_SCALEMODE_NATIVE, 0.5f);
	EXPECT_TRUE(p.active);
	EXPECT_EQ(960, p.virtualWidth);
	EXPECT_EQ(540, p.virtualHeight);
	EXPECT_EQ(1920, p.destW); EXPECT_EQ(1080, p.destH);
}

TEST(ScalePresent, FixedModeIsActiveAndFills)
{
	// A downscale preset smaller than the client needs the FBO and fills the client.
	ScalePresentPlan p = Plan(1920, 1080, VID_SCALEMODE_1280x800, 1.0f);
	EXPECT_TRUE(p.active);
	EXPECT_EQ(1280, p.virtualWidth);
	EXPECT_EQ(800, p.virtualHeight);
	EXPECT_EQ(1920, p.destW); EXPECT_EQ(1080, p.destH);
}

TEST(ScalePresent, UpscaleFactorIsAlsoActive)
{
	// Supersampling (factor > 1) renders larger than the window; still needs the FBO.
	ScalePresentPlan p = Plan(1280, 720, VID_SCALEMODE_NATIVE, 2.0f);
	EXPECT_TRUE(p.active);
	EXPECT_EQ(2560, p.virtualWidth);
	EXPECT_EQ(1440, p.virtualHeight);
	EXPECT_EQ(1280, p.destW); EXPECT_EQ(720, p.destH);
}

// --- ComputeScaleReconcile -------------------------------------------------
//
// Regression cover for "the whole game renders in the bottom-left corner of a black window":
// the Cocoa backend creates its window at a temporary size, so the client size cached when the
// scale buffer was built is stale from the very first frame, while the render size never changes
// and a render-size-only check therefore never fires.

TEST(VideoScaleReconcile, NothingChangedIsNoWork)
{
	EXPECT_EQ(zx::SCALE_RECONCILE_NONE,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 1280, 800, 1280, 800));
}

TEST(VideoScaleReconcile, RenderSizeChangeResizes)
{
	EXPECT_EQ(zx::SCALE_RECONCILE_RESIZE,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 1280, 800, 640, 400));
}

TEST(VideoScaleReconcile, StaleCachedClientRebuildsEvenWhenRenderSizeIsUnchanged)
{
	// The shipped bug, with its real numbers: the window is 1280x800 and the render size agrees,
	// but the cache still holds the temporary 319x199 window doubled for a 2x display.
	EXPECT_EQ(zx::SCALE_RECONCILE_REBUILD,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 638, 398, 1280, 800));
}

TEST(VideoScaleReconcile, ResizeWinsWhenBothChanged)
{
	EXPECT_EQ(zx::SCALE_RECONCILE_RESIZE,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 638, 398, 640, 400));
}

TEST(VideoScaleReconcile, EachAxisIsCheckedIndependently)
{
	EXPECT_EQ(zx::SCALE_RECONCILE_REBUILD,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 1280, 398, 1280, 800));
	EXPECT_EQ(zx::SCALE_RECONCILE_REBUILD,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 638, 800, 1280, 800));
	EXPECT_EQ(zx::SCALE_RECONCILE_RESIZE,
		zx::ComputeScaleReconcile(1280, 800, 1280, 800, 1280, 800, 1280, 400));
}
