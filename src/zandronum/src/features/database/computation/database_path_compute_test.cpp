#include "features/database/computation/database_path_compute.h"
#include "features/database/computation/database_outbox_compute.h"
#include "features/database/computation/database_test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

using namespace zxdbtest;

TEST(DatabasePath, StripsTheVerbatimPrefix)
{
	EXPECT_EQ(zx::ComputeDatabaseStripVerbatimPrefix(R"(\\?\C:\Data\chars.db)"), R"(C:\Data\chars.db)");
	EXPECT_EQ(zx::ComputeDatabaseStripVerbatimPrefix(R"(\\?\UNC\server\share\chars.db)"), R"(\\server\share\chars.db)");
	EXPECT_EQ(zx::ComputeDatabaseStripVerbatimPrefix(R"(C:\Data\chars.db)"), R"(C:\Data\chars.db)");
	EXPECT_EQ(zx::ComputeDatabaseStripVerbatimPrefix("/srv/chars.db"), "/srv/chars.db");
	EXPECT_EQ(zx::ComputeDatabaseStripVerbatimPrefix(""), "");
}

TEST(DatabasePath, ResolvesAnExistingFileToItsCanonicalPath)
{
	const std::filesystem::path file = tempDatabasePath("path_real");
	std::ofstream(file) << "x";
	const std::string relativeSpelling = (file.parent_path() / "." / file.filename()).string();
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem(zx::ComputeDatabaseResolvedPath(relativeSpelling)), zx::ComputeDatabaseOutboxStem(std::filesystem::canonical(file).string()));
	std::error_code ignored;
	std::filesystem::remove(file, ignored);
}

TEST(DatabasePath, AFileThatIsNotThereKeepsThePathGiven)
{
	const std::string missing = tempDatabasePath("path_missing").string();
	EXPECT_EQ(zx::ComputeDatabaseResolvedPath(missing), missing);
}

// [Doruk-Aksoy] The case the resolution is for: a database reached through a symlink names its outboxes beside the real file.
TEST(DatabasePath, ASymlinkResolvesToItsTarget)
{
	const std::filesystem::path target = tempDatabasePath("path_target");
	const std::filesystem::path link = tempDatabasePath("path_link");
	std::ofstream(target) << "x";
	std::error_code made;
	std::filesystem::create_symlink(target, link, made);
	if (made)
	{
		std::error_code ignored;
		std::filesystem::remove(target, ignored);
		GTEST_SKIP() << "no symlinks here without the privilege: " << made.message();
	}
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem(zx::ComputeDatabaseResolvedPath(link.string())), zx::ComputeDatabaseOutboxStem(std::filesystem::canonical(target).string()));
	std::error_code ignored;
	std::filesystem::remove(link, ignored);
	std::filesystem::remove(target, ignored);
}
