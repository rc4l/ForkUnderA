// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the channel-overlap rules and the late joiner's looping-sound record.
#include "gtest/gtest.h"
#include "features/sound-overlap/computation/sound_overlap_compute.h"

namespace
{
constexpr int kWeapon = 1;
constexpr int kBody = 4;
constexpr int kSoundA = 10;
constexpr int kSoundB = 11;

int g_actorA, g_actorB;
void *const kActorA = &g_actorA;
void *const kActorB = &g_actorB;

// [Doruk-Aksoy] Starts a loop as the callers do, with the slot and its flags packed.
void Loop(std::vector<ZxLoopingSound> &list, void *actor, int channel, int sound)
{
	ComputeLoopingSoundStarted(list, actor, channel, sound, 1.f, 1.f, true);
}

// [Doruk-Aksoy] CHAN_AUTO overlaps and keeps the flags it came with; an explicit slot is left as asked.
TEST(ComputeSoundChanFlags, AutoOverlaps)
{
	EXPECT_EQ(ComputeSoundChanFlags(ZX_CHAN_AUTO, 0), ZX_CHAN_OVERLAP);
	EXPECT_EQ(ComputeSoundChanFlags(ZX_CHAN_AUTO, ZX_CHAN_LOOP), ZX_CHAN_LOOP | ZX_CHAN_OVERLAP);
	EXPECT_EQ(ComputeSoundChanFlags(kBody, ZX_CHAN_LOOP), ZX_CHAN_LOOP);
	EXPECT_EQ(ComputeSoundChanFlags(kBody, ZX_CHAN_OVERLAP), ZX_CHAN_OVERLAP);
}

// [Doruk-Aksoy] Only a sound with a source and without overlap stops its channel.
TEST(ComputeStartStopsChannel, EveryCase)
{
	EXPECT_TRUE(ComputeStartStopsChannel(0, true));
	EXPECT_FALSE(ComputeStartStopsChannel(ZX_CHAN_OVERLAP, true));
	EXPECT_FALSE(ComputeStartStopsChannel(0, false));
}

// [Doruk-Aksoy] CHAN_NOSTOP blocks only while the same sound already plays, and nothing blocks without it.
TEST(ComputeNoStopBlocks, EveryCase)
{
	EXPECT_TRUE(ComputeNoStopBlocks(ZX_CHAN_NOSTOP, true));
	EXPECT_FALSE(ComputeNoStopBlocks(ZX_CHAN_NOSTOP, false));
	EXPECT_FALSE(ComputeNoStopBlocks(ZX_CHAN_OVERLAP, true));
}

// [Doruk-Aksoy] A_StartSound keeps its slot and every channel flag it shares with the engine, and leaves CHANF_LOOP and what this engine lacks behind.
TEST(ComputeStartSoundChannel, PassesTheChannelFlags)
{
	EXPECT_EQ(ComputeStartSoundChannel(kBody, 0), kBody);
	EXPECT_EQ(ComputeStartSoundChannel(kBody, ZX_CHAN_OVERLAP | ZX_CHAN_NOSTOP), kBody | ZX_CHAN_OVERLAP | ZX_CHAN_NOSTOP);
	EXPECT_EQ(ComputeStartSoundChannel(kWeapon, ZX_CHAN_LISTENERZ | ZX_CHAN_MAYBE_LOCAL | ZX_CHAN_UI | ZX_CHAN_NOPAUSE),
		kWeapon | ZX_CHAN_LISTENERZ | ZX_CHAN_MAYBE_LOCAL | ZX_CHAN_UI | ZX_CHAN_NOPAUSE);
	EXPECT_EQ(ComputeStartSoundChannel(kBody, ZX_CHAN_LOOP | 16384 | 65536), kBody);
	EXPECT_EQ(ComputeStartSoundChannel(12, 0), kBody);
}

// [Doruk-Aksoy] Without overlap a new loop replaces the slot's, as clients replace it.
TEST(ComputeLoopingSoundStarted, PlainLoopReplacesTheSlot)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody, kSoundA);
	Loop(list, kActorA, kBody, kSoundB);
	ASSERT_EQ(list.size(), 1u);
	EXPECT_EQ(list[0].soundId, kSoundB);
}

// [Doruk-Aksoy] Overlapping loops on one slot are all kept, with the flag a resend needs.
TEST(ComputeLoopingSoundStarted, OverlappingLoopsShareTheSlot)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA);
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundB);
	ASSERT_EQ(list.size(), 2u);
	EXPECT_EQ(list[0].channel, kBody);
	EXPECT_EQ(list[0].flags, ZX_CHAN_OVERLAP);
}

