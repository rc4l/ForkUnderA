// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/addon-catalogue/computation/livespick_compute.h"

using zx::HostGameMode;
using zx::LivesControl;
using zx::LivesCvars;
using zx::LivesFor;
using zx::LivesShape;

namespace
{

// "nothing chosen yet", which is what every entry starts on.
const int kUnchosen = -1;

std::string ValueOf(const std::vector<std::pair<std::string, std::string> > &cvars,
	const std::string &name)
{
	for (size_t i = 0; i < cvars.size(); ++i)
	{
		if (cvars[i].first == name)
			return cvars[i].second;
	}
	return std::string();
}

bool Sets(const std::vector<std::pair<std::string, std::string> > &cvars, const std::string &name)
{
	return !ValueOf(cvars, name).empty();
}

} // namespace

// ------------------------------------------------------------ which modes have lives at all

TEST(LivesPick, OnlyTheFourModesThatHonourMaxLivesOfferIt)
{
	// [rc4l] Straight off wadsrc/static/gamemode.txt: Survival, Invasion, LMS and TeamLMS carry
	// USEMAXLIVES. Cooperative is here too, but for the opposite reason -- it has no lives itself,
	// and the control's job there is to switch it to Survival.
	EXPECT_TRUE(LivesFor(HostGameMode::Cooperative, kUnchosen, 0, 5).applies);
	EXPECT_TRUE(LivesFor(HostGameMode::Survival, kUnchosen, 3, 5).applies);
	EXPECT_TRUE(LivesFor(HostGameMode::Invasion, kUnchosen, 0, 5).applies);
	EXPECT_TRUE(LivesFor(HostGameMode::LastManStanding, kUnchosen, 1, 5).applies);
	EXPECT_TRUE(LivesFor(HostGameMode::TeamLastManStanding, kUnchosen, 1, 5).applies);

	EXPECT_FALSE(LivesFor(HostGameMode::Deathmatch, kUnchosen, 0, 5).applies);
	EXPECT_FALSE(LivesFor(HostGameMode::TeamDeathmatch, kUnchosen, 0, 5).applies);
	EXPECT_FALSE(LivesFor(HostGameMode::Duel, kUnchosen, 0, 5).applies);
	EXPECT_FALSE(LivesFor(HostGameMode::CaptureTheFlag, kUnchosen, 0, 5).applies);
	EXPECT_FALSE(LivesFor(HostGameMode::Unknown, kUnchosen, 0, 5).applies);
}

TEST(LivesPick, AModeWithoutLivesSaysWhyRatherThanVanishing)
{
	// The caller greys the row in place. A control that disappears teaches nothing and moves
	// everything under it.
	const LivesControl c = LivesFor(HostGameMode::Deathmatch, kUnchosen, 0, 5);

	EXPECT_FALSE(c.applies);
	EXPECT_FALSE(c.reason.empty());
}

TEST(LivesPick, AnEntryThatSaysNothingGetsNoControl)
{
	// Every entry written before the field existed. Offering a lives slider on a gamemode we cannot
	// name would be guessing at what the cfg does.
	const LivesControl c = LivesFor(HostGameMode::Unknown, kUnchosen, 0, 5);

	EXPECT_FALSE(c.applies);
	EXPECT_EQ(LivesShape::None, c.shape);
}

TEST(LivesPick, AZeroCeilingIsHowAPackOptsOut)
{
	// Bosses from Hell is built around one arrangement and says so. A control offering to change it
	// would be offering a mistake.
	const LivesControl c = LivesFor(HostGameMode::Cooperative, kUnchosen, 0, 0);

	EXPECT_FALSE(c.applies);
	EXPECT_FALSE(c.reason.empty());
}

// ------------------------------------------------------------ the three shapes

TEST(LivesPick, CooperativeHasAnUnlimitedStopAtZero)
{
	const LivesControl c = LivesFor(HostGameMode::Cooperative, kUnchosen, 0, 5);

	EXPECT_EQ(LivesShape::CoopSurvival, c.shape);
	EXPECT_EQ(0, c.min);
	EXPECT_EQ(5, c.max);
	EXPECT_TRUE(c.unlimited);
}

TEST(LivesPick, InvasionHasARealUnlimitedStopToo)
{
	const LivesControl c = LivesFor(HostGameMode::Invasion, kUnchosen, 0, 5);

	EXPECT_EQ(LivesShape::Invasion, c.shape);
	EXPECT_EQ(0, c.min);
	EXPECT_TRUE(c.unlimited);
}

TEST(LivesPick, ARoundsModeStartsAtOneBecauseZeroWouldMeanOne)
{
	// [rc4l] GAMEMODE_GetMaxLives returns 1 for a zero in Survival and both LMS modes, so offering a
	// zero would offer a stop that silently means the stop next to it.
	const LivesControl c = LivesFor(HostGameMode::LastManStanding, kUnchosen, 1, 5);

	EXPECT_EQ(LivesShape::Rounds, c.shape);
	EXPECT_EQ(1, c.min);
	EXPECT_FALSE(c.unlimited);
}

TEST(LivesPick, ARoundsModeAskedForUnlimitedGetsTheFloor)
{
	// An entry carrying a default of 0 from a co-op sibling, or a preference left over from one.
	// Clamped rather than passed through, since the engine would read it as one life anyway and the
	// panel should not claim otherwise.
	const LivesControl c = LivesFor(HostGameMode::Survival, 0, 0, 5);

	EXPECT_EQ(1, c.value);
	EXPECT_FALSE(c.unlimited);
}

// ------------------------------------------------------------ the value

