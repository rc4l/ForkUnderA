// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/fileio/computation/readfile_compute.h"

#include <gtest/gtest.h>

namespace {

// [rc4l] Bytes over several of the reader's chunks, a zero among them.
std::string Bytes()
{
	std::string b(10000, 'x');
	b[5000] = '\0';
	return b;
}

// [rc4l] A file of its own beside the tests, removed when the test ends.
struct TempFile
{
	const char *path = "readfile_compute_test.tmp";
	explicit TempFile(const std::string &data)
	{
		FILE *f = fopen(path, "wb");
		fwrite(data.data(), 1, data.size(), f);
		fclose(f);
	}
	~TempFile() { remove(path); }
};

TEST(ReadFile, AnOpenFileIsReadWholeFromWhereItStands)
{
	FILE *f = tmpfile();
	ASSERT_NE(f, nullptr);
	const std::string data = Bytes();
	fwrite(data.data(), 1, data.size(), f);
	rewind(f);
	std::string out = "left over";
	EXPECT_TRUE(zx::ComputeReadWhole(f, out));
	EXPECT_EQ(out, data);
	fseek(f, 9000, SEEK_SET);
	EXPECT_TRUE(zx::ComputeReadWhole(f, out));
	EXPECT_EQ(out, data.substr(9000));
	fclose(f);
}

TEST(ReadFile, MoreThanTheCapIsRefused)
{
	FILE *f = tmpfile();
	ASSERT_NE(f, nullptr);
	fwrite("123456", 1, 6, f);
	rewind(f);
	std::string out;
	EXPECT_FALSE(zx::ComputeReadWhole(f, out, 5));
	rewind(f);
	EXPECT_TRUE(zx::ComputeReadWhole(f, out, 6));
	EXPECT_EQ(out, "123456");
	fclose(f);
}

TEST(ReadFile, AFileThatCannotBeReadSaysSo)
{
	TempFile temp("abc");
	FILE *f = fopen(temp.path, "ab");
	ASSERT_NE(f, nullptr);
	std::string out;
	EXPECT_FALSE(zx::ComputeReadWhole(f, out));
	fclose(f);
}

TEST(ReadFile, APathIsOpenedReadAndClosed)
{
	const std::string data = Bytes();
	TempFile temp(data);
	std::string out;
	EXPECT_TRUE(zx::ComputeReadWholeFile(temp.path, out));
	EXPECT_EQ(out, data);
	EXPECT_FALSE(zx::ComputeReadWholeFile(temp.path, out, 100));
}

TEST(ReadFile, AFilesTextIsItsBytesOrNothing)
{
	TempFile temp("text");
	EXPECT_EQ(zx::ComputeFileText(temp.path), "text");
	EXPECT_EQ(zx::ComputeFileText("no/such/readfile_compute_test.tmp"), "");
}

TEST(ReadFile, NoFileIsNoRead)
{
	std::string out = "left over";
	EXPECT_FALSE(zx::ComputeReadWholeFile("no/such/readfile_compute_test.tmp", out));
	EXPECT_EQ(out, "");
	EXPECT_FALSE(zx::ComputeReadWholeFile(nullptr, out));
}

} // namespace
