// [Doruk-Aksoy] Tests for continuetext_compute: the pill's label and tooltip for every mode and target, with colour codes stripped.
#include <gtest/gtest.h>

#include "features/continue/computation/continuetext_compute.h"

using namespace zx;

namespace
{

ContinueTextInputs Inputs(ContinueMode mode, ContinueTarget target)
{
	ContinueTextInputs in;
	in.verdict.mode = mode;
	in.verdict.target = target;
	in.serverName = "Friday Night";
	in.address = "10.0.0.5:10666";
	in.mapName = "MAP07";
	in.mapWad = "freedoom2.wad";
	in.hostMap = "MAP03";
	return in;
}

TEST(ContinueText, LabelFollowsTheMode)
{
	EXPECT_EQ("Disconnect", ComputeContinueLabel(ContinueMode::Disconnect));
	EXPECT_EQ("Continue", ComputeContinueLabel(ContinueMode::Continue));
	EXPECT_EQ("", ComputeContinueLabel(ContinueMode::Hidden));
}

TEST(ContinueText, HiddenHasNoTooltip)
{
	EXPECT_EQ("", ComputeContinueTooltip(Inputs(ContinueMode::Hidden, ContinueTarget::None)));
}

TEST(ContinueText, DisconnectSaysWhereEachTargetLands)
{
	EXPECT_EQ("Disconnect from this server\nthen resume your last offline game: MAP07 (freedoom2.wad)",
		ComputeContinueTooltip(Inputs(ContinueMode::Disconnect, ContinueTarget::Offline)));
	EXPECT_EQ("Disconnect from this server\nthen host MAP03 again with your last settings",
		ComputeContinueTooltip(Inputs(ContinueMode::Disconnect, ContinueTarget::Hosted)));
	EXPECT_EQ("Disconnect from this server\nand return to the main menu",
		ComputeContinueTooltip(Inputs(ContinueMode::Disconnect, ContinueTarget::MainMenu)));
}

TEST(ContinueText, ContinueNamesWhatItResumes)
{
	EXPECT_EQ("Rejoin the last server you played on\nFriday Night\n10.0.0.5:10666",
		ComputeContinueTooltip(Inputs(ContinueMode::Continue, ContinueTarget::Server)));
	EXPECT_EQ("Host your last game again\nMAP03, with the same settings",
		ComputeContinueTooltip(Inputs(ContinueMode::Continue, ContinueTarget::Hosted)));
	EXPECT_EQ("Resume your last offline game where you quit\nMAP07 (freedoom2.wad)",
		ComputeContinueTooltip(Inputs(ContinueMode::Continue, ContinueTarget::Offline)));
}

TEST(ContinueText, AServerWithoutANameShowsOnlyItsAddress)
{
	ContinueTextInputs in = Inputs(ContinueMode::Continue, ContinueTarget::Server);
	in.serverName = "";
	EXPECT_EQ("Rejoin the last server you played on\n10.0.0.5:10666", ComputeContinueTooltip(in));

	in.serverName = in.address;
	EXPECT_EQ("Rejoin the last server you played on\n10.0.0.5:10666", ComputeContinueTooltip(in));
}

TEST(ContinueText, AMapWithoutAWadShowsTheBareMap)
{
	ContinueTextInputs in = Inputs(ContinueMode::Continue, ContinueTarget::Offline);
	in.mapWad = "";
	EXPECT_EQ("Resume your last offline game where you quit\nMAP07", ComputeContinueTooltip(in));
}

TEST(ContinueText, ColourCodesInAServerNameNeverReachTheTooltip)
{
	// [Doruk-Aksoy] The record keeps the name as the browser got it, and escapes left in drew as stray glyphs on hover.
	ContinueTextInputs in = Inputs(ContinueMode::Continue, ContinueTarget::Server);
	in.serverName = "\034dGreen \034[Gold]Server\034";
	EXPECT_EQ("Rejoin the last server you played on\nGreen Server\n10.0.0.5:10666", ComputeContinueTooltip(in));
}

} // namespace
