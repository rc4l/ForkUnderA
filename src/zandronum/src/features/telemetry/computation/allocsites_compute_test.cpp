// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/telemetry/computation/allocsites_compute.h"

#include <gtest/gtest.h>

using namespace zx::telemetry;

namespace {

void *P(uintptr_t n) { return (void *)(n * 16); }

// [rc4l] The first count pointers after from whose home slot in mask + 1 slots is home.
std::vector<void *> WithHome(size_t home, size_t mask, size_t count, uintptr_t from = 1)
{
	std::vector<void *> out;
	for (uintptr_t n = from; out.size() < count; n++) if (ComputeAllocHome(P(n), mask) == home) out.push_back(P(n));
	return out;
}

bool Recorded(AllocTable &t, void *p)
{
	const size_t before = t.Live();
	t.Erase(p);
	return t.Live() + 1 == before;
}

} // namespace

TEST(AllocSites, AllocationsAreGroupedByStackMostFirstAndForgottenWhenFreed)
{
	AllocTable t(64, 16);
	void *a[] = { P(1), P(2) };
	void *b[] = { P(3) };
	EXPECT_TRUE(t.Insert(P(100), 10, a, 2));
	EXPECT_TRUE(t.Insert(P(101), 20, a, 2));
	EXPECT_TRUE(t.Insert(P(102), 500, b, 1));
	EXPECT_EQ(t.Live(), 3u);
	double bytes = 0.0;
	std::vector<AllocTable::Site> sites = t.TopSites(8, bytes);
	ASSERT_EQ(sites.size(), 2u);
	EXPECT_EQ(sites[0].count, 2u);
	EXPECT_EQ(sites[0].bytes, 30.0);
	EXPECT_EQ(sites[0].frames, std::vector<void *>(a, a + 2));
	EXPECT_EQ(bytes, 530.0);
	t.Erase(P(101));
	t.Erase(P(999));
	EXPECT_EQ(t.Live(), 2u);
	sites = t.TopSites(1, bytes);
	ASSERT_EQ(sites.size(), 1u);
	EXPECT_EQ(sites[0].bytes, 500.0);
}

TEST(AllocSites, AnAddressMadeAgainReplacesItsRecordAndDepthIsClamped)
{
	AllocTable t(8, 8);
	void *deep[AllocTable::kFrames + 4] = {};
	for (int i = 0; i < AllocTable::kFrames + 4; i++) deep[i] = P(i + 1);
	EXPECT_TRUE(t.Insert(P(50), 1, deep, AllocTable::kFrames + 4));
	EXPECT_TRUE(t.Insert(P(50), 7, deep, -3));
	EXPECT_EQ(t.Live(), 1u);
	double bytes = 0.0;
	const std::vector<AllocTable::Site> sites = t.TopSites(8, bytes);
	ASSERT_EQ(sites.size(), 1u);
	EXPECT_TRUE(sites[0].frames.empty());
	EXPECT_EQ(bytes, 7.0);
	EXPECT_TRUE(t.Insert(P(51), 100, deep, AllocTable::kFrames + 4));
	EXPECT_EQ(t.TopSites(8, bytes)[0].frames.size(), (size_t)AllocTable::kFrames);
}

TEST(AllocSites, FullTablesDropWhatTheyCannotHold)
{
	AllocTable live(4, 8);
	void *s[] = { P(1) };
	for (uintptr_t n = 1; n <= 3; n++) EXPECT_TRUE(live.Insert(P(1000 + n), 1, s, 1));
	EXPECT_FALSE(live.Insert(P(2000), 1, s, 1));
	EXPECT_EQ(live.Dropped(), 1u);
	EXPECT_EQ(live.Live(), 3u);

	AllocTable stacks(64, 4);
	for (uintptr_t n = 1; n <= 3; n++) { void *f[] = { P(n) }; EXPECT_TRUE(stacks.Insert(P(n), 1, f, 1)); }
	for (uintptr_t n = 4; n < 68; n++) { void *f[] = { P(n) }; EXPECT_FALSE(stacks.Insert(P(n), 1, f, 1)); }
	EXPECT_EQ(stacks.Dropped(), 64u);
	void *again[] = { P(2) };
	EXPECT_TRUE(stacks.Insert(P(500), 1, again, 1));
}

