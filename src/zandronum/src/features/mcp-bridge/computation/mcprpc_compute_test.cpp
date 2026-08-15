// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/mcp-bridge/computation/mcprpc_compute.h"

using namespace zx::mcp;

// ---- ParseRequest ----------------------------------------------------------

TEST(McpRpc, ParsesFullRequest)
{
	RpcRequest r = ParseRequest("{\"id\":7,\"cmd\":\"sim.step\",\"args\":{\"tics\":35}}");
	EXPECT_TRUE(r.valid);
	EXPECT_EQ(r.id, 7);
	EXPECT_EQ(r.cmd, "sim.step");
	long tics = 0;
	EXPECT_TRUE(GetInt(r.args, "tics", tics));
	EXPECT_EQ(tics, 35);
}

TEST(McpRpc, ParsesRequestWithNoArgs)
{
	RpcRequest r = ParseRequest("{\"id\":1,\"cmd\":\"ping\"}");
	EXPECT_TRUE(r.valid);
	EXPECT_EQ(r.id, 1);
	EXPECT_EQ(r.cmd, "ping");
	EXPECT_EQ(r.args, "{}"); // defaulted
}

TEST(McpRpc, NonRequestLineIsInvalid)
{
	EXPECT_FALSE(ParseRequest("{\"t\":\"event\",\"event\":\"tic\"}").valid);
	EXPECT_FALSE(ParseRequest("not json at all").valid);
	EXPECT_FALSE(ParseRequest("{\"cmd\":\"\"}").valid); // empty cmd
}

TEST(McpRpc, MissingIdDefaultsToMinusOne)
{
	RpcRequest r = ParseRequest("{\"cmd\":\"ping\"}");
	EXPECT_TRUE(r.valid);
	EXPECT_EQ(r.id, -1);
}

TEST(McpRpc, NestedArgsObjectSurvivesBraceMatching)
{
	RpcRequest r = ParseRequest("{\"id\":2,\"cmd\":\"x\",\"args\":{\"a\":{\"b\":1},\"c\":2}}");
	ASSERT_TRUE(r.valid);
	EXPECT_EQ(r.args, "{\"a\":{\"b\":1},\"c\":2}");
	long c = 0;
	EXPECT_TRUE(GetInt(r.args, "c", c));
	EXPECT_EQ(c, 2);
}

TEST(McpRpc, BraceInsideArgsStringDoesNotEndTheObject)
{
	RpcRequest r = ParseRequest("{\"id\":3,\"cmd\":\"console.exec\",\"args\":{\"text\":\"summon }evil{\"}}");
	ASSERT_TRUE(r.valid);
	std::string text;
	EXPECT_TRUE(GetStr(r.args, "text", text));
	EXPECT_EQ(text, "summon }evil{");
}

// ---- GetInt / GetStr -------------------------------------------------------

TEST(McpRpc, GetIntHandlesNegativeAndMissing)
{
	long v = 999;
	EXPECT_TRUE(GetInt("{\"n\":-42}", "n", v));
	EXPECT_EQ(v, -42);
	EXPECT_FALSE(GetInt("{\"n\":-42}", "missing", v));
}

TEST(McpRpc, GetFloatParsesSignFractionAndInteger)
{
	double v = -1.0;
	EXPECT_TRUE(GetFloat("{\"x\":0.75}", "x", v));
	EXPECT_NEAR(v, 0.75, 1e-9);
	EXPECT_TRUE(GetFloat("{\"x\":-0.5}", "x", v));
	EXPECT_NEAR(v, -0.5, 1e-9);
	EXPECT_TRUE(GetFloat("{\"x\":3}", "x", v));       // bare integer parses as float
	EXPECT_NEAR(v, 3.0, 1e-9);
	EXPECT_TRUE(GetFloat("{\"x\":+1.25}", "x", v));   // leading + sign
	EXPECT_NEAR(v, 1.25, 1e-9);
}

TEST(McpRpc, GetFloatRejectsMissingAndNonNumeric)
{
	double v = 42.0;
	EXPECT_FALSE(GetFloat("{\"x\":0.75}", "missing", v));
	EXPECT_FALSE(GetFloat("{\"x\":\"str\"}", "x", v)); // a string value is not a number
	EXPECT_FALSE(GetFloat("{\"x\":.}", "x", v));       // no digits at all
	EXPECT_NEAR(v, 42.0, 1e-9);                        // out untouched on failure
}

