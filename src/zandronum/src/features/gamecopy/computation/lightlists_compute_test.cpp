// [Doruk-Aksoy] Tests for lightlists_compute: the subsector light runs made from the lit subsectors alone, whole or by chunks joined, match a walk of every subsector.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/gamecopy/computation/lightlists_compute.h"
#include "features/jobs/computation/joinparts_compute.h"

using namespace zx::gamecopy;
using zx::jobs::Part;

namespace {

// [Doruk-Aksoy] Each subsector's light places in its own list's order, as a walk of every subsector reads them, -1 standing for a light not listed.
void Reference(const std::vector<std::vector<int>> &lists, std::vector<int> &first, std::vector<int> &count, std::vector<int> &out)
{
	first.assign(lists.size(), 0);
	count.assign(lists.size(), 0);
	out.clear();
	for (size_t s = 0; s < lists.size(); s++)
	{
		first[s] = (int)out.size();
		for (int place : lists[s]) if (place >= 0) out.push_back(place);
		count[s] = (int)out.size() - first[s];
	}
}

} // namespace

// [Doruk-Aksoy] Repeats and indices past either end mark nothing extra, lanes' parts mark together, and a level of no subsectors has no bits.
TEST(LightLists, TouchedSubsectorsMarkTheirBits)
{
	std::vector<uint64_t> bits;
	std::vector<Part<int>> touched(3);
	touched[0].items = { 3, 3, 64 };
	touched[2].items = { 70, -1, 200, 130 };
	ComputeLitSubsectors(touched, 131, bits);
	ASSERT_EQ(3u, bits.size());
	EXPECT_EQ(1ull << 3, bits[0]);
	EXPECT_EQ((1ull << 0) | (1ull << 6), bits[1]);
	EXPECT_EQ(1ull << 2, bits[2]);
	ComputeLitSubsectors(touched, 0, bits);
	EXPECT_TRUE(bits.empty());
	ComputeLitSubsectors(std::vector<Part<int>>(), 10, bits);
	ASSERT_EQ(1u, bits.size());
	EXPECT_EQ(0ull, bits[0]);
}

// [Doruk-Aksoy] Lists made from the lit subsectors alone are the lists a walk of every subsector makes, empty runs and unlisted lights included, whole or by chunks joined.
TEST(LightLists, RunsFromTheLitSubsectorsMatchAWalkOfEveryOne)
{
	unsigned seed = 7;
	auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
	for (int round = 0; round < 20; round++)
	{
		const int n = 1 + (int)(next() % 300);
		std::vector<std::vector<int>> lists(n);
		std::vector<Part<int>> touched(3);
		for (int s = 0; s < n; s++)
		{
			if (next() % 3 != 0) continue;
			const int k = (int)(next() % 5);
			for (int j = 0; j < k; j++) lists[s].push_back(next() % 4 == 0 ? -1 : (int)(next() % 50));
			// [Doruk-Aksoy] A lit subsector is touched by every light in it, whose lanes differ, so it can be reached more than once.
			for (int j = 0; j < k; j++) touched[next() % 3].items.push_back(s);
		}
		std::vector<uint64_t> bits;
		ComputeLitSubsectors(touched, n, bits);
		const auto lightsOf = [&lists](int s, std::vector<int> &runs) { for (int place : lists[s]) if (place >= 0) runs.push_back(place); };
		std::vector<int> wantFirst, wantCount, wantOut;
		Reference(lists, wantFirst, wantCount, wantOut);
		std::vector<int> first(n, -1), count(n, -1), out;
		ComputeSubsectorRuns(bits, 0, n, first, count, out, lightsOf);
		EXPECT_EQ(wantFirst, first) << "round " << round;
		EXPECT_EQ(wantCount, count) << "round " << round;
		EXPECT_EQ(wantOut, out) << "round " << round;
		const int chunk = 1 + (int)(next() % 40);
		std::vector<Part<int>> parts((n + chunk - 1) / chunk);
		std::vector<int> chunkedFirst(n, -1), chunkedCount(n, -1), chunkedOut, starts;
		for (int lo = 0; lo < n; lo += chunk) ComputeSubsectorRuns(bits, lo, lo + chunk < n ? lo + chunk : n, chunkedFirst, chunkedCount, parts[lo / chunk].items, lightsOf);
		zx::jobs::ComputeJoinParts(parts, chunkedOut, starts);
		ComputeShiftRuns(starts, chunk, chunkedFirst);
		EXPECT_EQ(wantFirst, chunkedFirst) << "round " << round << " chunk " << chunk;
		EXPECT_EQ(wantCount, chunkedCount) << "round " << round << " chunk " << chunk;
		EXPECT_EQ(wantOut, chunkedOut) << "round " << round << " chunk " << chunk;
	}
}

// [Doruk-Aksoy] A bitset shorter than the subsectors reads the rest as unlit, and an empty range adds nothing.
TEST(LightLists, ShortBitsAndNoSubsectors)
{
	std::vector<uint64_t> bits(1, 1ull);
	std::vector<int> first(100, -1), count(100, -1), out;
	ComputeSubsectorRuns(bits, 0, 100, first, count, out, [](int s, std::vector<int> &runs) { runs.push_back(s); });
	EXPECT_EQ(1u, out.size());
	EXPECT_EQ(1, count[0]);
	EXPECT_EQ(0, count[99]);
	EXPECT_EQ(1, first[99]);
	ComputeSubsectorRuns(bits, 5, 5, first, count, out, [](int, std::vector<int> &) {});
	EXPECT_EQ(1u, out.size());
}

// [Doruk-Aksoy] A chunk past the starts given, or a chunk size of none, moves nothing.
TEST(LightLists, ShiftsOnlyWhereAPartLanded)
{
	std::vector<int> first = { 0, 1, 0, 2, 0 };
	ComputeShiftRuns({ 0, 10 }, 2, first);
	EXPECT_EQ(std::vector<int>({ 0, 1, 10, 12, 0 }), first);
	ComputeShiftRuns({ 5 }, 0, first);
	EXPECT_EQ(std::vector<int>({ 0, 1, 10, 12, 0 }), first);
}

// [Doruk-Aksoy] The last chunk is cut at the subsectors' end, and starts past it move nothing.
TEST(LightLists, ShiftsTheShortLastChunk)
{
	std::vector<int> first = { 0, 1, 0, 2, 0 };
	ComputeShiftRuns({ 0, 10, 20, 30 }, 2, first);
	EXPECT_EQ(std::vector<int>({ 0, 1, 10, 12, 20 }), first);
}
