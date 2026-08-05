// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-download/computation/wadstore_compute.h"

using zx::ComputePruneOrder;
using zx::IsHexDigest;
using zx::StoreEntry;
using zx::StoredRelativeDir;
using zx::StoredRelativePath;
using std::string;
using std::vector;

namespace
{
const size_t kMd5Len = 32;
const string kDigest = "0123456789abcdef0123456789abcdef";
} // namespace

// ---------------------------------------------------------------- digest validation

TEST(IsHexDigest, AcceptsTheRightLengthInEitherCase)
{
	EXPECT_TRUE(IsHexDigest(kDigest, kMd5Len));
	EXPECT_TRUE(IsHexDigest("0123456789ABCDEF0123456789ABCDEF", kMd5Len));
}

TEST(IsHexDigest, RejectsTheWrongLength)
{
	EXPECT_FALSE(IsHexDigest("", kMd5Len));
	EXPECT_FALSE(IsHexDigest("abc", kMd5Len));
	EXPECT_FALSE(IsHexDigest(kDigest + "00", kMd5Len));
}

TEST(IsHexDigest, RejectsNonHex)
{
	// This string gets pasted into a filesystem path, and it came from a remote server -- so it is
	// checked rather than trusted because it usually looks right.
	EXPECT_FALSE(IsHexDigest("0123456789abcdef0123456789abcdeg", kMd5Len));
	EXPECT_FALSE(IsHexDigest("../456789abcdef0123456789abcdef01", kMd5Len));
	EXPECT_FALSE(IsHexDigest("0123456789abcdef0123456789abcde/", kMd5Len));
}

// ---------------------------------------------------------------- paths

TEST(StoredRelativeDir, IsTheStoreFolderPlusTheDigest)
{
	EXPECT_EQ("by-hash/" + kDigest, StoredRelativeDir(kDigest, kMd5Len));
}

TEST(StoredRelativeDir, NormalisesTheDigestToLowercase)
{
	// Or the same file lands in two folders depending on which server spelled the hash.
	EXPECT_EQ("by-hash/" + kDigest, StoredRelativeDir("0123456789ABCDEF0123456789ABCDEF", kMd5Len));
}

TEST(StoredRelativeDir, IsEmptyForAnUnusableDigest)
{
	EXPECT_EQ("", StoredRelativeDir("nonsense", kMd5Len));
}

TEST(StoredRelativePath, PutsTheNameInsideTheDigestFolder)
{
	// The name stays in the path on purpose: keyed on the digest alone, two files that collide would
	// be one entry, and chosen-prefix MD5 collisions are practical.
	EXPECT_EQ("by-hash/" + kDigest + "/test.wad", StoredRelativePath(kDigest, "test.wad", kMd5Len));
}

TEST(StoredRelativePath, KeepsTwoVersionsOfOneNameApart)
{
	const string a = StoredRelativePath(kDigest, "test.wad", kMd5Len);
	const string b = StoredRelativePath("ffffffffffffffffffffffffffffffff", "test.wad", kMd5Len);
	EXPECT_NE(a, b) << "the whole point: two test.wads must not be one file";
}

TEST(StoredRelativePath, IsEmptyForAnUnusableDigest)
{
	EXPECT_EQ("", StoredRelativePath("nope", "test.wad", kMd5Len));
}

TEST(StoredRelativePath, RefusesANameItWouldNotCreateAnyway)
{
	// Empty means "do not store this", never "here is a path".
	EXPECT_EQ("", StoredRelativePath(kDigest, "../escape.wad", kMd5Len));
	EXPECT_EQ("", StoredRelativePath(kDigest, "sub/dir.wad", kMd5Len));
	EXPECT_EQ("", StoredRelativePath(kDigest, "evil.exe", kMd5Len));
	EXPECT_EQ("", StoredRelativePath(kDigest, "", kMd5Len));
}

// ---------------------------------------------------------------- pruning

TEST(ComputePruneOrder, DeletesNothingWhenItAlreadyFits)
{
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(100, 1));
	entries.push_back(StoreEntry(100, 2));
	EXPECT_TRUE(ComputePruneOrder(entries, 1000).empty());
}

TEST(ComputePruneOrder, DeletesNothingWithNoCapConfigured)
{
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(100, 1));
	EXPECT_TRUE(ComputePruneOrder(entries, 0).empty());
	EXPECT_TRUE(ComputePruneOrder(entries, -1).empty());
}

TEST(ComputePruneOrder, DropsTheLeastRecentlyUsedFirst)
{
	// Versions accumulate here instead of overwriting, which is the point and also the cost -- so
	// what a player is actually playing has to be what survives.
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(100, 300));		// 0: newest
	entries.push_back(StoreEntry(100, 100));		// 1: oldest
	entries.push_back(StoreEntry(100, 200));		// 2: middle

	const vector<size_t> doomed = ComputePruneOrder(entries, 150);
	ASSERT_EQ(2u, doomed.size());
	EXPECT_EQ(1u, doomed[0]);
	EXPECT_EQ(2u, doomed[1]);
}

TEST(ComputePruneOrder, StopsAsSoonAsItFits)
{
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(500, 1));
	entries.push_back(StoreEntry(100, 2));
	entries.push_back(StoreEntry(100, 3));

	const vector<size_t> doomed = ComputePruneOrder(entries, 250);
	ASSERT_EQ(1u, doomed.size()) << "dropping the big old one is already enough";
	EXPECT_EQ(0u, doomed[0]);
}

TEST(ComputePruneOrder, IsDeterministicOnTiedTimestamps)
{
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(100, 5));
	entries.push_back(StoreEntry(100, 5));
	entries.push_back(StoreEntry(100, 5));

	const vector<size_t> first = ComputePruneOrder(entries, 150);
	const vector<size_t> second = ComputePruneOrder(entries, 150);
	EXPECT_EQ(first, second) << "a prune that picked differently each run would be untestable";
	ASSERT_EQ(2u, first.size());
	EXPECT_EQ(0u, first[0]);
	EXPECT_EQ(1u, first[1]);
}

TEST(ComputePruneOrder, CanEmptyTheStoreEntirely)
{
	vector<StoreEntry> entries;
	entries.push_back(StoreEntry(900, 1));
	EXPECT_EQ(1u, ComputePruneOrder(entries, 1).size());
}

TEST(ComputePruneOrder, HandlesAnEmptyStore)
{
	EXPECT_TRUE(ComputePruneOrder(vector<StoreEntry>(), 1000).empty());
}
