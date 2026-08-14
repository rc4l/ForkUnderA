// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/iwad-registry/computation/iwadregistry_compute.h"

using zx::IsSafeStoreName;
using zx::IwadStoreDir;
using zx::IwadStorePath;
using zx::NormalizeDigest;

namespace
{

// A real-shaped digest: 64 hex characters.
const char *const kDigest = "062ca2cbe7219958abdfb1a111c087c2d6224400979c12ad33b1f76822e4a4a8";
const char *const kRoot = "C:/Users/x/AppData/Local";

} // namespace

TEST(NormalizeDigest, UpperCaseComesBackLower)
{
	// The whole point. Windows folds case in paths and Linux does not, so the same digest written
	// two ways would be one folder on Windows and two on Linux: a store that silently splits, on
	// the platform where it is hardest to see.
	EXPECT_EQ(std::string(kDigest),
		NormalizeDigest("062CA2CBE7219958ABDFB1A111C087C2D6224400979C12AD33B1F76822E4A4A8"));
}

TEST(NormalizeDigest, AlreadyLowerIsUnchanged)
{
	EXPECT_EQ(std::string(kDigest), NormalizeDigest(kDigest));
}

TEST(NormalizeDigest, AnythingThatIsNotADigestIsRefused)
{
	EXPECT_EQ("", NormalizeDigest(""));
	EXPECT_EQ("", NormalizeDigest("062ca2cb"));                        // truncated
	EXPECT_EQ("", NormalizeDigest(std::string(64, 'z')));              // right length, not hex
	EXPECT_EQ("", NormalizeDigest(std::string(kDigest) + "0"));        // too long
	EXPECT_EQ("", NormalizeDigest(std::string(32, 'a')));              // an MD5, which is not this
}

TEST(IwadStoreDir, IsTheAgreedShape)
{
	EXPECT_EQ(std::string(kRoot) + "/iwads/" + kDigest,
		IwadStoreDir(kRoot, kDigest));
}

TEST(IwadStoreDir, ATrailingSeparatorOnTheRootDoesNotDoubleUp)
{
	// The root comes from the OS, which is inconsistent about this.
	const std::string want = std::string(kRoot) + "/iwads/" + kDigest;

	EXPECT_EQ(want, IwadStoreDir(std::string(kRoot) + "/", kDigest));
	EXPECT_EQ(want, IwadStoreDir(std::string(kRoot) + "\\", kDigest));
}

TEST(IwadStoreDir, NoDigestAndNoRootMeanNoPath)
{
	// A caller whose hash failed must get nothing back rather than a folder to put the file in.
	EXPECT_EQ("", IwadStoreDir(kRoot, "nonsense"));
	EXPECT_EQ("", IwadStoreDir("", kDigest));
}

TEST(IwadStorePath, KeepsTheNameAsTheLeaf)
{
	// The engine identifies IWADs partly by name, so a store of extensionless hashes would be
	// unreadable by everything except us.
	EXPECT_EQ(IwadStoreDir(kRoot, kDigest) + "/doom2.wad",
		IwadStorePath(kRoot, kDigest, "doom2.wad"));
}

TEST(IsSafeStoreName, RefusesAnythingThatCouldLeaveItsFolder)
{
	// The name came off a wire or someone's disk. A separator or a parent reference would put the
	// copy outside its digest folder, which is the one way a content-addressed store can be made
	// to overwrite something that is not itself.
	EXPECT_FALSE(IsSafeStoreName(""));
	EXPECT_FALSE(IsSafeStoreName("."));
	EXPECT_FALSE(IsSafeStoreName(".."));
	EXPECT_FALSE(IsSafeStoreName("../doom2.wad"));
	EXPECT_FALSE(IsSafeStoreName("sub/doom2.wad"));
	EXPECT_FALSE(IsSafeStoreName("sub\\doom2.wad"));
	EXPECT_FALSE(IsSafeStoreName("C:doom2.wad"));
	EXPECT_FALSE(IsSafeStoreName("doom2.wad:stream"));
	EXPECT_FALSE(IsSafeStoreName(std::string("doom2\nwad")));

	EXPECT_TRUE(IsSafeStoreName("doom2.wad"));
	EXPECT_TRUE(IsSafeStoreName("DOOM2.WAD"));
	EXPECT_TRUE(IsSafeStoreName("doom2 (1).wad"));
}

TEST(IwadStorePath, AnUnsafeNameYieldsNoPathAtAll)
{
	EXPECT_EQ("", IwadStorePath(kRoot, kDigest, "../escape.wad"));
	EXPECT_EQ("", IwadStorePath(kRoot, kDigest, ""));

	// And a bad digest is refused even when the name is fine, so neither check can be skipped by
	// getting the other one right.
	EXPECT_EQ("", IwadStorePath(kRoot, "nonsense", "doom2.wad"));
}

TEST(IwadStorePath, TwoBuildsOfOneNameGetTwoFolders)
{
	// Why this is addressed by content. Both are called doom2.wad and they are not the same file;
	// a name-keyed store would have one silently overwrite the other.
	const std::string other = "1d06f91d9a175c84d9fa5646675af72bb2994085eac2a4582bb6ba679c868dd7";

	EXPECT_NE(IwadStorePath(kRoot, kDigest, "doom2.wad"),
		IwadStorePath(kRoot, other, "doom2.wad"));
}
