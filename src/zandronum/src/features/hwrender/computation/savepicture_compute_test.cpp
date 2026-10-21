// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/savepicture_compute.h"

#include <gtest/gtest.h>
#include <string>

namespace {

typedef std::string Bytes;

Bytes Signature()
{
	return Bytes{ '\x89', 'P', 'N', 'G', 13, 10, 26, 10 };
}

const unsigned char *Data(const Bytes &b)
{
	return (const unsigned char *)b.data();
}

// [rc4l] One chunk: its data's length big-endian, its type, the data, and a CRC the splice never reads.
void Chunk(Bytes &png, const char *type, const std::string &data)
{
	const size_t len = data.size();
	png.push_back((unsigned char)(len >> 24)); png.push_back((unsigned char)(len >> 16));
	png.push_back((unsigned char)(len >> 8)); png.push_back((unsigned char)len);
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), data.begin(), data.end());
	png.insert(png.end(), 4, '\xEE');
}

// [rc4l] A save as G_DoSaveGame writes one: the picture, then the game's text and data, then IEND.
Bytes Save(const std::string &pixels)
{
	Bytes png = Signature();
	Chunk(png, "IHDR", "13-byte-head!");
	Chunk(png, "gAMA", "2.2!");
	Chunk(png, "IDAT", pixels);
	Chunk(png, "tEXt", "Title\0save");
	Chunk(png, "snXt", "1");
	Chunk(png, "IEND", "");
	return png;
}

TEST(SavePicture, TheLengthCoversTheLeadingPictureChunks)
{
	const Bytes save = Save("old");
	// [rc4l] Signature, then IHDR (13), gAMA (4) and IDAT (3), each with 12 bytes around it.
	EXPECT_EQ(zx::ComputePngPictureLength(Data(save), save.size()), 8u + 25 + 16 + 15);
}

TEST(SavePicture, APictureWithAPaletteKeepsIt)
{
	Bytes png = Signature();
	Chunk(png, "IHDR", "h");
	Chunk(png, "PLTE", "rgb");
	Chunk(png, "IDAT", "p");
	EXPECT_EQ(zx::ComputePngPictureLength(Data(png), png.size()), png.size());
}

TEST(SavePicture, NoPngHasNoPicture)
{
	const Bytes text = "not a png";
	EXPECT_EQ(zx::ComputePngPictureLength(Data(text), text.size()), 0u);
	const Bytes shortOne{ '\x89', 'P', 'N' };
	EXPECT_EQ(zx::ComputePngPictureLength(Data(shortOne), shortOne.size()), 0u);
	EXPECT_EQ(zx::ComputePngPictureLength(nullptr, 0), 0u);
}

TEST(SavePicture, APngThatStartsWithAnythingElseHasNoPicture)
{
	Bytes png = Signature();
	Chunk(png, "tEXt", "first");
	EXPECT_EQ(zx::ComputePngPictureLength(Data(png), png.size()), 0u);
	const Bytes bare = Signature();
	EXPECT_EQ(zx::ComputePngPictureLength(Data(bare), bare.size()), 0u);
}

TEST(SavePicture, AChunkRunningPastTheEndIsMalformed)
{
	Bytes png = Signature();
	Chunk(png, "IHDR", "head");
	png.resize(png.size() - 2);
	EXPECT_EQ(zx::ComputePngPictureLength(Data(png), png.size()), 0u);
	// [rc4l] A length and type with no room left for the CRC.
	Bytes cut = Signature();
	cut += std::string("\0\0\0\0IDAT", 8);
	EXPECT_EQ(zx::ComputePngPictureLength(Data(cut), cut.size()), 0u);
}

TEST(SavePicture, TheSpliceTakesTheNewPictureAndKeepsTheGame)
{
	const Bytes save = Save("old");
	Bytes picture = Signature();
	Chunk(picture, "IHDR", "new head");
	Chunk(picture, "IDAT", "new pixels");
	Chunk(picture, "IEND", "");
	Bytes out;
	ASSERT_TRUE(zx::ComputeSpliceSavePicture(save, picture, out));
	const size_t newPicture = zx::ComputePngPictureLength(Data(picture), picture.size());
	const size_t oldPicture = zx::ComputePngPictureLength(Data(save), save.size());
	EXPECT_EQ(out.substr(0, newPicture), picture.substr(0, newPicture));
	EXPECT_EQ(out.substr(newPicture), save.substr(oldPicture));
	EXPECT_EQ(zx::ComputePngPictureLength(Data(out), out.size()), newPicture);
}

TEST(SavePicture, AMalformedSideLeavesTheOutputAlone)
{
	const Bytes save = Save("old");
	const Bytes broken = "abc";
	Bytes out = "kept";
	EXPECT_FALSE(zx::ComputeSpliceSavePicture(broken, save, out));
	EXPECT_FALSE(zx::ComputeSpliceSavePicture(save, broken, out));
	EXPECT_EQ(out, "kept");
}

} // namespace
