// [rc4l] Tests for the instant-replay pure helpers. Exercises every line/branch (the coverage gate
// enforces 100% on *_compute.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/replay/computation/replay_compute.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace zx;

TEST(FrameDue, DisabledWhenFpsNonPositive)
{
	EXPECT_FALSE(ComputeFrameDue(0, 1000000, 0));
	EXPECT_FALSE(ComputeFrameDue(0, 1000000, -5));
}

TEST(FrameDue, DueOnlyAfterInterval)
{
	// 30 fps -> 33333 us interval.
	EXPECT_FALSE(ComputeFrameDue(0, 33332, 30));
	EXPECT_TRUE(ComputeFrameDue(0, 33333, 30));
	EXPECT_TRUE(ComputeFrameDue(0, 50000, 30));
}

TEST(ScaledDims, KeepsNativeWithinCap)
{
	ScaledDims d = ComputeScaledDims(1280, 720, 720);
	EXPECT_EQ(d.w, 1280);
	EXPECT_EQ(d.h, 720);

	// maxH == 0 disables scaling entirely.
	ScaledDims n = ComputeScaledDims(1920, 1080, 0);
	EXPECT_EQ(n.w, 1920);
	EXPECT_EQ(n.h, 1080);
}

TEST(ScaledDims, DownscalesPreservingAspectAndEvenDims)
{
	// 1920x1080 capped to 720 -> 1280x720.
	ScaledDims d = ComputeScaledDims(1920, 1080, 720);
	EXPECT_EQ(d.w, 1280);
	EXPECT_EQ(d.h, 720);

	// An aspect that would yield an odd width must floor to even (1366x768 -> cap 100).
	ScaledDims odd = ComputeScaledDims(1366, 768, 100);
	EXPECT_EQ(odd.h, 100);
	EXPECT_EQ(odd.w % 2, 0);
	EXPECT_EQ(odd.w, 1366 * 100 / 768 & ~1);
}

TEST(ScaledDims, ClampsToMinimumSurface)
{
	// Degenerate tiny source -> floor of 2x2, never zero.
	ScaledDims d = ComputeScaledDims(1, 1, 0);
	EXPECT_EQ(d.w, 2);
	EXPECT_EQ(d.h, 2);
}

TEST(RingCapacity, FramesPlusGuard)
{
	EXPECT_EQ(ComputeRingCapacity(10, 30), 301);
	EXPECT_EQ(ComputeRingCapacity(15, 30), 451);
}

TEST(RingCapacity, ClampsBadInputs)
{
	EXPECT_EQ(ComputeRingCapacity(-3, 30), 1);   // negative duration -> 0 frames + guard
	EXPECT_EQ(ComputeRingCapacity(2, 0), 3);     // fps < 1 clamps to 1
}

TEST(ClipFilename, FormatsZeroPaddedStamp)
{
	char buf[64];
	ClipStamp s{ 2026, 7, 27, 14, 30, 5 };
	ComputeClipFilename(buf, sizeof(buf), s);
	EXPECT_STREQ(buf, "clip-20260727-143005.mp4");
}

TEST(ClipFilename, SafeAgainstBadBuffer)
{
	// Null buffer and non-positive size must not write / must not crash.
	ClipStamp s{ 2026, 1, 1, 0, 0, 0 };
	ComputeClipFilename(nullptr, 64, s);

	char buf[4] = { 'x', 'x', 'x', '\0' };
	ComputeClipFilename(buf, 0, s);
	EXPECT_STREQ(buf, "xxx");   // untouched
}

// 6 packets 1s apart (span 5s), keyframes at indices 0 and 3.
static const int64_t       kT[6] = { 0, 1000000, 2000000, 3000000, 4000000, 5000000 };
static const unsigned char kK[6] = { 1, 0, 0, 1, 0, 0 };

TEST(ClipStartIndex, LastNSecondsFromKeyframeAlignedToWindow)
{
	// window 2s -> cutoff = 5s-2s = 3s; latest keyframe at/before 3s is index 3.
	EXPECT_EQ(ComputeClipStartIndex(kT, kK, 6, 5000000, 2), 3);
	// window 4s -> cutoff = 1s; latest keyframe at/before 1s is index 0 (t=0), not the 3s one.
	EXPECT_EQ(ComputeClipStartIndex(kT, kK, 6, 5000000, 4), 0);
}

TEST(ClipStartIndex, ShortSessionSavesWholeBufferFromFirstKeyframe)
{
	// Session spans only 5s but window is 10s -> no keyframe reaches the cutoff, so start at the
	// first keyframe (index 0). This is the "played less than the capture duration" guard.
	EXPECT_EQ(ComputeClipStartIndex(kT, kK, 6, 5000000, 10), 0);
}

TEST(ClipStartIndex, NothingSaveable)
{
	// Empty buffer, null pointers, and a keyframeless buffer all yield -1 (SaveClip then bails).
	EXPECT_EQ(ComputeClipStartIndex(kT, kK, 0, 5000000, 15), -1);
	EXPECT_EQ(ComputeClipStartIndex(nullptr, kK, 6, 5000000, 15), -1);
	EXPECT_EQ(ComputeClipStartIndex(kT, nullptr, 6, 5000000, 15), -1);

	const unsigned char noKeys[6] = { 0, 0, 0, 0, 0, 0 };
	EXPECT_EQ(ComputeClipStartIndex(kT, noKeys, 6, 5000000, 15), -1);
}
