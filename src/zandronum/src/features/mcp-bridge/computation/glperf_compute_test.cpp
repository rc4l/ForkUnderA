// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/mcp-bridge/computation/glperf_compute.h"

using namespace zx::mcp;

// ---- GLZoneName ------------------------------------------------------------

TEST(GlPerf, ZoneNamesAreStableAndBounded)
{
	EXPECT_STREQ(GLZoneName(GLZONE_SCENE), "scene");
	EXPECT_STREQ(GLZoneName(GLZONE_TRANSLUCENT), "translucent");
	EXPECT_STREQ(GLZoneName(GLZONE_PORTALS), "portals");
	EXPECT_STREQ(GLZoneName(GLZONE_POSTPROCESS), "postprocess");
	EXPECT_STREQ(GLZoneName(GLZONE_HUD2D), "hud2d");
	EXPECT_STREQ(GLZoneName(-1), "?");           // below range
	EXPECT_STREQ(GLZoneName(GLZONE_COUNT), "?"); // at/above range
}

// ---- Ring bookkeeping ------------------------------------------------------

TEST(GlPerf, RingSlotWrapsAndGuardsBadSize)
{
	EXPECT_EQ(GLRingSlot(0, 4), 0);
	EXPECT_EQ(GLRingSlot(3, 4), 3);
	EXPECT_EQ(GLRingSlot(4, 4), 0); // wraps
	EXPECT_EQ(GLRingSlot(9, 4), 1);
	EXPECT_EQ(GLRingSlot(5, 0), 0); // ringSize<=0 -> single slot, always 0
	EXPECT_EQ(GLRingSlot(5, -3), 0);
}

TEST(GlPerf, RingReadyOnlyAfterFirstFullCycle)
{
	EXPECT_FALSE(GLRingReady(0, 4));
	EXPECT_FALSE(GLRingReady(3, 4));
	EXPECT_TRUE(GLRingReady(4, 4));
	EXPECT_TRUE(GLRingReady(100, 4));
	EXPECT_TRUE(GLRingReady(1, 0)); // degenerate ringSize clamps to 1 -> ready at frame 1
}

// ---- Timestamp arithmetic --------------------------------------------------

TEST(GlPerf, NanosToMs)
{
	EXPECT_DOUBLE_EQ(GLNanosToMs(0), 0.0);
	EXPECT_DOUBLE_EQ(GLNanosToMs(1000000), 1.0);      // 1e6 ns = 1 ms
	EXPECT_DOUBLE_EQ(GLNanosToMs(2500000), 2.5);
}

TEST(GlPerf, SpanValidityRejectsZeroAndReversed)
{
	EXPECT_TRUE(GLSpanValid(1000, 2000));
	EXPECT_TRUE(GLSpanValid(5000, 5000));  // zero-length but both recorded = valid (0ms)
	EXPECT_FALSE(GLSpanValid(0, 2000));    // begin never issued
	EXPECT_FALSE(GLSpanValid(2000, 0));    // end never issued
	EXPECT_FALSE(GLSpanValid(0, 0));       // neither issued
	EXPECT_FALSE(GLSpanValid(3000, 2000)); // time ran backwards (wrapped counter)
}

TEST(GlPerf, SpanMsIsZeroWhenInvalid)
{
	EXPECT_DOUBLE_EQ(GLSpanMs(1000000, 4000000), 3.0); // 3 ms
	EXPECT_DOUBLE_EQ(GLSpanMs(0, 4000000), 0.0);       // invalid -> 0, never negative
	EXPECT_DOUBLE_EQ(GLSpanMs(4000000, 1000000), 0.0); // reversed -> 0
}

// ---- Timing verdict --------------------------------------------------------

TEST(GlPerf, AssessTimingNoFrames)
{
	GLTimingVerdict v = GLAssessTiming({});
	EXPECT_FALSE(v.available);
	EXPECT_EQ(v.note, "no frames captured");
}

