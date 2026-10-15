// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/telemetry/computation/telemetry_compute.h"

#include <gtest/gtest.h>

#include <limits>

using namespace zx::telemetry;

namespace {

FrameRing RingOf(const std::vector<float> &column)
{
	FrameRing ring((int)column.size(), 1);
	for (size_t i = 0; i < column.size(); i++) ring.Push((int64_t)i, &column[i], 1);
	return ring;
}

} // namespace

TEST(TelemetryRing, KeepsTheNewestRowsAndOverwritesTheOldest)
{
	FrameRing ring(3, 2);
	EXPECT_EQ(ring.Rows(), 0);
	EXPECT_EQ(ring.Columns(), 2);
	for (int f = 0; f < 5; f++)
	{
		const float row[2] = { (float)f, (float)(f * 10) };
		ring.Push(f, row, 2);
	}
	EXPECT_EQ(ring.Rows(), 3);
	EXPECT_EQ(ring.Frame(0), 4);
	EXPECT_EQ(ring.Frame(2), 2);
	EXPECT_FLOAT_EQ(ring.Row(0)[1], 40.f);
	EXPECT_FLOAT_EQ(ring.Row(2)[0], 2.f);
}

// [rc4l] A counter registered after a row was pushed reads zero in that row rather than whatever was there.
TEST(TelemetryRing, ColumnsPastTheCountReadZeroAndSizesHaveAFloor)
{
	FrameRing ring(0, 0);
	const float one = 1.f;
	ring.Push(0, &one, 1);
	ring.Push(1, &one, 1);
	EXPECT_EQ(ring.Rows(), 1);
	EXPECT_EQ(ring.Columns(), 1);
	FrameRing wide(2, 3);
	const float full[3] = { 1.f, 2.f, 3.f };
	wide.Push(0, full, 3);
	wide.Push(1, full, 1);
	EXPECT_FLOAT_EQ(wide.Row(0)[0], 1.f);
	EXPECT_FLOAT_EQ(wide.Row(0)[2], 0.f);
	wide.Push(2, full, -4);
	EXPECT_FLOAT_EQ(wide.Row(0)[0], 0.f);
}

TEST(TelemetryStats, AColumnsStatisticsOverTheNewestRows)
{
	const FrameRing ring = RingOf({ 9.f, 1.f, 2.f, 3.f, 4.f });
	const ColumnStats all = ComputeColumnStats(ring, 0, 5);
	EXPECT_EQ(all.n, 5);
	EXPECT_DOUBLE_EQ(all.last, 4.0);
	EXPECT_DOUBLE_EQ(all.mean, 19.0 / 5.0);
	EXPECT_DOUBLE_EQ(all.min, 1.0);
	EXPECT_DOUBLE_EQ(all.max, 9.0);
	EXPECT_DOUBLE_EQ(all.p50, 3.0);
	EXPECT_DOUBLE_EQ(all.p99, 9.0);
	const ColumnStats newest = ComputeColumnStats(ring, 0, 2);
	EXPECT_EQ(newest.n, 2);
	EXPECT_DOUBLE_EQ(newest.max, 4.0);
}

TEST(TelemetryStats, NoRowsOrAColumnOutsideTheRingIsAllZero)
{
	const FrameRing ring = RingOf({ 1.f });
	EXPECT_EQ(ComputeColumnStats(ring, 0, 0).n, 0);
	EXPECT_EQ(ComputeColumnStats(ring, -1, 1).n, 0);
	EXPECT_EQ(ComputeColumnStats(ring, 1, 1).n, 0);
}

TEST(TelemetryStats, TheTableShowsBytesInTheirUnitAndUnavailableOnesAsNA)
{
	ColumnStats s = {};
	s.n = 1; s.last = s.mean = s.p99 = s.max = 3.5;
	ColumnStats big = s;
	big.last = big.mean = big.p99 = big.max = 1500.0;
	ColumnStats mem = s;
	mem.last = mem.mean = mem.p99 = mem.max = 2.0 * 1048576.0;
	const std::string t = ComputeStatsTable({ { "frame/ms", "ms", s, true }, { "frame/count", "frames", big, true },
		{ "mem/a_very_long_counter_name", "bytes", mem, true }, { "gpu/budget", "bytes", s, false } });
	EXPECT_NE(t.find("counter"), std::string::npos);
	EXPECT_NE(t.find("3.50"), std::string::npos);
	EXPECT_NE(t.find("1500"), std::string::npos);
	EXPECT_NE(t.find("2.0 MB"), std::string::npos);
	EXPECT_NE(t.find("gpu/budget"), std::string::npos);
	EXPECT_NE(t.find("n/a"), std::string::npos);
}

