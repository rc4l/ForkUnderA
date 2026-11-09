// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "features/wad-folders/computation/wadfolders_compute.h"

using zx::ComputeWadFolderAdd;
using zx::ComputeWadFolderNeedsRefresh;
using zx::ComputeWadFolderNormalize;
using zx::ComputeWadFolderRemove;
using zx::ComputeWadFolderSame;
using zx::ComputeWadFolderWalk;
using zx::WadFolderEntry;
using zx::WadFolderIndex;

namespace
{

// [rc4l] A disk held in a map, folder to entries; a folder missing from it cannot be read.
struct FakeDisk
{
	std::map<std::string, std::vector<WadFolderEntry> > dirs;
	std::vector<std::string> listed;

	zx::WadFolderLister Lister()
	{
		return [this](const std::string &dir, std::vector<WadFolderEntry> &out) {
			listed.push_back(dir);
			const std::map<std::string, std::vector<WadFolderEntry> >::const_iterator it = dirs.find(dir);
			if (it == dirs.end())
				return false;
			out = it->second;
			return true;
		};
	}
};

WadFolderEntry File(const char *name) { return WadFolderEntry{ name, false }; }
WadFolderEntry Dir(const char *name) { return WadFolderEntry{ name, true }; }

bool Never() { return false; }

std::vector<std::string> FindIn(const WadFolderIndex &index, const char *name)
{
	std::vector<std::string> out;
	index.Find(name, out);
	return out;
}

} // namespace

TEST(WadFoldersCompute, NormalizeUsesForwardSlashesAndDropsTrailingOnes)
{
	EXPECT_EQ("C:/wads/dump", ComputeWadFolderNormalize("C:\\wads\\dump\\"));
	EXPECT_EQ("/home/me/wads", ComputeWadFolderNormalize("/home/me/wads//"));
	EXPECT_EQ("C:/", ComputeWadFolderNormalize("C:\\"));
	EXPECT_EQ("C:/", ComputeWadFolderNormalize("C://"));
	EXPECT_EQ("/", ComputeWadFolderNormalize("/"));
	EXPECT_EQ("/", ComputeWadFolderNormalize("///"));
	EXPECT_EQ("", ComputeWadFolderNormalize(""));
}

TEST(WadFoldersCompute, SameFoldsCaseOnlyWhenAsked)
{
	EXPECT_TRUE(ComputeWadFolderSame("C:\\Wads\\", "c:/wads", true));
	EXPECT_FALSE(ComputeWadFolderSame("/home/Wads", "/home/wads", false));
	EXPECT_TRUE(ComputeWadFolderSame("/home/wads/", "/home/wads", false));
}

TEST(WadFoldersCompute, AddNormalizesAndRefusesEmptyAndDuplicates)
{
	std::vector<std::string> folders;
	EXPECT_TRUE(ComputeWadFolderAdd(folders, "D:\\dump\\", true));
	EXPECT_FALSE(ComputeWadFolderAdd(folders, "d:/DUMP", true));
	EXPECT_FALSE(ComputeWadFolderAdd(folders, "", true));
	EXPECT_TRUE(ComputeWadFolderAdd(folders, "D:/other", true));
	ASSERT_EQ(2u, folders.size());
	EXPECT_EQ("D:/dump", folders[0]);
	EXPECT_EQ("D:/other", folders[1]);
}

TEST(WadFoldersCompute, RemoveDropsOnlyAListedIndex)
{
	std::vector<std::string> folders = { "/a", "/b", "/c" };
	EXPECT_TRUE(ComputeWadFolderRemove(folders, 1));
	EXPECT_EQ((std::vector<std::string>{ "/a", "/c" }), folders);
	EXPECT_FALSE(ComputeWadFolderRemove(folders, 2));
	EXPECT_EQ(2u, folders.size());
}

TEST(WadFoldersCompute, IndexFindsEveryCopyByNameInAnyCase)
{
	WadFolderIndex index;
	index.Add("Sunder.WAD", "/a/Sunder.WAD");
	index.Add("sunder.wad", "/b/sunder.wad");
	EXPECT_EQ(2u, index.Size());
	EXPECT_EQ((std::vector<std::string>{ "/a/Sunder.WAD", "/b/sunder.wad" }), FindIn(index, "SUNDER.wad"));
	EXPECT_TRUE(FindIn(index, "other.wad").empty());
}

