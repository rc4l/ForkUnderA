#include "features/database/computation/database_outbox_compute.h"
#include "features/database/computation/database_test_support.h"
#include "features/database/computation/database_writer_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <string>
#include <utility>
#include <vector>

using namespace zxdbtest;


namespace
{

zx::DatabaseBatch batch(uint64_t seq, double stamp, std::vector<zx::DatabaseOp> ops)
{
	zx::DatabaseBatch made;
	made.seq = seq;
	made.stamp = stamp;
	made.ops = std::move(ops);
	return made;
}

bool sameBatches(const std::vector<zx::DatabaseBatch> &a, const std::vector<zx::DatabaseBatch> &b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].seq != b[i].seq || a[i].stamp != b[i].stamp || !(a[i].ops == b[i].ops))
			return false;
	return true;
}

std::string value(sqlite3 *db, const char *key)
{
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, db);
	std::string text;
	bool found = false;
	zx::ComputeDatabaseGet(statements, "ns", key, text, found);
	return found ? text : "<missing>";
}

const std::vector<zx::DatabaseBatch> kBatches = {
	batch(1, 10.5, { zx::ComputeDatabaseSetOp("ns", "a", "one"), zx::ComputeDatabaseIncrementOp("ns", "n", -3) }),
	batch(2, 11.5, { zx::ComputeDatabaseSetOp("ns", "a", "") }),
};

}

TEST(DatabaseOutbox, PathsSitBesideTheSharedFile)
{
	EXPECT_EQ(zx::ComputeDatabaseOutboxPath("C:/data/chars.db", 3), "C:/data/chars.db.fua-outbox-3");
}

TEST(DatabaseOutbox, EverySpellingOfAPathGivesOneStem)
{
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem(R"(C:\Data\.\db\..\Chars.db)"), "C:/Data/Chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("C:/Data//Chars.db"), "C:/Data/Chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("/srv/./zan//chars.db"), "/srv/zan/chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("/srv/zan/old/../chars.db"), "/srv/zan/chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("/../chars.db"), "/chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("../up/chars.db"), "../up/chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("a/../../b.db"), "../b.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem("c:chars.db"), "c:chars.db");
	EXPECT_EQ(zx::ComputeDatabaseOutboxStem(""), "");
}

// [Doruk-Aksoy] Two files only a case-sensitive volume tells apart must never share outboxes, or one's orphans would be replayed into the other.
TEST(DatabaseOutbox, TheStemKeepsCase)
{
	EXPECT_NE(zx::ComputeDatabaseOutboxStem("/srv/A.db"), zx::ComputeDatabaseOutboxStem("/srv/a.db"));
}

TEST(DatabaseOutbox, OneProcessHoldsAnOutboxAtATime)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_lock");
	const std::string path = zx::ComputeDatabaseOutboxPath(base.string(), 0);
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	{
		zx::DatabaseOutbox first, second;
		EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(first, path, true, leftover, next), zx::DatabaseOutboxOpen::Opened);
		EXPECT_EQ(first.server.size(), 32u);
		EXPECT_EQ(next, 1u);
		EXPECT_TRUE(leftover.empty());
		EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(second, path, true, leftover, next), zx::DatabaseOutboxOpen::Busy);
		EXPECT_EQ(second.db, nullptr);
	}
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, MissingAndUnmakeableOutboxesDoNotOpen)
{
	ZX_REQUIRE_FILE_DATABASES();
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	zx::DatabaseOutbox outbox;
	const std::string missing = tempDatabasePath("outbox_missing").string();
	EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, missing, false, leftover, next), zx::DatabaseOutboxOpen::Missing);
	const std::string unmakeable = (std::filesystem::temp_directory_path() / "zx_db_no_such_folder" / "x.db").string();
	EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, unmakeable, true, leftover, next), zx::DatabaseOutboxOpen::Failed);
	EXPECT_EQ(zx::ComputeDatabaseOutboxClaim(outbox, unmakeable, 3, leftover, next), -1);
	EXPECT_FALSE(zx::ComputeDatabaseOutboxAppend(outbox, kBatches, 3, 0));
}