TEST(TelemetryStats, TheJsonCarriesEveryStatisticAndNonFiniteAsZero)
{
	ColumnStats s = { 2, 1.0, 2.0, 0.5, std::numeric_limits<double>::infinity(), 1.5, 3.0, 3.5 };
	const std::string j = ComputeStatsJson({ { "frame/ms", "ms", s, true }, { "gpu/x", "ms", ColumnStats{}, false } });
	EXPECT_EQ(j.front(), '[');
	EXPECT_NE(j.find("\"name\":\"frame/ms\""), std::string::npos);
	EXPECT_NE(j.find("\"available\":true"), std::string::npos);
	EXPECT_NE(j.find("\"available\":false"), std::string::npos);
	EXPECT_NE(j.find("\"max\":0,"), std::string::npos);
	EXPECT_NE(j.find("\"p99\":3.5"), std::string::npos);
	EXPECT_EQ(ComputeStatsJson({}), "[]");
}

TEST(TelemetryHitch, OverTwiceTheMedianAndOverTheFloor)
{
	EXPECT_TRUE(ComputeIsHitch(20.0, 4.0, 8.0));
	EXPECT_FALSE(ComputeIsHitch(7.0, 3.0, 8.0));
	EXPECT_FALSE(ComputeIsHitch(30.0, 20.0, 8.0));
}

TEST(TelemetryCapture, CsvHeaderQuotesNamesThatNeedItAndRowsCarryTheFrame)
{
	EXPECT_EQ(ComputeCsvHeader({ "frame/ms", "gpu/mem/a,b", "q\"x" }), "frame,time_ms,frame/ms,\"gpu/mem/a,b\",\"q\"\"x\"\n");
	const float v[2] = { 1.5f, 2.f };
	EXPECT_EQ(ComputeCsvRow(7, 12.25, v, 2), "7,12.250,1.5,2\n");
}

TEST(TelemetryCapture, TheTraceNamesThreadsAndRecordsCompleteEvents)
{
	const std::string t = ComputeChromeTrace({ { "frame/backend", 1, 10.0, 2.5 } }, { "main", "job 1" });
	EXPECT_NE(t.find("\"thread_name\""), std::string::npos);
	EXPECT_NE(t.find("\"name\":\"job 1\""), std::string::npos);
	EXPECT_NE(t.find("\"ph\":\"X\""), std::string::npos);
	EXPECT_NE(t.find("\"tid\":1,\"ts\":10,\"dur\":2.5"), std::string::npos);
	EXPECT_EQ(ComputeChromeTrace({ { "a", 0, 1.0, 1.0 } }, {}).find("{\"displayTimeUnit\":\"ms\",\"traceEvents\":[{\"name\":\"a\""), 0u);
}

TEST(TelemetrySnapshot, TheDiffListsTheLargestChangeFirstAndMissingSidesAsZero)
{
	const std::vector<Reading> before = { { "mem/a", "bytes", 100.0 }, { "mem/b", "bytes", 10.0 }, { "mem/gone", "bytes", 5.0 } };
	const std::vector<Reading> after = { { "mem/a", "bytes", 100.0 }, { "mem/b", "bytes", 1000.0 }, { "mem/new", "bytes", 50.0 } };
	const std::vector<DiffRow> d = ComputeSnapshotDiff(before, after);
	ASSERT_EQ(d.size(), 4u);
	EXPECT_EQ(d[0].name, "mem/b");
	EXPECT_DOUBLE_EQ(d[0].delta, 990.0);
	EXPECT_EQ(d[1].name, "mem/new");
	EXPECT_DOUBLE_EQ(d[1].before, 0.0);
	EXPECT_EQ(d[2].name, "mem/gone");
	EXPECT_DOUBLE_EQ(d[2].delta, -5.0);
	const std::string t = ComputeDiffTable(d, 2);
	EXPECT_NE(t.find("+990 B"), std::string::npos);
	EXPECT_NE(t.find("mem/new"), std::string::npos);
	EXPECT_EQ(t.find("mem/gone"), std::string::npos);
	EXPECT_EQ(t.find("mem/a "), std::string::npos);
	EXPECT_NE(ComputeDiffTable(d, 10).find("-5 B"), std::string::npos);
	EXPECT_NE(ComputeDiffTable({}, 10).find("(no change)"), std::string::npos);
}

