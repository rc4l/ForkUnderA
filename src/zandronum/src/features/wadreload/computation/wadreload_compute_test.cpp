// [rc4l] Tests for the WAD-reload pure helpers. Every line/branch (the coverage gate enforces 100%
// on *_compute.cpp). Pins the two things that break: the wanted==loaded skip check, and the argv
// rewrite -- especially the array-tail case DArgs::RemoveArgs gets wrong.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/wadreload/computation/wadreload_compute.h"

#include <gtest/gtest.h>

using namespace zx::wadreload;

// ---- NormalizePath ---------------------------------------------------------

TEST(NormalizePath, LowercasesSlashifiesAndTrims)
{
	EXPECT_EQ(NormalizePath("  C:\\Games\\DOOM2.WAD  "), "c:/games/doom2.wad");
	EXPECT_EQ(NormalizePath("/Users/x/FreeDoom2.wad"), "/users/x/freedoom2.wad");
	EXPECT_EQ(NormalizePath(""), "");
	EXPECT_EQ(NormalizePath("   "), "");   // all-whitespace collapses to empty
}

// ---- WantedMatchesLoaded ---------------------------------------------------

TEST(WantedMatches, TrueWhenIwadAndFilesMatchCaseInsensitively)
{
	EXPECT_TRUE(WantedMatchesLoaded("/p/DOOM2.wad", {"/p/A.wad", "/p/B.wad"},
	                                "/p/doom2.WAD", {"/p/a.wad", "/p/b.WAD"}));
	// No pwads on either side, same iwad.
	EXPECT_TRUE(WantedMatchesLoaded("/p/doom2.wad", {}, "/p/doom2.wad", {}));
}

TEST(WantedMatches, FalseOnDifferentIwad)
{
	EXPECT_FALSE(WantedMatchesLoaded("/p/freedoom2.wad", {}, "/p/doom2.wad", {}));
}

TEST(WantedMatches, FalseOnDifferentFileCountOrContentOrOrder)
{
	EXPECT_FALSE(WantedMatchesLoaded("/p/d.wad", {"/p/a.wad"}, "/p/d.wad", {"/p/a.wad", "/p/b.wad"}));
	EXPECT_FALSE(WantedMatchesLoaded("/p/d.wad", {"/p/a.wad"}, "/p/d.wad", {"/p/c.wad"}));
	// Reorder is a real change (load order controls lump overrides).
	EXPECT_FALSE(WantedMatchesLoaded("/p/d.wad", {"/p/a.wad", "/p/b.wad"},
	                                 "/p/d.wad", {"/p/b.wad", "/p/a.wad"}));
}

// ---- IsSwitchToken ---------------------------------------------------------

TEST(IsSwitch, DashAndPlusAreSwitchesEverythingElseIsValue)
{
	EXPECT_TRUE(IsSwitchToken("-iwad"));
	EXPECT_TRUE(IsSwitchToken("+map"));
	EXPECT_FALSE(IsSwitchToken("doom2.wad"));
	EXPECT_FALSE(IsSwitchToken(""));          // empty is a value, not a switch
}

// ---- ComputeReloadArgv -----------------------------------------------------

TEST(ReloadArgv, DropsOldIwadAndFileAppendsNewSetKeepingOtherArgs)
{
	std::vector<std::string> argv = {
		"zandronum", "-iwad", "freedoom2.wad", "-warp", "1",
		"-file", "old1.wad", "old2.wad", "+set", "vid_vsync", "0"
	};
	std::vector<std::string> out = ComputeReloadArgv(
		argv, {"-iwad", "-file"},
		{"-iwad", "doom2.wad", "-file", "new1.wad"});

	std::vector<std::string> expect = {
		"zandronum", "-warp", "1", "+set", "vid_vsync", "0",   // argv[0] + unrelated switches kept
		"-iwad", "doom2.wad", "-file", "new1.wad"              // appended new set
	};
	EXPECT_EQ(out, expect);
}

TEST(ReloadArgv, RemovesFileAtTheVeryEnd_TheDArgsBugCase)
{
	// -file is the last switch and its values run to the end. DArgs::RemoveArgs leaves the final
	// value behind here; ComputeReloadArgv must drop the whole run.
	std::vector<std::string> argv = { "zandronum", "-iwad", "d.wad", "-file", "a.wad", "b.wad" };
	std::vector<std::string> out = ComputeReloadArgv(argv, {"-file"}, {"-file", "z.wad"});
	std::vector<std::string> expect = { "zandronum", "-iwad", "d.wad", "-file", "z.wad" };
	EXPECT_EQ(out, expect);
}