// [Doruk-Aksoy] CHAN_AUTO loops overlap too, where the old record kept only the last.
TEST(ComputeLoopingSoundStarted, AutoLoopsShareChannelZero)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, ZX_CHAN_AUTO, kSoundA);
	Loop(list, kActorA, ZX_CHAN_AUTO, kSoundB);
	EXPECT_EQ(list.size(), 2u);
}

// [Doruk-Aksoy] The same overlapping loop started again replaces itself rather than doubling.
TEST(ComputeLoopingSoundStarted, SameLoopReplacesItself)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA);
	ComputeLoopingSoundStarted(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA, 0.5f, 2.f, true);
	ASSERT_EQ(list.size(), 1u);
	EXPECT_FLOAT_EQ(list[0].volume, 0.5f);
	EXPECT_FLOAT_EQ(list[0].attenuation, 2.f);
}

// [Doruk-Aksoy] A one-shot sound stops the slot's loops unless it overlaps them.
TEST(ComputeLoopingSoundStarted, OneShotStopsOnlyWithoutOverlap)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA);
	ComputeLoopingSoundStarted(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA, 1.f, 1.f, false);
	EXPECT_EQ(list.size(), 1u);
	ComputeLoopingSoundStarted(list, kActorA, kBody, kSoundB, 1.f, 1.f, false);
	EXPECT_TRUE(list.empty());
}

// [Doruk-Aksoy] A CHAN_NOSTOP sound clients skip, as the same loop plays, keeps the loop; another sound still replaces it.
TEST(ComputeLoopingSoundStarted, NoStopKeepsTheSameLoop)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody, kSoundA);
	ComputeLoopingSoundStarted(list, kActorA, kBody | ZX_CHAN_NOSTOP, kSoundA, 1.f, 1.f, false);
	ASSERT_EQ(list.size(), 1u);
	ComputeLoopingSoundStarted(list, kActorA, kBody | ZX_CHAN_NOSTOP, kSoundB, 1.f, 1.f, false);
	EXPECT_TRUE(list.empty());
}

// [Doruk-Aksoy] Another actor's or another slot's loops are never touched, and a loop flag in the channel stays out of the slot.
TEST(ComputeLoopingSoundStarted, OtherActorsAndSlotsStay)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kWeapon, kSoundA);
	Loop(list, kActorB, kBody, kSoundA);
	Loop(list, kActorA, kBody | ZX_CHAN_LOOP, kSoundB);
	ASSERT_EQ(list.size(), 3u);
	EXPECT_EQ(list[2].channel, kBody);
	EXPECT_EQ(list[2].flags, ZX_CHAN_LOOP);
}

// [Doruk-Aksoy] Stopping a slot drops every loop on it, overlapped or not, for that actor alone.
TEST(ComputeLoopingSoundStopped, ClearsTheWholeSlot)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA);
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundB);
	Loop(list, kActorA, kWeapon, kSoundA);
	Loop(list, kActorB, kBody, kSoundA);
	ComputeLoopingSoundStopped(list, kActorA, kBody);
	ASSERT_EQ(list.size(), 2u);
	EXPECT_EQ(list[0].channel, kWeapon);
	EXPECT_EQ(list[1].actor, kActorB);
}

// [Doruk-Aksoy] A loop is found by actor, slot and sound together.
TEST(ComputeIsLooping, MatchesAllThree)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody | ZX_CHAN_OVERLAP, kSoundA);
	EXPECT_TRUE(ComputeIsLooping(list, kActorA, kBody, kSoundA));
	EXPECT_FALSE(ComputeIsLooping(list, kActorA, kBody, kSoundB));
	EXPECT_FALSE(ComputeIsLooping(list, kActorA, kWeapon, kSoundA));
	EXPECT_FALSE(ComputeIsLooping(list, kActorB, kBody, kSoundA));
}

// [Doruk-Aksoy] Forgetting an actor keeps the others, and a null actor forgets everyone.
TEST(ComputeForgetLooping, OneActorOrAll)
{
	std::vector<ZxLoopingSound> list;
	Loop(list, kActorA, kBody, kSoundA);
	Loop(list, kActorB, kBody, kSoundA);
	ComputeForgetLooping(list, kActorA);
	ASSERT_EQ(list.size(), 1u);
	EXPECT_EQ(list[0].actor, kActorB);
	ComputeForgetLooping(list, nullptr);
	EXPECT_TRUE(list.empty());
}
}
