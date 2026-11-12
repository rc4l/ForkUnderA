// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/voicechat_hear_compute.h"

#include <gtest/gtest.h>

using namespace zx::voice;

TEST(VoiceHear, AnInvalidSpeakerIsNeverHeard)
{
	EXPECT_TRUE(ComputeVoiceForbiddenToHear(false, false, false, false, false));
	EXPECT_TRUE(ComputeVoiceForbiddenToHear(false, true, false, true, false));
}

TEST(VoiceHear, TheLocalPlayerHearsTheirOwnTest)
{
	EXPECT_FALSE(ComputeVoiceForbiddenToHear(true, true, true, false, true));
}

TEST(VoiceHear, AnIgnoredSpeakerIsNotHeard)
{
	EXPECT_TRUE(ComputeVoiceForbiddenToHear(true, false, true, true, false));
}

TEST(VoiceHear, ALiveListenerDoesNotHearAForbiddenSpectator)
{
	EXPECT_TRUE(ComputeVoiceForbiddenToHear(true, false, false, false, true));
}

TEST(VoiceHear, ASpectatingListenerHearsOtherSpectators)
{
	EXPECT_FALSE(ComputeVoiceForbiddenToHear(true, false, false, true, true));
}

TEST(VoiceHear, AnAllowedSpeakerIsHeard)
{
	EXPECT_FALSE(ComputeVoiceForbiddenToHear(true, false, false, false, false));
}