TEST(GlPerf, AssessTimingAllZeroIsUnavailable)
{
	GLTimingVerdict v = GLAssessTiming({0.0, 0.0, 0.0});
	EXPECT_FALSE(v.available);
	EXPECT_NE(v.note.find("GL_TIMESTAMP"), std::string::npos);
}

TEST(GlPerf, AssessTimingAnyNonzeroIsAvailable)
{
	GLTimingVerdict v = GLAssessTiming({0.0, 0.0, 4.2});
	EXPECT_TRUE(v.available);
	EXPECT_TRUE(v.note.empty());
}

// ---- Report assembly -------------------------------------------------------

static std::vector<std::vector<double>> zonesWith(int idx, std::vector<double> samples)
{
	std::vector<std::vector<double>> z(GLZONE_COUNT);
	z[idx] = std::move(samples);
	return z;
}

TEST(GlPerf, TimersJsonAvailableWithZonesAndCounters)
{
	GLTimingVerdict v{true, ""};
	auto zones = zonesWith(GLZONE_TRANSLUCENT, {10.0, 12.0, 11.0});
	zones[GLZONE_SCENE] = {4.0, 5.0, 6.0};
	std::string js = GLTimersJson(v, zones, {14.0, 17.0, 17.0}, "{\"draw_calls\":812}");

	EXPECT_NE(js.find("\"available\":true"), std::string::npos);
	EXPECT_NE(js.find("\"frames\":3"), std::string::npos);
	EXPECT_NE(js.find("\"scene\":"), std::string::npos);
	EXPECT_NE(js.find("\"translucent\":"), std::string::npos);
	EXPECT_NE(js.find("\"total\":"), std::string::npos);
	EXPECT_NE(js.find("\"counters\":{\"draw_calls\":812}"), std::string::npos);
	// Reserved-but-unmeasured zones must NOT appear as fake 0ms entries.
	EXPECT_EQ(js.find("\"portals\":"), std::string::npos);
	EXPECT_EQ(js.find("\"postprocess\":"), std::string::npos);
	EXPECT_EQ(js.find("\"note\":"), std::string::npos); // available -> no note
}

TEST(GlPerf, TimersJsonOmitsCountersWhenEmpty)
{
	GLTimingVerdict v{true, ""};
	std::string js = GLTimersJson(v, zonesWith(GLZONE_SCENE, {3.0}), {3.0}, "");
	EXPECT_EQ(js.find("\"counters\":"), std::string::npos);
}

TEST(GlPerf, TimersJsonToleratesShortZoneVector)
{
	// A perZoneMs shorter than GLZONE_COUNT must not read out of bounds -- missing zones are skipped.
	GLTimingVerdict v{true, ""};
	std::vector<std::vector<double>> shortZones; // size 0, well under GLZONE_COUNT
	std::string js = GLTimersJson(v, shortZones, {5.0, 6.0}, "");
	EXPECT_NE(js.find("\"frames\":2"), std::string::npos);
	EXPECT_NE(js.find("\"zones\":{}"), std::string::npos); // nothing to report, no crash
}

TEST(GlPerf, TimersJsonUnavailableCarriesEscapedNote)
{
	GLTimingVerdict v{false, "gpu timer queries returned zero -- \"legacy\" driver"};
	std::string js = GLTimersJson(v, std::vector<std::vector<double>>(GLZONE_COUNT), {}, "");
	EXPECT_NE(js.find("\"available\":false"), std::string::npos);
	EXPECT_NE(js.find("\"frames\":0"), std::string::npos);
	EXPECT_NE(js.find("\"note\":\""), std::string::npos);
	EXPECT_NE(js.find("\\\"legacy\\\""), std::string::npos); // the quote in the note was JSON-escaped
	EXPECT_NE(js.find("\"zones\":{}"), std::string::npos);   // no samples -> empty zone object
}
