// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/wad-download/computation/fileresolve_compute.h"

using namespace zx;

namespace
{

const char *const kMd5 = "0123456789abcdef0123456789abcdef";
const char *const kOtherMd5 = "ffffffffffffffffffffffffffffffff";

std::vector<std::string> Hits()
{
	return std::vector<std::string>();
}

std::vector<std::string> Hits(const std::string &a)
{
	std::vector<std::string> v;
	v.push_back(a);
	return v;
}

std::vector<std::string> Hits(const std::string &a, const std::string &b)
{
	std::vector<std::string> v;
	v.push_back(a);
	v.push_back(b);
	return v;
}

} // namespace

// ---------------------------------------------------------------- the order

TEST(FileResolve, StoreFirstThenFlatThenTheSearchHits)
{
	const std::vector<ResolveStep> steps =
		PlanFileResolve("test.wad", kMd5, "C:/dl/", Hits("D:/wads/test.wad"));

	ASSERT_EQ(3u, steps.size());

	EXPECT_EQ(std::string("C:/dl/by-hash/") + kMd5 + "/test.wad", steps[0].path);
	EXPECT_EQ(ResolveCheck::Stat, steps[0].check) << "the path names the digest, so existing is enough";

	EXPECT_EQ("C:/dl/test.wad", steps[1].path);
	EXPECT_EQ(ResolveCheck::Hash, steps[1].check);

	EXPECT_EQ("D:/wads/test.wad", steps[2].path);
	EXPECT_EQ(ResolveCheck::Hash, steps[2].check);
}

TEST(FileResolve, TheStoreIsTheOnlyStepThatCostsNoRead)
{
	// The point of the whole arrangement: whatever else changes, exactly one step is free, and it is
	// the one that answers for every file we downloaded ourselves.
	const std::vector<ResolveStep> steps =
		PlanFileResolve("big.pk3", kMd5, "C:/dl/", Hits("A:/a/big.pk3", "B:/b/big.pk3"));

	int stats = 0;
	for (size_t i = 0; i < steps.size(); ++i)
	{
		if (steps[i].check == ResolveCheck::Stat)
			++stats;
	}

	EXPECT_EQ(1, stats);
	EXPECT_EQ(ResolveCheck::Stat, steps[0].check) << "and it must come first, or it saves nothing";
}

TEST(FileResolve, SearchHitsKeepTheOrderTheEngineFoundThemIn)
{
	const std::vector<ResolveStep> steps =
		PlanFileResolve("a.wad", kMd5, "", Hits("first/a.wad", "second/a.wad"));

	ASSERT_EQ(2u, steps.size());
	EXPECT_EQ("first/a.wad", steps[0].path);
	EXPECT_EQ("second/a.wad", steps[1].path);
}

// ---------------------------------------------------------------- nothing to plan

TEST(FileResolve, NoDigestMeansNoPlanAtAll)
{
	// Not "a plan that happens to fail". The caller has to be able to tell the difference and fall
	// back to a name search knowingly.
	EXPECT_TRUE(PlanFileResolve("test.wad", "", "C:/dl/", Hits("D:/test.wad")).empty());
}

TEST(FileResolve, ADigestOfTheWrongLengthIsNoDigest)
{
	EXPECT_TRUE(PlanFileResolve("test.wad", "abc", "C:/dl/", Hits()).empty());
	EXPECT_TRUE(PlanFileResolve("test.wad", std::string(64, 'a'), "C:/dl/", Hits()).empty())
		<< "a sha-256 is not an md5, and the store is keyed on md5";
}

TEST(FileResolve, ADigestThatIsNotHexIsNoDigest)
{
	EXPECT_TRUE(PlanFileResolve("test.wad", std::string(32, 'z'), "C:/dl/", Hits()).empty());
}

TEST(FileResolve, AnEmptyNameHasNothingToLookFor)
{
	EXPECT_TRUE(PlanFileResolve("", kMd5, "C:/dl/", Hits("D:/x.wad")).empty());
}

TEST(FileResolve, NoDownloadFolderAndNoHitsIsAnEmptyPlan)
{
	EXPECT_TRUE(PlanFileResolve("test.wad", kMd5, "", Hits()).empty());
}

// ---------------------------------------------------------------- unsafe names

TEST(FileResolve, ANameWithTraversalIsNeverJoinedOntoOurFolder)
{
	// The digest is fine here, so the empty store path can only mean the name is not one we build
	// paths from. Neither of our own two steps may use it, including the flat one, which does not
	// go through StoredRelativePath itself.
	const std::vector<ResolveStep> steps =
		PlanFileResolve("../evil.wad", kMd5, "C:/dl/", Hits("D:/wads/evil.wad"));

	ASSERT_EQ(1u, steps.size()) << "only the hit the engine found, which is a path we did not build";
	EXPECT_EQ("D:/wads/evil.wad", steps[0].path);
}