TEST(LivesPick, NothingChosenTakesTheEntrysDefault)
{
	EXPECT_EQ(3, LivesFor(HostGameMode::Invasion, kUnchosen, 3, 5).value);
	EXPECT_EQ(0, LivesFor(HostGameMode::Cooperative, kUnchosen, 0, 5).value);
}

TEST(LivesPick, AChoiceBeatsTheDefault)
{
	EXPECT_EQ(2, LivesFor(HostGameMode::Invasion, 2, 3, 5).value);
}

TEST(LivesPick, AChoiceTooBigForThisEntryIsClamped)
{
	// A number is not a named option: when it no longer fits there is an obvious nearest answer, so
	// clamping beats falling back to a default the player did not ask for.
	EXPECT_EQ(5, LivesFor(HostGameMode::Invasion, 99, 3, 5).value);
}

TEST(LivesPick, AnyNegativeMeansUnchosenRatherThanTooSmall)
{
	// One rule rather than a special case for -1. A negative life count is not a thing anybody can
	// have meant, so reading them all as "no preference" beats clamping some of them to zero and
	// silently turning a stored -3 into a request for unlimited.
	EXPECT_EQ(3, LivesFor(HostGameMode::Invasion, -1, 3, 5).value);
	EXPECT_EQ(3, LivesFor(HostGameMode::Invasion, -5, 3, 5).value);
}

TEST(LivesPick, TheValueIsAlwaysInsideTheRange)
{
	// Swept, because the caller draws a knob at value/max and an out-of-range value puts it outside
	// its own track.
	const HostGameMode modes[] = {
		HostGameMode::Cooperative, HostGameMode::Survival, HostGameMode::Invasion,
		HostGameMode::LastManStanding, HostGameMode::TeamLastManStanding,
	};

	for (int m = 0; m < 5; ++m)
	{
		for (int wanted = -3; wanted <= 12; ++wanted)
		{
			for (int max = 1; max <= 6; ++max)
			{
				const LivesControl c = LivesFor(modes[m], wanted, 2, max);
				ASSERT_TRUE(c.applies);
				EXPECT_GE(c.value, c.min) << "mode " << m << " wanted " << wanted << " max " << max;
				EXPECT_LE(c.value, c.max) << "mode " << m << " wanted " << wanted << " max " << max;
				EXPECT_EQ(c.unlimited, c.value == 0);
			}
		}
	}
}

// ------------------------------------------------------------ what it sets

TEST(LivesCvars, CooperativeAtZeroIsTheCooperativeGamemode)
{
	// [rc4l] THE bug this exists to make unrepresentable. Setting sv_maxlives alone in Cooperative
	// does nothing at all, because Cooperative does not honour it.
	const LivesControl c = LivesFor(HostGameMode::Cooperative, 0, 0, 5);
	const std::vector<std::pair<std::string, std::string> > cvars = LivesCvars(c);

	EXPECT_EQ("false", ValueOf(cvars, "survival"));
	EXPECT_EQ("true", ValueOf(cvars, "cooperative"));
}

TEST(LivesCvars, CooperativeWithLivesBecomesSurvival)
{
	const LivesControl c = LivesFor(HostGameMode::Cooperative, 2, 0, 5);
	const std::vector<std::pair<std::string, std::string> > cvars = LivesCvars(c);

	EXPECT_EQ("true", ValueOf(cvars, "survival")) << "the gamemode has to change, not just the count";
	EXPECT_EQ("2", ValueOf(cvars, "sv_maxlives"));
}

TEST(LivesCvars, BothSidesOfTheCoopSwitchAreAlwaysWritten)
{
	// The entry's own cfg has already set one of them. A control that only ever turned survival on
	// could never turn it back off.
	EXPECT_TRUE(Sets(LivesCvars(LivesFor(HostGameMode::Cooperative, 0, 0, 5)), "survival"));
	EXPECT_TRUE(Sets(LivesCvars(LivesFor(HostGameMode::Cooperative, 3, 0, 5)), "survival"));
}

TEST(LivesCvars, AModeThatIsAlreadyALivesModeOnlySetsTheCount)
{
	// Writing `survival true` under Invasion would change the gamemode out from under the pack.
	const std::vector<std::pair<std::string, std::string> > cvars =
		LivesCvars(LivesFor(HostGameMode::Invasion, 3, 0, 5));

	EXPECT_EQ("3", ValueOf(cvars, "sv_maxlives"));
	EXPECT_FALSE(Sets(cvars, "survival"));
	EXPECT_FALSE(Sets(cvars, "cooperative"));
}

TEST(LivesCvars, InvasionUnlimitedIsAZeroRatherThanAModeChange)
{
	const std::vector<std::pair<std::string, std::string> > cvars =
		LivesCvars(LivesFor(HostGameMode::Invasion, 0, 0, 5));

	EXPECT_EQ("0", ValueOf(cvars, "sv_maxlives"));
	EXPECT_FALSE(Sets(cvars, "cooperative"));
}

TEST(LivesCvars, AControlThatDoesNotApplySetsNothing)
{
	EXPECT_TRUE(LivesCvars(LivesFor(HostGameMode::Deathmatch, kUnchosen, 0, 5)).empty());
	EXPECT_TRUE(LivesCvars(LivesFor(HostGameMode::Cooperative, kUnchosen, 0, 0)).empty());
}

TEST(LivesPick, AControlWithNoShapeWritesNothingEvenIfItClaimsToApply)
{
	// [rc4l] Built by hand, because LivesFor cannot produce it: `applies` is only ever set once a
	// shape has been chosen. The guard is here for the caller that assembles a control itself, and
	// the answer is silence -- there is no cvar that means "lives, of no particular kind".
	zx::LivesControl control;
	control.shape = zx::LivesShape::None;
	control.applies = true;
	control.value = 3;

	EXPECT_TRUE(zx::LivesCvars(control).empty());
}