TEST(TelemetrySoak, AValueThatKeepsGrowingAfterWarmupIsALeak)
{
	const GrowthVerdict leak = ComputeGrowthVerdict({ 100, 200, 300, 400, 500, 600 }, 1, 10.0, 0.01);
	EXPECT_TRUE(leak.growing);
	EXPECT_NEAR(leak.perCycle, 100.0, 1e-9);
	EXPECT_DOUBLE_EQ(leak.total, 400.0);
	EXPECT_EQ(leak.cycles, 5);
}

// [rc4l] A cache that fills during warmup and then holds, and noise around a flat line, are not leaks.
TEST(TelemetrySoak, APlateauOrNoiseIsNot)
{
	EXPECT_FALSE(ComputeGrowthVerdict({ 10, 500, 900, 900, 901, 900, 902 }, 2, 16.0, 0.0).growing);
	EXPECT_FALSE(ComputeGrowthVerdict({ 100, 103, 99, 104, 100, 102 }, 0, 0.0, 0.05).growing);
	const GrowthVerdict tooFew = ComputeGrowthVerdict({ 1, 2, 3, 4 }, 2, 0.0, 0.0);
	EXPECT_FALSE(tooFew.growing);
	EXPECT_EQ(tooFew.cycles, 2);
	EXPECT_EQ(ComputeGrowthVerdict({ 1, 2 }, 5, 0.0, 0.0).cycles, 0);
}

// [rc4l] A slope that ends lower than it started, a spike falling back, is not growth either.
TEST(TelemetrySoak, GrowthMustAlsoShowBetweenTheFirstAndLastCycle)
{
	EXPECT_FALSE(ComputeGrowthVerdict({ 0, 0, 100, 100, 100, 0 }, -1, 1.0, 0.0).growing);
}

TEST(TelemetryLedger, CountsBytesAndObjectsPerTag)
{
	TagLedger ledger(8);
	const size_t a = ledger.Add("scene VB", 1000.0);
	const size_t b = ledger.Add("material", 50.0);
	EXPECT_EQ(ledger.Add("scene VB", 500.0), a);
	EXPECT_DOUBLE_EQ(ledger.TotalBytes(), 1550.0);
	EXPECT_DOUBLE_EQ(ledger.TotalObjects(), 3.0);
	const std::vector<Reading> both = ledger.Readings("");
	ASSERT_EQ(both.size(), 2u);
	EXPECT_EQ(both[1].name, "material");
	EXPECT_DOUBLE_EQ(both[0].value, 1500.0);
	ledger.Remove(b, 50.0);
	EXPECT_DOUBLE_EQ(ledger.TotalObjects(), 2.0);
	const std::vector<Reading> r = ledger.Readings("gpu/mem/");
	ASSERT_EQ(r.size(), 1u);
	EXPECT_EQ(r[0].name, "gpu/mem/scene VB");
	EXPECT_EQ(r[0].unit, "bytes");
}

TEST(TelemetryLedger, TagsPastTheLimitShareOther)
{
	TagLedger ledger(3);
	ledger.Add("a", 1.0);
	ledger.Add("b", 2.0);
	const size_t c = ledger.Add("c", 3.0);
	EXPECT_EQ(ledger.Add("d", 4.0), c);
	const std::vector<Reading> r = ledger.Readings("");
	ASSERT_EQ(r.size(), 3u);
	EXPECT_EQ(r[0].name, "other");
	EXPECT_DOUBLE_EQ(r[0].value, 7.0);
	TagLedger one(0);
	one.Add("x", 1.0);
	EXPECT_EQ(one.Readings("")[0].name, "other");
}

