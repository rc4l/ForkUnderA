// [Doruk-Aksoy] Tests for lightlists_compute: the subsector light runs made from the lit subsectors alone match a walk of every subsector.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "features/gamecopy/computation/lightlists_compute.h"

using namespace zx::gamecopy;

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

// [Doruk-Aksoy] Repeats and indices past either end mark nothing extra, and a level of no subsectors has no bits.
TEST(LightLists, TouchedSubsectorsMarkTheirBits)
{
	std::vector<uint64_t> bits;
	const int touched[] = { 3, 3, 64, 70, -1, 200, 130 };
	ComputeLitSubsectors(touched, 7, 131, bits);
	ASSERT_EQ(3u, bits.size());
	EXPECT_EQ(1ull << 3, bits[0]);
	EXPECT_EQ((1ull << 0) | (1ull << 6), bits[1]);
	EXPECT_EQ(1ull << 2, bits[2]);
	ComputeLitSubsectors(touched, 7, 0, bits);
	EXPECT_TRUE(bits.empty());
	ComputeLitSubsectors(nullptr, 0, 10, bits);
	ASSERT_EQ(1u, bits.size());
	EXPECT_EQ(0ull, bits[0]);
}

// [Doruk-Aksoy] Lists made from the lit subsectors alone are the lists a walk of every subsector makes, empty runs and unlisted lights included.
TEST(LightLists, RunsFromTheLitSubsectorsMatchAWalkOfEveryOne)
{
	unsigned seed = 7;
	auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
	for (int round = 0; round < 20; round++)
	{
		const int n = 1 + (int)(next() % 300);
		std::vector<std::vector<int>> lists(n);
		std::vector<int> touched;
		for (int s = 0; s < n; s++)
		{
			if (next() % 3 != 0) continue;
			const int k = (int)(next() % 5);
			for (int j = 0; j < k; j++) lists[s].push_back(next() % 4 == 0 ? -1 : (int)(next() % 50));
			// [Doruk-Aksoy] A lit subsector is touched by every light in it, so it can be reached more than once.
			for (int j = 0; j < k; j++) touched.push_back(s);
		}
		std::vector<uint64_t> bits;
		ComputeLitSubsectors(touched.empty() ? nullptr : &touched[0], (int)touched.size(), n, bits);
		std::vector<int> first, count, out, wantFirst, wantCount, wantOut;
		ComputeSubsectorRuns(bits, n, first, count, out, [&lists](int s, std::vector<int> &runs) {
			for (int place : lists[s]) if (place >= 0) runs.push_back(place);
		});
		Reference(lists, wantFirst, wantCount, wantOut);
		EXPECT_EQ(wantFirst, first) << "round " << round;
		EXPECT_EQ(wantCount, count) << "round " << round;
		EXPECT_EQ(wantOut, out) << "round " << round;
	}
}

// [Doruk-Aksoy] A bitset shorter than the subsectors reads the rest as unlit, and none makes empty arrays.
TEST(LightLists, ShortBitsAndNoSubsectors)
{
	std::vector<uint64_t> bits(1, 1ull);
	std::vector<int> first, count, out;
	ComputeSubsectorRuns(bits, 100, first, count, out, [](int s, std::vector<int> &runs) { runs.push_back(s); });
	EXPECT_EQ(1u, out.size());
	EXPECT_EQ(1, count[0]);
	EXPECT_EQ(0, count[99]);
	EXPECT_EQ(1, first[99]);
	ComputeSubsectorRuns(bits, 0, first, count, out, [](int, std::vector<int> &) {});
	EXPECT_TRUE(first.empty());
	EXPECT_TRUE(count.empty());
}
