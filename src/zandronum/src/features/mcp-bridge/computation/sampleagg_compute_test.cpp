// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/mcp-bridge/computation/sampleagg_compute.h"

using namespace zx;

namespace
{

// `n` samples that all landed in the same function.
void Push(std::vector<SampleHit> &out, const char *symbol, const char *dso, int n)
{
	for (int i = 0; i < n; ++i)
		out.push_back(SampleHit(symbol, dso));
}

std::vector<SampleHit> Profile()
{
	// 100 samples: a clear winner, a runner-up, and a driver frame that is not ours.
	std::vector<SampleHit> hits;
	Push(hits, "P_CheckPosition", "forkundera.exe", 50);
	Push(hits, "R_RenderMaskedSegRange", "forkundera.exe", 30);
	Push(hits, "DrvPresentBuffers", "nvoglv64.dll", 20);

	return hits;
}

} // namespace

// ---------------------------------------------------------------- ranking

TEST(RankSamples, PutsTheHottestFunctionFirstAndSharesOutTheWholeRun)
{
	const std::vector<SampleFunc> ranked = RankSamples(Profile(), 0);

	ASSERT_EQ(size_t(3), ranked.size());
	EXPECT_EQ("P_CheckPosition", ranked[0].symbol);
	EXPECT_EQ(50u, ranked[0].count);
	EXPECT_NEAR(50.0, ranked[0].percent, 0.001);
	EXPECT_EQ("R_RenderMaskedSegRange", ranked[1].symbol);
	EXPECT_EQ("DrvPresentBuffers", ranked[2].symbol);
	EXPECT_EQ("nvoglv64.dll", ranked[2].dso);
}

TEST(RankSamples, KeepsPercentagesOfTheWHOLERunWhenTheTableIsTruncated)
{
	// [rc4l] Of the run, not of the kept rows. A top-1 table reading "100%" would say the tail does
	// not exist, which is the one thing a truncated profile must not claim.
	const std::vector<SampleFunc> ranked = RankSamples(Profile(), 1);

	ASSERT_EQ(size_t(1), ranked.size());
	EXPECT_NEAR(50.0, ranked[0].percent, 0.001);
}

TEST(RankSamples, AskingForMoreRowsThanExistIsNotAnError)
{
	EXPECT_EQ(size_t(3), RankSamples(Profile(), 99).size());
}

TEST(RankSamples, CountsAnUnresolvedAddressRatherThanDroppingIt)
{
	// Dropping it would hand its share to everything else and overstate what we did resolve.
	std::vector<SampleHit> hits;
	Push(hits, "P_CheckPosition", "forkundera.exe", 1);
	hits.push_back(SampleHit("", ""));

	const std::vector<SampleFunc> ranked = RankSamples(hits, 0);

	ASSERT_EQ(size_t(2), ranked.size());
	EXPECT_NEAR(50.0, ranked[0].percent, 0.001) << "the named one holds half, not all";

	bool sawUnknown = false;
	for (size_t i = 0; i < ranked.size(); ++i)
	{
		if (ranked[i].symbol == "[unknown]")
			sawUnknown = true;
	}

	EXPECT_TRUE(sawUnknown);
}

TEST(RankSamples, DoesNotFoldTwoModulesSameNamedFunctionsTogether)
{
	// Two DLLs can each export an `init`. Folding them would invent a hot function.
	std::vector<SampleHit> hits;
	Push(hits, "init", "a.dll", 2);
	Push(hits, "init", "b.dll", 1);

	const std::vector<SampleFunc> ranked = RankSamples(hits, 0);

	ASSERT_EQ(size_t(2), ranked.size());
	EXPECT_EQ("a.dll", ranked[0].dso);
	EXPECT_EQ("b.dll", ranked[1].dso);
}

TEST(RankSamples, BreaksATieTheSameWayEveryTimeSoAProfileCanBeDiffed)
{
	std::vector<SampleHit> hits;
	Push(hits, "zzz", "m.dll", 1);
	Push(hits, "aaa", "m.dll", 1);
	// Same name and same count, different module: the last tiebreak.
	Push(hits, "aaa", "a.dll", 1);

	const std::vector<SampleFunc> ranked = RankSamples(hits, 0);

	ASSERT_EQ(size_t(3), ranked.size());
	EXPECT_EQ("aaa", ranked[0].symbol);
	EXPECT_EQ("a.dll", ranked[0].dso);
	EXPECT_EQ("aaa", ranked[1].symbol);
	EXPECT_EQ("m.dll", ranked[1].dso);
	EXPECT_EQ("zzz", ranked[2].symbol);
}