TEST(WadFoldersCompute, WalkIndexesLoadableFilesInSubfoldersAndSkipsTheRest)
{
	FakeDisk disk;
	disk.dirs["D:/dump"] = { File("readme.txt"), Dir("s"), File("aaa.pk3") };
	disk.dirs["D:/dump/s"] = { File("sunder.wad"), Dir("unreadable") };

	WadFolderIndex index;
	EXPECT_FALSE(ComputeWadFolderWalk({ "D:\\dump\\" }, disk.Lister(), 5, 100, Never, index));
	EXPECT_EQ(2u, index.Size());
	EXPECT_EQ((std::vector<std::string>{ "D:/dump/s/sunder.wad" }), FindIn(index, "sunder.wad"));
	EXPECT_EQ((std::vector<std::string>{ "D:/dump/aaa.pk3" }), FindIn(index, "aaa.pk3"));
	EXPECT_TRUE(FindIn(index, "readme.txt").empty());
}

TEST(WadFoldersCompute, WalkFromARootDoesNotDoubleTheSlash)
{
	FakeDisk disk;
	disk.dirs["/"] = { File("doom2.wad") };
	WadFolderIndex index;
	ComputeWadFolderWalk({ "/" }, disk.Lister(), 5, 100, Never, index);
	EXPECT_EQ((std::vector<std::string>{ "/doom2.wad" }), FindIn(index, "doom2.wad"));
}

TEST(WadFoldersCompute, WalkStopsAtTheDepthCap)
{
	FakeDisk disk;
	disk.dirs["/d"] = { Dir("1"), File("top.wad") };
	disk.dirs["/d/1"] = { Dir("2"), File("one.wad") };
	disk.dirs["/d/1/2"] = { File("two.wad") };

	WadFolderIndex index;
	ComputeWadFolderWalk({ "/d" }, disk.Lister(), 1, 100, Never, index);
	EXPECT_EQ(2u, index.Size());
	EXPECT_TRUE(FindIn(index, "two.wad").empty());
}

TEST(WadFoldersCompute, WalkReportsTheFileCapAndStopsEveryRoot)
{
	FakeDisk disk;
	disk.dirs["/a"] = { File("1.wad"), File("2.wad"), Dir("sub") };
	disk.dirs["/a/sub"] = { File("3.wad") };
	disk.dirs["/b"] = { File("4.wad") };

	WadFolderIndex index;
	EXPECT_TRUE(ComputeWadFolderWalk({ "/a", "/b" }, disk.Lister(), 5, 2, Never, index));
	EXPECT_EQ(2u, index.Size());
	EXPECT_EQ((std::vector<std::string>{ "/a", "/a/sub" }), disk.listed);
}

TEST(WadFoldersCompute, WalkStopsWhenCancelled)
{
	FakeDisk disk;
	disk.dirs["/a"] = { File("1.wad"), Dir("sub") };
	disk.dirs["/a/sub"] = { File("2.wad") };

	int calls = 0;
	WadFolderIndex index;
	ComputeWadFolderWalk({ "/a" }, disk.Lister(), 5, 100, [&calls]() { return ++calls > 1; }, index);
	EXPECT_EQ(1u, index.Size());
	EXPECT_EQ((std::vector<std::string>{ "/a" }), disk.listed);
}

TEST(WadFoldersCompute, LabelKeepsAShortFolderWhole)
{
	EXPECT_EQ("D:/wads", zx::ComputeWadFolderLabel("D:\\wads\\", 20));
}

TEST(WadFoldersCompute, LabelTrimsTheFrontAtASeparator)
{
	EXPECT_EQ(".../scratchpad/dump", zx::ComputeWadFolderLabel("C:/Users/me/AppData/Local/Temp/scratchpad/dump", 20));
}

TEST(WadFoldersCompute, LabelCutsALastComponentTooLongForTheRoom)
{
	EXPECT_EQ("...ghijkl", zx::ComputeWadFolderLabel("C:/abcdefghijkl", 9));
}

TEST(WadFoldersCompute, LabelInARoomTooSmallForTheEllipsisKeepsTheEnd)
{
	EXPECT_EQ("ump", zx::ComputeWadFolderLabel("C:/dump", 3));
}

TEST(WadFoldersCompute, RefreshOnlyAfterAMissOnAnIndexOldEnough)
{
	EXPECT_TRUE(ComputeWadFolderNeedsRefresh(true, 60.0, 60.0));
	EXPECT_FALSE(ComputeWadFolderNeedsRefresh(true, 59.0, 60.0));
	EXPECT_FALSE(ComputeWadFolderNeedsRefresh(false, 600.0, 60.0));
}
