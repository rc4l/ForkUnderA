// [Doruk-Aksoy] Tests for joinscreen_compute: the join's stage, its status line, the bar's fill and the screen layout.
#include <gtest/gtest.h>

#include "features/join-screen/computation/joinscreen_compute.h"

using namespace zx;

namespace
{

JoinScreenInputs Client(ConnectionPhase phase)
{
	JoinScreenInputs in;
	in.clientMode = true;
	in.phase = phase;
	return in;
}

TEST(JoinScreen, EachConnectionPhaseMapsToAStage)
{
	EXPECT_EQ(JoinStage::Connecting, ComputeJoinStage(Client(ConnectionPhase::Connecting)));
	EXPECT_EQ(JoinStage::Authenticating, ComputeJoinStage(Client(ConnectionPhase::Authenticating)));
	EXPECT_EQ(JoinStage::ReceivingGame, ComputeJoinStage(Client(ConnectionPhase::RequestingSnapshot)));
	EXPECT_EQ(JoinStage::ReceivingGame, ComputeJoinStage(Client(ConnectionPhase::ReceivingSnapshot)));
}

TEST(JoinScreen, InTheGameOrDroppedShowsNothing)
{
	// [Doruk-Aksoy] A refused connect drops to Disconnected, and the console then has to show the reason.
	EXPECT_EQ(JoinStage::None, ComputeJoinStage(Client(ConnectionPhase::Active)));
	EXPECT_EQ(JoinStage::None, ComputeJoinStage(Client(ConnectionPhase::Disconnected)));
}

TEST(JoinScreen, ABrowserJoinShowsWhileTheFilesReload)
{
	JoinScreenInputs in;
	in.joinInFlight = true;
	EXPECT_EQ(JoinStage::LoadingFiles, ComputeJoinStage(in));

	in.joinInFlight = false;
	EXPECT_EQ(JoinStage::None, ComputeJoinStage(in));
}

TEST(JoinScreen, DisabledShowsNothing)
{
	JoinScreenInputs in = Client(ConnectionPhase::Connecting);
	in.enabled = false;
	in.joinInFlight = true;
	EXPECT_EQ(JoinStage::None, ComputeJoinStage(in));
}

TEST(JoinScreen, ProgressRisesStageByStage)
{
	EXPECT_EQ(0.f, ComputeJoinStageProgress(JoinStage::None));
	EXPECT_LT(ComputeJoinStageProgress(JoinStage::LoadingFiles), ComputeJoinStageProgress(JoinStage::Connecting));
	EXPECT_LT(ComputeJoinStageProgress(JoinStage::Connecting), ComputeJoinStageProgress(JoinStage::Authenticating));
	EXPECT_LT(ComputeJoinStageProgress(JoinStage::Authenticating), ComputeJoinStageProgress(JoinStage::ReceivingGame));
	EXPECT_LT(ComputeJoinStageProgress(JoinStage::ReceivingGame), 1.f);
}

TEST(JoinScreen, StatusNamesTheServerWithoutColourCodes)
{
	EXPECT_EQ("Loading files for Friday DM", ComputeJoinStatus(JoinStage::LoadingFiles, "\034dFriday \034[Gold]DM"));
	EXPECT_EQ("Connecting to Friday DM", ComputeJoinStatus(JoinStage::Connecting, "Friday DM"));
	EXPECT_EQ("Verifying the map with Friday DM", ComputeJoinStatus(JoinStage::Authenticating, "Friday DM"));
	EXPECT_EQ("Receiving the game from Friday DM", ComputeJoinStatus(JoinStage::ReceivingGame, "Friday DM"));
	EXPECT_EQ("", ComputeJoinStatus(JoinStage::None, "Friday DM"));
}

TEST(JoinScreen, AnUnnamedServerIsCalledTheServer)
{
	EXPECT_EQ("Connecting to the server", ComputeJoinStatus(JoinStage::Connecting, ""));
}

TEST(JoinScreen, ProgressGlidesTowardItsTargetAndNeverBack)
{
	EXPECT_FLOAT_EQ(0.1f, ComputeJoinProgressStep(0.f, 1.f, 16));
	EXPECT_FLOAT_EQ(1.f, ComputeJoinProgressStep(0.5f, 1.f, 1000));
	EXPECT_FLOAT_EQ(0.7f, ComputeJoinProgressStep(0.7f, 0.25f, 16));
	EXPECT_FLOAT_EQ(0.2f, ComputeJoinProgressStep(0.2f, 0.9f, -5));
}

TEST(JoinScreen, LayoutStacksInsideABandAtTheBottom)
{
	const JoinScreenLayout l = ComputeJoinScreenLayout(1280, 720, 16);
	EXPECT_EQ(720, l.bandY + l.bandH);
	EXPECT_LT(l.bandY, l.titleY);
	EXPECT_LT(l.titleY, l.statusY);
	EXPECT_LT(l.statusY + 16, l.barY + 1);
	EXPECT_LT(l.barY + l.barH, l.hintY + 1);
	EXPECT_LE(l.hintY + 16, 720);
	EXPECT_EQ(960, l.barW);
	EXPECT_EQ(160, l.barX);
}

TEST(JoinScreen, LayoutSurvivesDegenerateSizes)
{
	const JoinScreenLayout l = ComputeJoinScreenLayout(0, 0, 0);
	EXPECT_GE(l.barW, 1);
	EXPECT_GE(l.barH, 4);
	EXPECT_EQ(0, l.bandY + l.bandH);
}

TEST(JoinScreen, TextGoesDarkOnALightBand)
{
	EXPECT_TRUE(ComputeJoinTextIsDark(200, 210, 220));
	EXPECT_FALSE(ComputeJoinTextIsDark(0, 0, 0));
	EXPECT_FALSE(ComputeJoinTextIsDark(128, 128, 128));
	EXPECT_TRUE(ComputeJoinTextIsDark(129, 129, 129));
}

} // namespace