TEST(McpRpc, DegreesToViewUnitsIsExactForDivisorsAndSigned)
{
	EXPECT_EQ(DegreesToViewUnits(135.0), 24576); // the scenario angle -- exact
	EXPECT_EQ(DegreesToViewUnits(90.0), 16384);
	EXPECT_EQ(DegreesToViewUnits(180.0), 32768);
	EXPECT_EQ(DegreesToViewUnits(360.0), 65536);
	EXPECT_EQ(DegreesToViewUnits(0.0), 0);
	EXPECT_EQ(DegreesToViewUnits(-45.0), -8192); // sign preserved
	// arbitrary angle rounds to nearest BAM step (10 deg = 1820.44 -> 1820)
	EXPECT_EQ(DegreesToViewUnits(10.0), 1820);
}

TEST(McpRpc, GetStrDecodesEscapes)
{
	std::string out;
	EXPECT_TRUE(GetStr("{\"s\":\"a\\nb\\t\\\"c\\\\d\"}", "s", out));
	EXPECT_EQ(out, "a\nb\t\"c\\d");
}

TEST(McpRpc, GetStrDecodesCarriageReturnSlashAndUnknownEscapes)
{
	// \r and \/ decode to their characters; an unknown escape (\q) keeps the escaped char verbatim.
	std::string out;
	EXPECT_TRUE(GetStr("{\"s\":\"a\\rb\\/c\\qd\"}", "s", out));
	EXPECT_EQ(out, "a\rb/cqd");
}

TEST(McpRpc, GetStrRejectsUnterminated)
{
	std::string out;
	EXPECT_FALSE(GetStr("{\"s\":\"oops", "s", out));
}

// ---- JsonEscape ------------------------------------------------------------

TEST(McpRpc, JsonEscapeQuotesBackslashNewlineAndDropsCR)
{
	std::string out;
	JsonEscape("he said \"hi\"\r\n\tpath\\x", out);
	EXPECT_EQ(out, "he said \\\"hi\\\"\\n\\tpath\\\\x"); // CR dropped, LF/TAB/quote/backslash escaped
}

TEST(McpRpc, JsonEscapeControlCharToUnicode)
{
	std::string out;
	std::string in;
	in.push_back((char)0x01);
	JsonEscape(in, out);
	EXPECT_EQ(out, "\\u0001");
}

// ---- Framing ---------------------------------------------------------------

TEST(McpRpc, BuildOkResponseWrapsBody)
{
	EXPECT_EQ(BuildOkResponse(5, "{\"pong\":true}"),
		"{\"id\":5,\"ok\":true,\"result\":{\"pong\":true}}");
}

TEST(McpRpc, BuildErrResponseEscapesMessage)
{
	EXPECT_EQ(BuildErrResponse(9, "bad \"cmd\""),
		"{\"id\":9,\"ok\":false,\"error\":\"bad \\\"cmd\\\"\"}");
}

TEST(McpRpc, BuildEventWrapsData)
{
	EXPECT_EQ(BuildEvent("stepped", "{\"leveltime\":70}"),
		"{\"t\":\"event\",\"event\":\"stepped\",\"data\":{\"leveltime\":70}}");
	// empty data becomes {}
	EXPECT_EQ(BuildEvent("mapload", ""),
		"{\"t\":\"event\",\"event\":\"mapload\",\"data\":{}}");
}

TEST(McpRpc, RoundTripRequestThenResponse)
{
	RpcRequest r = ParseRequest("{\"id\":11,\"cmd\":\"sim.seed\",\"args\":{\"value\":123}}");
	ASSERT_TRUE(r.valid);
	long seed = 0;
	ASSERT_TRUE(GetInt(r.args, "value", seed));
	std::string body = "{\"rngseed\":" + std::to_string(seed) + "}";
	EXPECT_EQ(BuildOkResponse(r.id, body), "{\"id\":11,\"ok\":true,\"result\":{\"rngseed\":123}}");
}

// ---- State hash ------------------------------------------------------------

