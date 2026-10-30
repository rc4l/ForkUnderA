// [Doruk-Aksoy] Tests for radixsort_compute: the order std::stable_sort gives, for keys of any width.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "features/hwrender/computation/radixsort_compute.h"

using namespace zx::hwrender;

namespace {

struct Item { uint64_t key; int tag; };

unsigned ByteOf(const Item &it, int b) { return (unsigned)(it.key >> (8 * b)) & 0xFF; }

std::vector<Item> Random(int n, unsigned seed, uint64_t mask)
{
	std::vector<Item> out;
	uint64_t s = seed;
	for (int i = 0; i < n; i++)
	{
		s = s * 6364136223846793005ull + 1442695040888963407ull;
		out.push_back({ (s ^ (s >> 29)) & mask, i });
	}
	return out;
}

template <int KeyBytes> void ExpectStableSorted(std::vector<Item> items)
{
	std::vector<Item> want = items, scratch;
	std::stable_sort(want.begin(), want.end(), [](const Item &a, const Item &b) { return a.key < b.key; });
	ComputeRadixSort<KeyBytes>(items, scratch, ByteOf);
	ASSERT_EQ(want.size(), items.size());
	for (size_t i = 0; i < want.size(); i++)
	{
		EXPECT_EQ(want[i].key, items[i].key) << "at " << i;
		EXPECT_EQ(want[i].tag, items[i].tag) << "at " << i;
	}
}

} // namespace

// [Doruk-Aksoy] Full 64-bit keys, keys from a few values (many ties) and keys sharing their high bytes, as pointers do.
TEST(RadixSort, SortsAsAStableSortDoes)
{
	ExpectStableSorted<8>(Random(3000, 1u, ~0ull));
	ExpectStableSorted<8>(Random(3000, 2u, 0x7ull));
	ExpectStableSorted<8>(Random(3000, 3u, 0x0000FFFF00FF00F0ull));
}

// [Doruk-Aksoy] Keys that all agree are left in their order, and no byte is moved for them.
TEST(RadixSort, EqualKeysKeepTheirOrder)
{
	std::vector<Item> items = { { 5, 0 }, { 5, 1 }, { 5, 2 } }, scratch;
	ComputeRadixSort<8>(items, scratch, ByteOf);
	EXPECT_EQ(0, items[0].tag); EXPECT_EQ(1, items[1].tag); EXPECT_EQ(2, items[2].tag);
	EXPECT_TRUE(scratch.size() == 3u);
}

// [Doruk-Aksoy] Nothing and one item are already sorted, and only the bytes asked for are read.
TEST(RadixSort, ShortListsAndNarrowKeys)
{
	std::vector<Item> none, one = { { 9, 0 } }, scratch;
	ComputeRadixSort<8>(none, scratch, ByteOf);
	ComputeRadixSort<8>(one, scratch, ByteOf);
	EXPECT_TRUE(none.empty());
	EXPECT_EQ(9u, one[0].key);
	std::vector<Item> items = { { 0x102, 0 }, { 0x201, 1 } };
	ComputeRadixSort<1>(items, scratch, ByteOf);
	EXPECT_EQ(1, items[0].tag) << "byte 0 alone orders 0x201 first";
}