TEST(TelemetryTextures, BytesCountEveryMipLayerAndSample)
{
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 4, 4, 1, 1, 1, 1, 4.0, 0, 0, 0.0 }), 64.0);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 4, 4, 1, 1, 3, 1, 4.0, 0, 0, 0.0 }), 64.0 + 16.0 + 4.0);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 4, 4, 1, 6, 1, 2, 1.0, 0, 0, 0.0 }), 16.0 * 6 * 2);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 4, 4, 4, 1, 2, 1, 1.0, 0, 0, 0.0 }), 64.0 + 4.0 * 2);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 0, 0, 0, 0, 1, 0, 2.0, 0, 0, 0.0 }), 2.0);
}

// [rc4l] Mips of 0 is the whole chain down to one texel, as a device makes it, a volume's depth counting too.
TEST(TelemetryTextures, NoMipCountMeansTheWholeChain)
{
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 4, 4, 1, 1, 0, 1, 1.0, 0, 0, 0.0 }), 16.0 + 4.0 + 1.0);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 2, 1, 4, 1, 0, 1, 1.0, 0, 0, 0.0 }), 8.0 + 2.0 + 1.0);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 0, 0, 0, 0, 0, 0, 2.0, 0, 0, 0.0 }), 2.0);
}

TEST(TelemetryTextures, CompressedFormatsCountWholeBlocks)
{
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 6, 6, 1, 1, 1, 1, 0.0, 4, 4, 8.0 }), 4.0 * 8.0);
	EXPECT_DOUBLE_EQ(ComputeTextureBytes({ 8, 8, 1, 1, 2, 1, 0.0, 4, 4, 16.0 }), 4.0 * 16.0 + 16.0);
}

TEST(TelemetryTag, NumberedCopiesShareATag)
{
	EXPECT_EQ(ComputeMemoryTag("fua sky look 3"), "fua sky look");
	EXPECT_EQ(ComputeMemoryTag("fua scene VB"), "fua scene VB");
	EXPECT_EQ(ComputeMemoryTag("pool #12"), "pool");
	EXPECT_EQ(ComputeMemoryTag("slots[4]"), "slots");
	EXPECT_EQ(ComputeMemoryTag(""), "unnamed");
	EXPECT_EQ(ComputeMemoryTag("7up"), "unnamed");
}

TEST(TelemetryBytes, EachUnitAndNegatives)
{
	EXPECT_EQ(ComputeBytesText(40.0), "40 B");
	EXPECT_EQ(ComputeBytesText(512.0 * 1024.0), "512 KB");
	EXPECT_EQ(ComputeBytesText(12.3 * 1048576.0), "12.3 MB");
	EXPECT_EQ(ComputeBytesText(2.5 * 1073741824.0), "2.50 GB");
	EXPECT_EQ(ComputeBytesText(-3.0 * 1048576.0), "-3.0 MB");
}

TEST(TelemetryFlight, TheSummaryGivesEachSecondsFrameTimeAndEachGaugesChange)
{
	FrameRing seconds(4, 4);
	for (int s = 0; s < 3; s++)
	{
		const float row[4] = { 4.f + s, 9.f + s, 100.f * (s + 1) * 1048576.f, 100.f * (s + 1) * 1048576.f };
		seconds.Push(s, row, 4);
	}
	const std::string t = ComputeFlightSummary(seconds, 0, { { "mem/process_private", "bytes", 1 } }, 10, 2);
	EXPECT_NE(t.find("last 3 s, 2 hitches"), std::string::npos);
	EXPECT_NE(t.find(" 4.0/9.0 5.0/10.0 6.0/11.0\n"), std::string::npos);
	EXPECT_NE(t.find("mem/process_private: 100.0 MB -> 300.0 MB (+200.0 MB)"), std::string::npos);
	FrameRing falling(2, 4);
	const float hi[4] = { 1.f, 1.f, 50.f, 50.f }, lo[4] = { 1.f, 1.f, 20.f, 20.f };
	falling.Push(0, hi, 4);
	falling.Push(1, lo, 4);
	EXPECT_NE(ComputeFlightSummary(falling, 0, { { "census/actors", "objects", 1 } }, 2, 1).find("50.00 -> 20.00 (-30.00)"), std::string::npos);
	EXPECT_NE(ComputeFlightSummary(falling, 0, {}, 2, 1).find("1 hitch\n"), std::string::npos);
	EXPECT_EQ(ComputeFlightSummary(FrameRing(2, 2), 0, {}, 5, 0), "telemetry: last 0 s, 0 hitches\n");
}