TEST(AllocSites, ErasingKeepsEveryLaterRecordOfItsRunFindable)
{
	const size_t mask = 7;
	AllocTable t(8, 8);
	void *s[] = { P(1) };
	// [rc4l] Two homed at slot 0 take 0 and 1, and two homed at 2 take 2 and 3, which stay put when 0 empties.
	const std::vector<void *> zero = WithHome(0, mask, 2);
	const std::vector<void *> two = WithHome(2, mask, 2);
	for (void *p : zero) t.Insert(p, 1, s, 1);
	for (void *p : two) t.Insert(p, 1, s, 1);
	t.Erase(zero[0]);
	EXPECT_TRUE(Recorded(t, zero[1]));
	EXPECT_TRUE(Recorded(t, two[0]));
	EXPECT_TRUE(Recorded(t, two[1]));
	EXPECT_EQ(t.Live(), 0u);
}

TEST(AllocSites, ErasingAcrossTheTablesEndWrapsTheRun)
{
	const size_t mask = 7;
	AllocTable t(8, 8);
	void *s[] = { P(1) };
	// [rc4l] Two homed at slot 7 take 7 and 0, one homed at 0 is pushed to 1, and one homed at 1 to 2.
	const std::vector<void *> seven = WithHome(7, mask, 2);
	const std::vector<void *> zero = WithHome(0, mask, 1);
	const std::vector<void *> one = WithHome(1, mask, 1);
	for (void *p : seven) t.Insert(p, 1, s, 1);
	t.Insert(zero[0], 1, s, 1);
	t.Insert(one[0], 1, s, 1);
	t.Erase(seven[0]);
	EXPECT_TRUE(Recorded(t, seven[1]));
	EXPECT_TRUE(Recorded(t, zero[0]));
	EXPECT_TRUE(Recorded(t, one[0]));

	// [rc4l] One homed at 7 and one homed at 0 sitting in its own slot stay where they are when 7 empties.
	AllocTable u(8, 8);
	const std::vector<void *> home0 = WithHome(0, mask, 1, 5000);
	u.Insert(seven[0], 1, s, 1);
	u.Insert(home0[0], 1, s, 1);
	u.Erase(seven[0]);
	EXPECT_TRUE(Recorded(u, home0[0]));
}

TEST(AllocSites, SitesTieOnCountAreOrderedByBytes)
{
	AllocTable t(16, 16);
	void *a[] = { P(1) };
	void *b[] = { P(2) };
	t.Insert(P(10), 5, a, 1);
	t.Insert(P(11), 50, b, 1);
	double bytes = 0.0;
	const std::vector<AllocTable::Site> sites = t.TopSites(2, bytes);
	ASSERT_EQ(sites.size(), 2u);
	EXPECT_EQ(sites[0].bytes, 50.0);
}

TEST(AllocSites, AStackReadsWithoutTheAllocatorsOwnFrames)
{
	const std::vector<std::string> names = { "zx::telemetry::CaptureStack", "`anonymous namespace'::Allocate", "operator new", "malloc",
		"std::vector<int>::_Emplace_reallocate", "", "FLevelLocals::Load", "G_DoLoadLevel", "D_DoomLoop" };
	EXPECT_EQ(ComputeAllocStackText(names, 3), "? <- FLevelLocals::Load <- G_DoLoadLevel");
	EXPECT_EQ(ComputeAllocStackText(names, 10), "? <- FLevelLocals::Load <- G_DoLoadLevel <- D_DoomLoop");
	EXPECT_EQ(ComputeAllocStackText({ "operator new" }, 3), "(no frames)");
	EXPECT_EQ(ComputeAllocStackText({}, 3), "(no frames)");
}