TEST(DatabaseOutbox, AFileThatIsNotADatabaseIsSkipped)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_garbage");
	std::ofstream(zx::ComputeDatabaseOutboxPath(base.string(), 0), std::ios::binary) << std::string(4096, 'x');
	zx::DatabaseOutbox outbox;
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, zx::ComputeDatabaseOutboxPath(base.string(), 0), true, leftover, next), zx::DatabaseOutboxOpen::Unreadable);
	std::vector<std::string> unreadable;
	EXPECT_EQ(zx::ComputeDatabaseOutboxClaim(outbox, base.string(), 2, leftover, next, &unreadable), 1);
	EXPECT_EQ(unreadable, (std::vector<std::string>{ zx::ComputeDatabaseOutboxPath(base.string(), 0) }));
	zx::ComputeDatabaseOutboxClose(outbox);
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, ADatabaseWithAForeignSchemaIsNotAnOutbox)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_foreign");
	const std::string path = zx::ComputeDatabaseOutboxPath(base.string(), 0);
	sqlite3 *foreign = nullptr;
	sqlite3_open(path.c_str(), &foreign);
	exec(foreign, "CREATE TABLE Meta(Something text)");
	sqlite3_close(foreign);
	zx::DatabaseOutbox outbox;
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	EXPECT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, path, true, leftover, next), zx::DatabaseOutboxOpen::Unreadable);
	EXPECT_EQ(outbox.db, nullptr);
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, ClaimsTheFirstSlotNoLiveProcessHolds)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_claim");
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	{
		zx::DatabaseOutbox a, b, c;
		EXPECT_EQ(zx::ComputeDatabaseOutboxClaim(a, base.string(), 2, leftover, next), 0);
		EXPECT_EQ(zx::ComputeDatabaseOutboxClaim(b, base.string(), 2, leftover, next), 1);
		EXPECT_EQ(zx::ComputeDatabaseOutboxClaim(c, base.string(), 2, leftover, next), -1);
		EXPECT_NE(a.server, b.server);
	}
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, BatchesOutliveAnUncleanCloseAndKeepTheirServer)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_crash");
	const std::string path = zx::ComputeDatabaseOutboxPath(base.string(), 0);
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	std::string server;
	{
		zx::DatabaseOutbox outbox;
		ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, path, true, leftover, next), zx::DatabaseOutboxOpen::Opened);
		server = outbox.server;
		EXPECT_TRUE(zx::ComputeDatabaseOutboxAppend(outbox, kBatches, 3, 0));
	}
	zx::DatabaseOutbox reopened;
	ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(reopened, path, false, leftover, next), zx::DatabaseOutboxOpen::Opened);
	EXPECT_EQ(reopened.server, server);
	EXPECT_TRUE(sameBatches(leftover, kBatches));
	EXPECT_EQ(next, 3u);
	// [Doruk-Aksoy] Pruning everything still keeps the next number, so a server never reuses one.
	EXPECT_TRUE(zx::ComputeDatabaseOutboxAppend(reopened, {}, 3, 2));
	zx::ComputeDatabaseOutboxClose(reopened);
	ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(reopened, path, false, leftover, next), zx::DatabaseOutboxOpen::Opened);
	EXPECT_TRUE(leftover.empty());
	EXPECT_EQ(next, 3u);
	zx::ComputeDatabaseOutboxClose(reopened);
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, NextNumberFollowsTheHighestKeptBatch)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_next");
	const std::string path = zx::ComputeDatabaseOutboxPath(base.string(), 0);
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	{
		zx::DatabaseOutbox outbox;
		zx::ComputeDatabaseOutboxOpen(outbox, path, true, leftover, next);
		zx::ComputeDatabaseOutboxAppend(outbox, kBatches, 1, 0);
	}
	zx::DatabaseOutbox reopened;
	zx::ComputeDatabaseOutboxOpen(reopened, path, false, leftover, next);
	EXPECT_EQ(next, 3u);
	zx::ComputeDatabaseOutboxClose(reopened);
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, AnAppendThatCannotBeWrittenFails)
{
	ZX_REQUIRE_FILE_DATABASES();
	const std::filesystem::path base = tempDatabasePath("outbox_failed");
	const std::string path = zx::ComputeDatabaseOutboxPath(base.string(), 0);
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	{
		zx::DatabaseOutbox outbox;
		zx::ComputeDatabaseOutboxOpen(outbox, path, true, leftover, next);
		EXPECT_TRUE(zx::ComputeDatabaseOutboxAppend(outbox, { kBatches[0] }, 2, 0));
		exec(outbox.db, "DROP TABLE Meta");
		EXPECT_FALSE(zx::ComputeDatabaseOutboxAppend(outbox, { kBatches[1] }, 3, 0));
		exec(outbox.db, "CREATE TABLE Meta(Key text PRIMARY KEY, Value text)");
		exec(outbox.db, "DROP TABLE Ops");
		EXPECT_FALSE(zx::ComputeDatabaseOutboxAppend(outbox, { kBatches[1] }, 3, 0));
	}
	removeDatabaseFiles(base);
}