TEST(TelemetryStatus, ReadsAFieldInBytesOrAsWritten)
{
	const std::string status = "Name:\tforkundera\nVmHWM:\t  204800 kB\nVmRSS:\t  102400 kB\nThreads:\t17\nBroken:\tnone\n";
	EXPECT_DOUBLE_EQ(ComputeStatusField(status, "VmRSS"), 102400.0 * 1024.0);
	EXPECT_DOUBLE_EQ(ComputeStatusField(status, "Threads"), 17.0);
	EXPECT_DOUBLE_EQ(ComputeStatusField(status, "Broken"), -1.0);
	EXPECT_DOUBLE_EQ(ComputeStatusField(status, "VmSwap"), -1.0);
	EXPECT_DOUBLE_EQ(ComputeStatusField("Last:\t5", "Last"), 5.0);
}

TEST(TelemetryTabbed, ReadsANameAndValueALineAndSkipsTheRest)
{
	const std::vector<Reading> r = ComputeTabbedReadings("fua scene VB\t14270000\nno tab\n\t5\nbad\tvalue\nlast\t2.5", "gpu/page/", "bytes");
	ASSERT_EQ(r.size(), 2u);
	EXPECT_EQ(r[0].name, "gpu/page/fua scene VB");
	EXPECT_EQ(r[0].unit, "bytes");
	EXPECT_DOUBLE_EQ(r[0].value, 14270000.0);
	EXPECT_EQ(r[1].name, "gpu/page/last");
	EXPECT_DOUBLE_EQ(r[1].value, 2.5);
	EXPECT_TRUE(ComputeTabbedReadings("", "p/", "bytes").empty());
}

TEST(TelemetryKeyed, TimeSumsPerKeyAndTheTakeResetsIt)
{
	KeyedTimer timer;
	int imp = 0, zombie = 0, barrel = 0;
	timer.Add(&imp, "DoomImp", 3000000);
	timer.Add(&imp, "DoomImp", 1000000);
	timer.Add(&zombie, "ZombieMan", 1000000);
	timer.Add(&imp, "DoomImp", 1000000);
	timer.Add(&barrel, nullptr, 500000);
	const std::vector<Reading> r = timer.Take("sim/thinker/", 2);
	ASSERT_EQ(r.size(), 2u);
	EXPECT_EQ(r[0].name, "sim/thinker/DoomImp");
	EXPECT_DOUBLE_EQ(r[0].value, 5.0);
	EXPECT_EQ(r[1].name, "sim/thinker/ZombieMan");
	EXPECT_TRUE(timer.Take("x/", 10).empty());
	timer.Add(&barrel, "ignored", 2000000);
	const std::vector<Reading> again = timer.Take("", 10);
	ASSERT_EQ(again.size(), 1u);
	EXPECT_EQ(again[0].name, "unnamed");
}

TEST(TelemetryThrottle, RunsTheFirstTimeAndThenOnceAnInterval)
{
	Throttle every(10);
	EXPECT_TRUE(every.Due(100));
	EXPECT_FALSE(every.Due(105));
	EXPECT_TRUE(every.Due(110));
	EXPECT_FALSE(every.Due(119));
	Throttle always;
	EXPECT_TRUE(always.Due(5));
	EXPECT_TRUE(always.Due(5));
}

TEST(TelemetryText, PrefixesAndValuesInTheirUnit)
{
	EXPECT_TRUE(ComputeStartsWith("gpu/mem/tag", "gpu/"));
	EXPECT_TRUE(ComputeStartsWith("gpu/", ""));
	EXPECT_FALSE(ComputeStartsWith("gpu/", "gpu/mem/"));
	EXPECT_EQ(ComputeValueText(2.0 * 1048576.0, "bytes"), "2.0 MB");
	EXPECT_EQ(ComputeValueText(3.5, "ms"), "3.50");
	EXPECT_EQ(ComputeValueText(-1500.0, "objects"), "-1500");
}