TEST(McpRpc, HashIsDeterministicAndOrderSensitive)
{
	uint64_t a = FnvMixU64(FnvMixU64(FnvInit(), 100), 200);
	uint64_t b = FnvMixU64(FnvMixU64(FnvInit(), 100), 200);
	uint64_t c = FnvMixU64(FnvMixU64(FnvInit(), 200), 100); // swapped
	EXPECT_EQ(a, b);            // same inputs, same order -> equal (the desync-detect invariant)
	EXPECT_NE(a, c);            // order matters
}

TEST(McpRpc, HashDetectsASingleFieldDifference)
{
	// Two "actors" identical but for one health value -> different fingerprints.
	uint64_t h1 = FnvMixU64(FnvMixU64(FnvMixU64(FnvInit(), 10), 20), 100);
	uint64_t h2 = FnvMixU64(FnvMixU64(FnvMixU64(FnvInit(), 10), 20), 99);
	EXPECT_NE(h1, h2);
}

TEST(McpRpc, HashMixStrDistinguishesStrings)
{
	EXPECT_NE(FnvMixStr(FnvInit(), "MAP01"), FnvMixStr(FnvInit(), "MAP02"));
	EXPECT_EQ(FnvMixStr(FnvInit(), "MAP01"), FnvMixStr(FnvInit(), "MAP01"));
}

// ---- Step planning ---------------------------------------------------------

TEST(McpRpc, StepTargetAddsTicsAndClampsToOne)
{
	EXPECT_EQ(StepTarget(70, 35), 105);
	EXPECT_EQ(StepTarget(70, 0), 71);   // clamped to at least one tic
	EXPECT_EQ(StepTarget(70, -5), 71);
}

TEST(McpRpc, StepCompleteWhenClockReachesTarget)
{
	EXPECT_FALSE(StepComplete(104, 105));
	EXPECT_TRUE(StepComplete(105, 105));
	EXPECT_TRUE(StepComplete(106, 105)); // overshoot still complete
}

// ---- Perf summary ----------------------------------------------------------

TEST(McpRpc, PerfSummaryBasicStats)
{
	// 10 frames at 10ms + implied 100fps.
	std::vector<double> f(10, 10.0);
	PerfSummary s = SummarizeFrameTimes(f);
	EXPECT_EQ(s.n, 10);
	EXPECT_DOUBLE_EQ(s.mean, 10.0);
	EXPECT_DOUBLE_EQ(s.min, 10.0);
	EXPECT_DOUBLE_EQ(s.max, 10.0);
	EXPECT_DOUBLE_EQ(s.fpsAvg, 100.0);
	EXPECT_DOUBLE_EQ(s.fps1pctLow, 100.0);
}

TEST(McpRpc, PerfSummaryCatchesTheStutterInTheTail)
{
	// 99 smooth frames (10ms) + one 100ms spike: the mean barely moves, but the tail (p99 / 1% low)
	// exposes the stutter -- exactly why we report percentiles, not average FPS.
	std::vector<double> f(99, 10.0);
	f.push_back(100.0);
	PerfSummary s = SummarizeFrameTimes(f);
	EXPECT_EQ(s.n, 100);
	EXPECT_NEAR(s.mean, 10.9, 0.001);          // average hides it
	EXPECT_GT(s.fpsAvg, 90.0);                  // ~91.7 fps avg looks perfectly fine...
	EXPECT_DOUBLE_EQ(s.p99, 10.0);             // ...and even p99 misses a single spike (it's p100)
	EXPECT_DOUBLE_EQ(s.max, 100.0);            // only max...
	EXPECT_DOUBLE_EQ(s.fps1pctLow, 10.0);      // ...and the 1% low (10 fps) expose the stutter
}

TEST(McpRpc, PerfSummaryEmptyIsZeroed)
{
	PerfSummary s = SummarizeFrameTimes({});
	EXPECT_EQ(s.n, 0);
	EXPECT_DOUBLE_EQ(s.fpsAvg, 0.0);
}

TEST(McpRpc, PerfSummaryJsonRoundsToThreeDecimals)
{
	std::vector<double> f(4, 8.0);
	std::string j = PerfSummaryJson(SummarizeFrameTimes(f));
	EXPECT_NE(j.find("\"mean_ms\":8.000"), std::string::npos);
	EXPECT_NE(j.find("\"fps_avg\":125.000"), std::string::npos);
	EXPECT_NE(j.find("\"n\":4"), std::string::npos);
}