TEST(FileResolve, ANameWithASeparatorIsRefusedTheSameWay)
{
	const std::vector<ResolveStep> steps = PlanFileResolve("sub/test.wad", kMd5, "C:/dl/", Hits());
	EXPECT_TRUE(steps.empty());
}

// ---------------------------------------------------------------- deduplication

TEST(FileResolve, TheDownloadFolderIsNotHashedTwiceWhenItIsAlsoASearchPath)
{
	// RegisterDownloadDirInSearchPath puts our folder in FileSearch.Directories, so the engine finds
	// the flat copy too. Without dedup that is a second full read of the same file.
	const std::vector<ResolveStep> steps =
		PlanFileResolve("test.wad", kMd5, "C:/dl/", Hits("C:/dl/test.wad"));

	ASSERT_EQ(2u, steps.size());
	EXPECT_EQ(std::string("C:/dl/by-hash/") + kMd5 + "/test.wad", steps[0].path);
	EXPECT_EQ("C:/dl/test.wad", steps[1].path);
}

TEST(FileResolve, DedupIsBlindToSeparatorAndCase)
{
	// The same file reaches us spelled both ways depending on who resolved it.
	const std::vector<ResolveStep> steps =
		PlanFileResolve("test.wad", kMd5, "C:/dl/", Hits("C:\\DL\\Test.wad"));

	ASSERT_EQ(2u, steps.size()) << "C:\\DL\\Test.wad is C:/dl/test.wad";
}

TEST(FileResolve, TwoHitsForTheSameFileCollapse)
{
	const std::vector<ResolveStep> steps =
		PlanFileResolve("a.wad", kMd5, "", Hits("X:/a.wad", "x:\\A.WAD"));

	ASSERT_EQ(1u, steps.size());
}

TEST(FileResolve, DifferentCopiesOfOneNameAreAllKept)
{
	// The case the whole unit exists for: four test.wads, and only the digest says which is meant.
	std::vector<std::string> hits;
	hits.push_back("A:/one/test.wad");
	hits.push_back("B:/two/test.wad");
	hits.push_back("C:/three/test.wad");

	const std::vector<ResolveStep> steps = PlanFileResolve("test.wad", kMd5, "", hits);
	ASSERT_EQ(3u, steps.size());
}

TEST(FileResolve, AnEmptyHitIsSkipped)
{
	const std::vector<ResolveStep> steps = PlanFileResolve("a.wad", kMd5, "", Hits("", "X:/a.wad"));

	ASSERT_EQ(1u, steps.size());
	EXPECT_EQ("X:/a.wad", steps[0].path);
}

// ---------------------------------------------------------------- joining

TEST(FileResolve, ADownloadFolderWithoutATrailingSeparatorStillJoins)
{
	const std::vector<ResolveStep> steps = PlanFileResolve("a.wad", kMd5, "C:/dl", Hits());

	ASSERT_EQ(2u, steps.size());
	EXPECT_EQ(std::string("C:/dl/by-hash/") + kMd5 + "/a.wad", steps[0].path);
	EXPECT_EQ("C:/dl/a.wad", steps[1].path);
}

TEST(FileResolve, ABackslashTerminatedFolderIsNotGivenASecondSeparator)
{
	const std::vector<ResolveStep> steps = PlanFileResolve("a.wad", kMd5, "C:\\dl\\", Hits());

	ASSERT_EQ(2u, steps.size());
	EXPECT_EQ("C:\\dl\\a.wad", steps[1].path);
}

TEST(FileResolve, TheDigestIsLowercasedIntoTheStorePath)
{
	// Same bytes, so it must be the same folder however the server spelled the hash.
	const std::vector<ResolveStep> upper =
		PlanFileResolve("a.wad", "0123456789ABCDEF0123456789ABCDEF", "C:/dl/", Hits());

	ASSERT_FALSE(upper.empty());
	EXPECT_EQ(std::string("C:/dl/by-hash/") + kMd5 + "/a.wad", upper[0].path);
}

TEST(FileResolve, ADifferentDigestIsADifferentFolder)
{
	const std::vector<ResolveStep> a = PlanFileResolve("test.wad", kMd5, "C:/dl/", Hits());
	const std::vector<ResolveStep> b = PlanFileResolve("test.wad", kOtherMd5, "C:/dl/", Hits());

	ASSERT_FALSE(a.empty());
	ASSERT_FALSE(b.empty());
	EXPECT_NE(a[0].path, b[0].path) << "which is what stops two test.wads clobbering each other";
}

TEST(FileResolve, ADefaultStepReadsAsTheExpensiveKind)
{
	// If a step is ever built without saying, it must cost more than it should rather than skip a
	// check it should have made.
	const ResolveStep fresh;
	EXPECT_EQ(ResolveCheck::Hash, fresh.check);
	EXPECT_TRUE(fresh.path.empty());
}