TEST(DatabaseOutbox, AnotherServersOrphanedBatchesAreAppliedOnce)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb shared("outbox_orphans");
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	{
		zx::DatabaseOutbox orphan;
		ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(orphan, zx::ComputeDatabaseOutboxPath(shared.path.string(), 2), true, leftover, next), zx::DatabaseOutboxOpen::Opened);
		zx::ComputeDatabaseOutboxAppend(orphan, kBatches, 3, 0);
	}
	zx::DatabaseOutbox own;
	ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(own, zx::ComputeDatabaseOutboxPath(shared.path.string(), 0), true, leftover, next), zx::DatabaseOutboxOpen::Opened);
	zx::ComputeDatabaseOutboxAppend(own, { batch(1, 50, { zx::ComputeDatabaseSetOp("ns", "own", "mine") }) }, 2, 0);
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, shared.db);
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseOutboxReplayOrphans(statements, shared.path.string(), 4, 0, error), 2);
	EXPECT_EQ(value(shared.db, "a"), "<missing>");
	EXPECT_EQ(value(shared.db, "n"), "-3");
	EXPECT_EQ(value(shared.db, "own"), "<missing>");
	EXPECT_EQ(zx::ComputeDatabaseOutboxReplayOrphans(statements, shared.path.string(), 4, 0, error), 0);
	EXPECT_EQ(value(shared.db, "n"), "-3");
}

TEST(DatabaseOutbox, AnOrphanThatCannotBeAppliedIsKept)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb shared("outbox_keep");
	exec(shared.db, "DROP TABLE fua_Applied");
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	const std::string path = zx::ComputeDatabaseOutboxPath(shared.path.string(), 1);
	{
		zx::DatabaseOutbox orphan;
		zx::ComputeDatabaseOutboxOpen(orphan, path, true, leftover, next);
		zx::ComputeDatabaseOutboxAppend(orphan, kBatches, 3, 0);
	}
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, shared.db);
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseOutboxReplayOrphans(statements, shared.path.string(), 2, 0, error), 0);
	EXPECT_FALSE(error.empty());
	zx::DatabaseOutbox reopened;
	zx::ComputeDatabaseOutboxOpen(reopened, path, false, leftover, next);
	EXPECT_TRUE(sameBatches(leftover, kBatches));
}

TEST(DatabaseOutbox, ALiveServersOutboxIsLeftAlone)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb shared("outbox_live");
	std::vector<zx::DatabaseBatch> leftover;
	uint64_t next = 0;
	zx::DatabaseOutbox live;
	zx::ComputeDatabaseOutboxOpen(live, zx::ComputeDatabaseOutboxPath(shared.path.string(), 1), true, leftover, next);
	zx::ComputeDatabaseOutboxAppend(live, kBatches, 3, 0);
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, shared.db);
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseOutboxReplayOrphans(statements, shared.path.string(), 2, 0, error), 0);
	EXPECT_EQ(value(shared.db, "n"), "<missing>");
}

TEST(DatabaseOutbox, TheOrphanScanListsOutboxesItCannotOpen)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb shared("outbox_scanunreadable");
	std::ofstream(zx::ComputeDatabaseOutboxPath(shared.path.string(), 2), std::ios::binary) << std::string(4096, 'x');
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, shared.db);
	std::string error;
	std::vector<std::string> unreadable;
	EXPECT_EQ(zx::ComputeDatabaseOutboxReplayOrphans(statements, shared.path.string(), 4, 0, error, &unreadable), 0);
	EXPECT_EQ(unreadable, (std::vector<std::string>{ zx::ComputeDatabaseOutboxPath(shared.path.string(), 2) }));
}