TEST(TelemetryStats, BreakdownReadingsUnderThePrefixAreRowsOfOneValue)
{
	const std::vector<StatsRow> rows = ComputeReadingRows({ { "gpu/page/fua VB", "bytes", 64.0 }, { "sim/thinker/Imp", "ms", 2.0 } }, "gpu/");
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows[0].name, "gpu/page/fua VB");
	EXPECT_TRUE(rows[0].available);
	EXPECT_EQ(rows[0].stats.n, 1);
	EXPECT_DOUBLE_EQ(rows[0].stats.last, 64.0);
	EXPECT_DOUBLE_EQ(rows[0].stats.p99, 64.0);
}

TEST(TelemetryStats, TheOverlayShowsTheFirstRowsLastValues)
{
	ColumnStats s = {};
	s.last = 2.0 * 1048576.0;
	ColumnStats t = {};
	t.last = 1.5;
	const std::vector<StatsRow> rows = { { "mem/heap_bytes", "bytes", s, true }, { "frame/ms", "ms", t, true }, { "gpu/vk", "bytes", s, false } };
	EXPECT_EQ(ComputeWatchText(rows, 16), "mem/heap_bytes 2.0 MB\nframe/ms 1.50\ngpu/vk n/a\n");
	EXPECT_EQ(ComputeWatchText(rows, 1), "mem/heap_bytes 2.0 MB\n");
}

TEST(TelemetryHitch, ASlowFramesTopTimingsLargestFirst)
{
	const float row[5] = { 30.f, 4.f, 9.f, 0.01f, 6.f };
	const std::vector<std::pair<int, float>> top = ComputeHitchTop(row, { true, true, true, true, false }, 0, 0.05f, 2);
	ASSERT_EQ(top.size(), 2u);
	EXPECT_EQ(top[0].first, 2);
	EXPECT_EQ(top[1].first, 1);
	EXPECT_EQ(ComputeHitchTop(row, { true, true, true, true, false }, 0, 0.05f, 8).size(), 2u);
}

TEST(TelemetryHitch, TheTextNamesTheNewestHitchesRegions)
{
	const std::vector<Hitch> hitches = { { 10, 20.0, { { 1, 9.5f } } }, { 40, 31.25, { { 1, 12.f }, { 2, 3.f }, { 7, 1.f } } }, { 50, 18.0, {} } };
	const std::string t = ComputeHitchText(hitches, { "frame/ms", "draw/world", "vis/clip" }, 60, 2);
	EXPECT_EQ(t, "frame 40: 31.25 ms (20 frames ago): draw/world 12.00, vis/clip 3.00, ? 1.00\nframe 50: 18.00 ms (10 frames ago):\n");
	EXPECT_NE(ComputeHitchText(hitches, {}, 60, 10).find("frame 10"), std::string::npos);
	EXPECT_EQ(ComputeHitchText({}, {}, 0, 10), "no hitches yet\n");
}

// [rc4l] A counter registered mid-capture widens the later rows and reads zero in the earlier ones.
TEST(TelemetryCapture, ACapturesRowsArePaddedToEveryCounter)
{
	EXPECT_EQ(ComputeCaptureCsv({ "a", "b" }, { 4, 5 }, { 0.5, 1.25 }, { 1, 2 }, { 1.f, 2.f, 3.f }),
		"frame,time_ms,a,b\n4,0.500,1,0\n5,1.250,2,3\n");
	EXPECT_EQ(ComputeCaptureCsv({ "a" }, { 6 }, { 2.0 }, { 2 }, { 7.f, 8.f }), "frame,time_ms,a\n6,2.000,7\n");
	EXPECT_EQ(ComputeCaptureCsv({ "a" }, {}, {}, {}, {}), "frame,time_ms,a\n");
}

TEST(TelemetryCapture, ARingIsACsvOldestFirstWithAColumnPerName)
{
	FrameRing ring(4, 2);
	const float a[2] = { 1.f, 2.f }, b[2] = { 3.f, 4.f };
	ring.Push(7, a, 2);
	ring.Push(8, b, 2);
	EXPECT_EQ(ComputeRingCsv(ring, { "x", "y" }), "frame,time_ms,x,y\n7,0.000,1,2\n8,0.000,3,4\n");
	EXPECT_EQ(ComputeRingCsv(ring, { "x", "y", "z" }), "frame,time_ms,x,y\n7,0.000,1,2\n8,0.000,3,4\n");
	EXPECT_EQ(ComputeSecondNames({ "frame/ms" }), (std::vector<std::string>{ "frame/ms mean", "frame/ms max" }));
}

