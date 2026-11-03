// [Doruk-Aksoy] Tests for oal_voice_compute.
#include "features/openal-sound/computation/oal_voice_compute.h"

#include <gtest/gtest.h>

TEST(OalVoice, AVoiceIsLowerByPriorityThenByBeingFarther)
{
	EXPECT_TRUE(ComputeVoiceLower(1, 10.f, 2, 10.f));
	EXPECT_FALSE(ComputeVoiceLower(3, 99.f, 2, 10.f));
	EXPECT_TRUE(ComputeVoiceLower(2, 20.f, 2, 10.f));
	EXPECT_FALSE(ComputeVoiceLower(2, 10.f, 2, 10.f));   // [Doruk-Aksoy] a tie keeps the one met first
	EXPECT_FALSE(ComputeVoiceLower(2, 5.f, 2, 10.f));
}

// [Doruk-Aksoy] A crowd's thousand sounds each searched every channel for the lowest voice while all were busy, an answer that changes only when a voice starts or stops.
TEST(OalVoice, TheLowestVoiceIsKeptOnlyInsideARunAndUntilAVoiceChanges)
{
	LowestVoiceMemo m;
	const void *got = nullptr;
	int a = 0, b = 0;
	ComputeMemoKeep(m, &a);
	EXPECT_FALSE(ComputeMemoRecall(m, got));   // [Doruk-Aksoy] outside a run nothing is kept
	ComputeMemoBegin(m);
	EXPECT_FALSE(ComputeMemoRecall(m, got));
	ComputeMemoKeep(m, &a);
	ASSERT_TRUE(ComputeMemoRecall(m, got));
	EXPECT_EQ(&a, got);
	ComputeMemoKeep(m, nullptr);   // [Doruk-Aksoy] no voice at all is an answer too
	ASSERT_TRUE(ComputeMemoRecall(m, got));
	EXPECT_EQ(nullptr, got);
	ComputeMemoForget(m);
	EXPECT_FALSE(ComputeMemoRecall(m, got));
	ComputeMemoKeep(m, &b);
	ComputeMemoBegin(m);   // [Doruk-Aksoy] runs nest, and the outer one's end forgets
	ComputeMemoEnd(m);
	ASSERT_TRUE(ComputeMemoRecall(m, got));
	EXPECT_EQ(&b, got);
	ComputeMemoEnd(m);
	EXPECT_FALSE(ComputeMemoRecall(m, got));
	ComputeMemoEnd(m);   // [Doruk-Aksoy] an end without a begin leaves no run open
	ComputeMemoKeep(m, &a);
	EXPECT_FALSE(ComputeMemoRecall(m, got));
}