TEST(RankSamples, NothingSampledIsAnEmptyTableRatherThanADivisionByZero)
{
	EXPECT_TRUE(RankSamples(std::vector<SampleHit>(), 0).empty());
}

// ---------------------------------------------------------------- our code only

TEST(OnlyFrom, KeepsOneModuleAndDoesNotCareHowTheLoaderCasedIt)
{
	const std::vector<SampleHit> ours = OnlyFrom(Profile(), "ForkUnderA.exe");

	ASSERT_EQ(size_t(80), ours.size());

	for (size_t i = 0; i < ours.size(); ++i)
		EXPECT_NE("nvoglv64.dll", ours[i].dso);
}

TEST(OnlyFrom, AModuleThatNeverAppearedLeavesNothing)
{
	EXPECT_TRUE(OnlyFrom(Profile(), "notloaded.dll").empty());
}

// ---------------------------------------------------------------- the report

TEST(SampleReportJson, CarriesTheFieldsAReaderExpects)
{
	const std::string json = SampleReportJson(RankSamples(Profile(), 2), 2.5, 100);

	EXPECT_NE(std::string::npos, json.find("\"available\":true"));
	EXPECT_NE(std::string::npos, json.find("\"backend\":\"bridge\""));
	EXPECT_NE(std::string::npos, json.find("\"seconds\":2.500"));
	EXPECT_NE(std::string::npos, json.find("\"samples\":100"));
	EXPECT_NE(std::string::npos, json.find("\"symbol\":\"P_CheckPosition\""));
	EXPECT_NE(std::string::npos, json.find("\"percent\":50.00"));
	EXPECT_NE(std::string::npos, json.find("\"dso\":\"forkundera.exe\""));
	EXPECT_NE(std::string::npos, json.find("\"symbol\":\"R_RenderMaskedSegRange\""));

	// Only two rows were asked for, so the third-hottest is not in the table -- and `samples` still
	// counts the whole run, which is what keeps the percentages honest.
	EXPECT_EQ(std::string::npos, json.find("DrvPresentBuffers"));
}

TEST(SampleReportJson, SurvivesASymbolWithCharactersJsonCaresAbout)
{
	// A C++ symbol carries template arguments and operator names; a quote in one would otherwise end
	// the string early and make the whole report unparseable.
	std::vector<SampleFunc> funcs;
	funcs.push_back(SampleFunc("op\"erator\\<>", "m.dll", 1, 100.0));

	const std::string json = SampleReportJson(funcs, 1.0, 1);

	EXPECT_NE(std::string::npos, json.find("op\\\"erator\\\\<>"));
}

TEST(SampleReportJson, DropsAControlCharacterRatherThanWritingItRaw)
{
	std::vector<SampleFunc> funcs;
	funcs.push_back(SampleFunc(std::string("bad") + '\n' + "name", "m.dll", 1, 100.0));

	const std::string json = SampleReportJson(funcs, 1.0, 1);

	EXPECT_NE(std::string::npos, json.find("badname"));
	EXPECT_EQ(std::string::npos, json.find('\n'));
}

TEST(SampleReportJson, AnEmptyProfileIsStillValidJson)
{
	const std::string json = SampleReportJson(std::vector<SampleFunc>(), 0.0, 0);

	EXPECT_NE(std::string::npos, json.find("\"functions\":[]"));
	EXPECT_NE(std::string::npos, json.find("\"samples\":0"));
}

// ---------------------------------------------------------------- the plain structs

TEST(SampleStructs, DefaultToEmpty)
{
	// The default constructors are what let these sit in a vector before they are filled in.
	const SampleHit hit;
	EXPECT_TRUE(hit.symbol.empty());
	EXPECT_TRUE(hit.dso.empty());

	const SampleFunc func;
	EXPECT_TRUE(func.symbol.empty());
	EXPECT_TRUE(func.dso.empty());
	EXPECT_EQ(0u, func.count);
	EXPECT_NEAR(0.0, func.percent, 0.0001);
}
