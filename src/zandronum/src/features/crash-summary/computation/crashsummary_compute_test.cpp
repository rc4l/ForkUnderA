// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/crash-summary/computation/crashsummary_compute.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <string>

using zx::crashsummary::ComputeSummaryRing;

namespace {

const int64_t kSecond = 1000000000LL;

// [Doruk-Aksoy] Runs frames of ms each for one second of the ring's clock from startNs, then closes it with the memory given.
int64_t RunSecond(ComputeSummaryRing &ring, int64_t startNs, double ms, double privateBytes, double gpuBytes)
{
	const int64_t step = (int64_t)(ms * 1e6);
	int64_t t = startNs;
	while (!ring.NoteFrame(ms, t)) t += step;
	ring.CloseSecond(privateBytes, gpuBytes);
	return t;
}

} // namespace

// [Doruk-Aksoy] No closed second yet reads as an empty minute.
TEST(ComputeSummaryRing, EmptyBeforeASecond)
{
	ComputeSummaryRing ring;
	EXPECT_FALSE(ring.NoteFrame(7.0, kSecond));
	EXPECT_EQ(ring.Seconds(), 0);
	EXPECT_EQ(ring.Text(), "telemetry: last 0 s, 0 hitches\n");
}

// [Doruk-Aksoy] Each second's mean and max, both memory readings and their change, oldest first.
TEST(ComputeSummaryRing, SecondsAndMemory)
{
	ComputeSummaryRing ring;
	int64_t t = RunSecond(ring, kSecond, 10.0, 1048576.0, 2097152.0);
	RunSecond(ring, t, 20.0, 3145728.0, 2097152.0);
	EXPECT_EQ(ring.Seconds(), 2);
	const std::string text = ring.Text();
	EXPECT_NE(text.find("last 2 s, 0 hitches"), std::string::npos) << text;
	EXPECT_NE(text.find(" 10.0/10.0 20.0/20.0"), std::string::npos) << text;
	EXPECT_NE(text.find("mem/private_bytes: 1.0 MB -> 3.0 MB (+2.0 MB)"), std::string::npos) << text;
	EXPECT_NE(text.find("gpu/process_bytes: 2.0 MB -> 2.0 MB (+0 B)"), std::string::npos) << text;
}

// [Doruk-Aksoy] Frames over the stall line count as hitches, and a platform without a reading leaves its gauge out.
TEST(ComputeSummaryRing, StallsAndMissingReadings)
{
	ComputeSummaryRing ring;
	EXPECT_FALSE(ring.NoteFrame(80.0, kSecond));
	EXPECT_FALSE(ring.NoteFrame(5.0, kSecond + 100000000LL));
	EXPECT_TRUE(ring.NoteFrame(5.0, 2 * kSecond));
	ring.CloseSecond(-1.0, -1.0);
	const std::string text = ring.Text();
	EXPECT_NE(text.find("last 1 s, 1 hitch\n"), std::string::npos) << text;
	EXPECT_NE(text.find(" 30.0/80.0"), std::string::npos) << text;
	EXPECT_EQ(text.find("mem/"), std::string::npos) << text;
	EXPECT_EQ(text.find("gpu/"), std::string::npos) << text;
}

// [Doruk-Aksoy] A stall leaves the count once its second is a minute old.
TEST(ComputeSummaryRing, StallsAgeOut)
{
	ComputeSummaryRing ring;
	EXPECT_FALSE(ring.NoteFrame(90.0, kSecond));
	int64_t t = kSecond;
	while (!ring.NoteFrame(5.0, t)) t += 5000000;
	ring.CloseSecond(0.0, 0.0);
	EXPECT_NE(ring.Text().find("1 hitch\n"), std::string::npos);
	for (int s = 0; s < ComputeSummaryRing::kSeconds; s++) t = RunSecond(ring, t, 5.0, 0.0, 0.0);
	EXPECT_NE(ring.Text().find("0 hitches"), std::string::npos) << ring.Text();
}