TEST(ReloadArgv, RemovesASwitchThatHasNoValues)
{
	std::vector<std::string> argv = { "zandronum", "-nomonsters", "-iwad", "d.wad" };
	std::vector<std::string> out = ComputeReloadArgv(argv, {"-nomonsters"}, {});
	std::vector<std::string> expect = { "zandronum", "-iwad", "d.wad" };
	EXPECT_EQ(out, expect);
}

TEST(ReloadArgv, TreatsEmptyTokenAsAValueNotASwitchWhenSkipping)
{
	// An empty value token in the middle of -file's run must still be skipped (it's a value).
	std::vector<std::string> argv = { "zandronum", "-file", "a.wad", "", "b.wad", "-warp", "1" };
	std::vector<std::string> out = ComputeReloadArgv(argv, {"-file"}, {});
	std::vector<std::string> expect = { "zandronum", "-warp", "1" };
	EXPECT_EQ(out, expect);
}

TEST(ReloadArgv, EmptyArgvJustYieldsTheAppendedTokens)
{
	std::vector<std::string> out = ComputeReloadArgv({}, {"-file"}, {"-iwad", "d.wad"});
	std::vector<std::string> expect = { "-iwad", "d.wad" };
	EXPECT_EQ(out, expect);
}

TEST(ReloadArgv, NoRemovalNoAppendIsIdentityExceptItRebuilds)
{
	std::vector<std::string> argv = { "zandronum", "-warp", "1" };
	EXPECT_EQ(ComputeReloadArgv(argv, {}, {}), argv);
}

// ---- ClassifyArchiveMagic --------------------------------------------------

static zx::wadreload::ArchiveKind Kind(const char *s, size_t n)
{
	return ClassifyArchiveMagic(reinterpret_cast<const unsigned char *>(s), n);
}

TEST(ArchiveMagic, RecognizesIwadAndPwad)
{
	EXPECT_EQ(Kind("IWAD\x00\x00\x00\x00", 8), ArchiveKind::Wad);
	EXPECT_EQ(Kind("PWAD----", 8), ArchiveKind::Wad);
}

TEST(ArchiveMagic, RecognizesZipPk3Signatures)
{
	EXPECT_EQ(Kind("PK\x03\x04rest", 8), ArchiveKind::Zip);   // normal local-file header (.pk3/.zip)
	EXPECT_EQ(Kind("PK\x05\x06", 4), ArchiveKind::Zip);       // empty archive (end-of-central-dir)
	EXPECT_EQ(Kind("PK\x07\x08", 4), ArchiveKind::Zip);       // spanned marker
}

TEST(ArchiveMagic, RecognizesSevenZip)
{
	const unsigned char sevenz[6] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C };
	EXPECT_EQ(ClassifyArchiveMagic(sevenz, 6), ArchiveKind::SevenZip);
}

TEST(ArchiveMagic, RejectsGarbageTruncatedAndNull)
{
	EXPECT_EQ(Kind("this is not a wad", 17), ArchiveKind::Unknown);   // the corrupt-download case
	EXPECT_EQ(Kind("IWA", 3), ArchiveKind::Unknown);                  // too short for a WAD magic
	EXPECT_EQ(Kind("PK\x01\x02", 4), ArchiveKind::Unknown);           // "PK" but not a real record sig
	EXPECT_EQ(Kind("7z\xBC\xAF", 4), ArchiveKind::Unknown);           // 7z prefix but too short
	EXPECT_EQ(ClassifyArchiveMagic(nullptr, 8), ArchiveKind::Unknown);
	EXPECT_EQ(Kind("", 0), ArchiveKind::Unknown);
}

// ---- ParseMapAssignment ----------------------------------------------------

TEST(MapAssignment, ExtractsValueWithCaseInsensitiveKey)
{
	EXPECT_EQ(ParseMapAssignment("map=MAP02"), "MAP02");
	EXPECT_EQ(ParseMapAssignment("MAP=E1M1"), "E1M1");   // key is case-insensitive
	EXPECT_EQ(ParseMapAssignment("Map=map07"), "map07"); // value preserved verbatim
}

TEST(MapAssignment, EmptyForNonAssignmentsAndEmptyValue)
{
	EXPECT_EQ(ParseMapAssignment("doom2.wad"), "");                 // a wad path, not a map token
	EXPECT_EQ(ParseMapAssignment("/path/to/Judgment.wad"), "");
	EXPECT_EQ(ParseMapAssignment("map="), "");                      // no value -> no map
	EXPECT_EQ(ParseMapAssignment("mapfoo"), "");                    // no '=' -> not an assignment
	EXPECT_EQ(ParseMapAssignment("ma"), "");                        // shorter than the key
	EXPECT_EQ(ParseMapAssignment(""), "");
}
