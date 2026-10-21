// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/command-safety/computation/commandsafety_compute.h"

#include <gtest/gtest.h>

using zx::ComputeFileBeside;
using zx::ComputeMenuCommandUnsafe;
using zx::ComputeOwnExtension;
using zx::ComputePlainFileName;

TEST(ComputeOwnExtension, AppendsTheExtensionToABareName)
{
	EXPECT_EQ("server.log", ComputeOwnExtension("server", {".log"}));
	EXPECT_EQ("logs/server.log", ComputeOwnExtension("logs/server", {".log"}));
}

TEST(ComputeOwnExtension, KeepsANameThatAlreadyEndsInItInAnyCase)
{
	EXPECT_EQ("out.log", ComputeOwnExtension("out.log", {".log"}));
	EXPECT_EQ("SONG.MID", ComputeOwnExtension("SONG.MID", {".mid"}));
}

TEST(ComputeOwnExtension, AppendsAfterAnyOtherExtension)
{
	// [rc4l] Another kind of file's extension is kept as part of the name, so the file is still this command's kind.
	EXPECT_EQ("notes.txt.log", ComputeOwnExtension("notes.txt", {".log"}));
}

TEST(ComputeOwnExtension, AppendsToANameShorterThanTheExtension)
{
	EXPECT_EQ("a.wad", ComputeOwnExtension("a", {".wad"}));
	EXPECT_EQ(".wad", ComputeOwnExtension("", {".wad"}));
}

TEST(ComputeOwnExtension, KeepsAnyOfSeveralAndAppendsTheFirst)
{
	// [rc4l] writeopl writes DOSBox's format for a .dro name and the RDOS one otherwise.
	EXPECT_EQ("e1m1.dro", ComputeOwnExtension("e1m1.dro", {".raw", ".dro"}));
	EXPECT_EQ("e1m1.raw", ComputeOwnExtension("e1m1.raw", {".raw", ".dro"}));
	EXPECT_EQ("e1m1.raw", ComputeOwnExtension("e1m1", {".raw", ".dro"}));
}

TEST(ComputePlainFileName, KeepsAPlainName)
{
	EXPECT_EQ("my backup_2", ComputePlainFileName("my backup_2"));
}

TEST(ComputePlainFileName, TurnsPathDriveAndExtensionCharactersIntoDashes)
{
	EXPECT_EQ("------x-", ComputePlainFileName("../..\\x:"));
	EXPECT_EQ("a-b-c-d-e-f-g", ComputePlainFileName("a*b?c\"d<e>f|g"));
}

TEST(ComputePlainFileName, TurnsControlCharactersIntoDashes)
{
	EXPECT_EQ("a-b-c", ComputePlainFileName("a\nb\tc"));
}

TEST(ComputePlainFileName, KeepsBytesBeyondAscii)
{
	// [rc4l] UTF-8 never encodes a separator inside a multi-byte character, so a name in any script survives.
	EXPECT_EQ("\xD0\xBA\xD0\xBE\xD1\x82", ComputePlainFileName("\xD0\xBA\xD0\xBE\xD1\x82"));
}

TEST(ComputeFileBeside, PutsTheNameInTheAnchorsFolder)
{
	EXPECT_EQ("C:/Games/FUA/backup.ini", ComputeFileBeside("C:/Games/FUA/forkundera-me.ini", "backup", ".ini"));
	EXPECT_EQ("/srv/fua/.config/fua/backup.ini", ComputeFileBeside("/srv/fua/.config/fua/forkundera.ini", "backup", ".ini"));
}

TEST(ComputeFileBeside, UnderstandsBackslashFolders)
{
	EXPECT_EQ("C:\\Games\\FUA\\backup.ini", ComputeFileBeside("C:\\Games\\FUA\\forkundera.ini", "backup", ".ini"));
}

TEST(ComputeFileBeside, UsesTheCurrentFolderWhenTheAnchorHasNone)
{
	EXPECT_EQ("backup.ini", ComputeFileBeside("forkundera.ini", "backup", ".ini"));
}

TEST(ComputeFileBeside, DropsATrailingExtensionBeforeMakingTheNamePlain)
{
	EXPECT_EQ("/cfg/backup.ini", ComputeFileBeside("/cfg/a.ini", "backup.INI", ".ini"));
}

TEST(ComputeFileBeside, NeverLeavesTheAnchorsFolder)
{
	EXPECT_EQ("/cfg/------x-bat.ini", ComputeFileBeside("/cfg/a.ini", "../../x.bat", ".ini"));
	EXPECT_EQ("/cfg/C--elsewhere-x.ini", ComputeFileBeside("/cfg/a.ini", "C:\\elsewhere\\x", ".ini"));
}

TEST(ComputeFileBeside, IsEmptyWhenNothingOfTheNameIsLeft)
{
	EXPECT_EQ("", ComputeFileBeside("/cfg/a.ini", "", ".ini"));
	EXPECT_EQ("", ComputeFileBeside("/cfg/a.ini", ".ini", ".ini"));
}

TEST(ComputeMenuCommandUnsafe, TrustsOnlyAConfirmedItemFromTheEnginesOwnPk3)
{
	EXPECT_FALSE(ComputeMenuCommandUnsafe(true, 0, 1));
}

TEST(ComputeMenuCommandUnsafe, AnItemWithoutConfirmationIsUnsafeEvenFromTheEngine)
{
	EXPECT_TRUE(ComputeMenuCommandUnsafe(false, 0, 1));
}

TEST(ComputeMenuCommandUnsafe, AConfirmedItemFromTheIwadOrAModIsUnsafe)
{
	EXPECT_TRUE(ComputeMenuCommandUnsafe(true, 1, 1));
	EXPECT_TRUE(ComputeMenuCommandUnsafe(true, 4, 1));
}

TEST(ComputeMenuCommandUnsafe, AnItemFromNoLumpIsUnsafe)
{
	EXPECT_TRUE(ComputeMenuCommandUnsafe(true, -1, 1));
}

// [rc4l] `save complex` in the browser wrote /complex.zds, outside the home the page keeps, and a reload lost it; a bare name now goes in the save directory, as uzdoom's does.
TEST(CommandSafety, ABareNameHasNoFolderOrDrive)
{
	EXPECT_TRUE(zx::ComputeBareFileName("complex.zds"));
	EXPECT_TRUE(zx::ComputeBareFileName("heavy"));
	EXPECT_FALSE(zx::ComputeBareFileName("saves/heavy.zds"));
	EXPECT_FALSE(zx::ComputeBareFileName("saves\\heavy.zds"));
	EXPECT_FALSE(zx::ComputeBareFileName("C:heavy.zds"));
	EXPECT_FALSE(zx::ComputeBareFileName("/home/web_user/a.zds"));
}