namespace {

std::vector<Reading> Mark(double heap, double actors, double frameMs)
{
	return { { "mem/heap_bytes", "bytes", heap }, { "census/actors", "objects", actors }, { "frame/ms", "ms", frameMs } };
}

} // namespace

TEST(TelemetrySoak, TheVerdictNamesWhatKeptGrowingAndByHowMuch)
{
	std::vector<std::vector<Reading>> marks;
	for (int i = 0; i < 6; i++) marks.push_back(Mark(1048576.0 * (10 + i), 50.0, 100.0 * i));
	const SoakVerdict v = ComputeSoakVerdict(marks, 1);
	EXPECT_FALSE(v.pass);
	EXPECT_EQ(v.marks, 6);
	EXPECT_EQ(v.judged, 2);
	ASSERT_EQ(v.grew.size(), 1u);
	EXPECT_EQ(v.grew[0].name, "mem/heap_bytes");
	EXPECT_EQ(v.grew[0].text, "mem/heap_bytes grew 1.0 MB a cycle (4.0 MB over 5 cycles)");
	EXPECT_EQ(ComputeSoakText(v), "SOAK FAIL: 6 marks, 2 counters judged, 1 grew\n  mem/heap_bytes grew 1.0 MB a cycle (4.0 MB over 5 cycles)\n");
	const std::string j = ComputeSoakJson(v);
	EXPECT_NE(j.find("{\"pass\":false,\"marks\":6,\"judged\":2,\"grew\":[{\"name\":\"mem/heap_bytes\",\"text\":\"mem/heap_bytes grew"), std::string::npos);
	EXPECT_NE(j.find("\"report\":\"SOAK FAIL: 6 marks, 2 counters judged, 1 grew\\n  mem/heap_bytes"), std::string::npos);
}

// [rc4l] A count grows by more than four to be judged growing, and a reading a later mark lacks is not judged.
TEST(TelemetrySoak, CountsAndMissingReadingsAreJudgedApart)
{
	std::vector<std::vector<Reading>> marks;
	for (int i = 0; i < 6; i++) marks.push_back(Mark(1048576.0, 50.0 + 10 * i, 0.0));
	marks[5].erase(marks[5].begin());
	const SoakVerdict v = ComputeSoakVerdict(marks, 1);
	EXPECT_EQ(v.judged, 1);
	ASSERT_EQ(v.grew.size(), 1u);
	EXPECT_EQ(v.grew[0].text, "census/actors grew 10.00 a cycle (40.00 over 5 cycles)");
	EXPECT_NE(ComputeSoakJson(v).find("{\"name\":\"census/actors\""), std::string::npos);
}

// [rc4l] Nothing growing passes only once three marks follow the warmup.
TEST(TelemetrySoak, AFlatSoakPassesOnceItHasEnoughMarks)
{
	std::vector<std::vector<Reading>> marks;
	for (int i = 0; i < 4; i++) marks.push_back(Mark(1048576.0, 50.0, 0.0));
	EXPECT_TRUE(ComputeSoakVerdict(marks, 1).pass);
	EXPECT_FALSE(ComputeSoakVerdict(marks, 2).pass);
	EXPECT_EQ(ComputeSoakText(ComputeSoakVerdict(marks, 1)), "SOAK PASS: 4 marks, 2 counters judged, 0 grew\n");
	const SoakVerdict none = ComputeSoakVerdict({}, 1);
	EXPECT_FALSE(none.pass);
	EXPECT_EQ(none.judged, 0);
	EXPECT_EQ(ComputeSoakJson(none).find("{\"pass\":false,\"marks\":0,\"judged\":0,\"grew\":[],\"report\":"), 0u);
}

TEST(TelemetryBudget, TheDevicesOwnHeapsAreSummed)
{
	const HeapBudget sum = ComputeDeviceLocalBudget({ { true, 100.0, 1000.0 }, { false, 5.0, 50.0 }, { true, 20.0, 200.0 } });
	EXPECT_TRUE(sum.deviceLocal);
	EXPECT_DOUBLE_EQ(sum.used, 120.0);
	EXPECT_DOUBLE_EQ(sum.budget, 1200.0);
}
