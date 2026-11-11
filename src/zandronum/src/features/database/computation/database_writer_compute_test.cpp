#include "features/database/computation/database_writer_compute.h"
#include "features/database/computation/database_test_support.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <sqlite3.h>
#include <string>
#include <thread>
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

zx::DatabaseOp set(const char *key, const char *value)
{
	return zx::ComputeDatabaseSetOp("ns", key, value);
}

zx::DatabaseOp inc(const char *key, int64_t delta)
{
	return zx::ComputeDatabaseIncrementOp("ns", key, delta);
}

zx::DatabaseApplyResult apply(zx::DatabaseStatements &statements, const char *server, const zx::DatabaseBatch &made)
{
	std::string error;
	return zx::ComputeDatabaseApplyBatch(statements, server, made, error);
}

std::string value(zx::DatabaseStatements &statements, const char *key)
{
	std::string text;
	bool found = false;
	zx::ComputeDatabaseGet(statements, "ns", key, text, found);
	return found ? text : "<missing>";
}

double markerOf(sqlite3 *db, const char *key)
{
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, "SELECT Timestamp FROM fua_Deleted WHERE Namespace='ns' AND KeyName=?1", -1, &statement, nullptr);
	sqlite3_bind_text(statement, 1, key, -1, SQLITE_STATIC);
	const double stamp = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_double(statement, 0) : -1;
	sqlite3_finalize(statement);
	return stamp;
}

int64_t countOf(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
	sqlite3_step(statement);
	const int64_t count = sqlite3_column_int64(statement, 0);
	sqlite3_finalize(statement);
	return count;
}

double nowSeconds()
{
	return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t appliedOf(sqlite3 *db, const char *server)
{
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, "SELECT Seq FROM fua_Applied WHERE Server=?1", -1, &statement, nullptr);
	sqlite3_bind_text(statement, 1, server, -1, SQLITE_STATIC);
	const int64_t seq = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int64(statement, 0) : -1;
	sqlite3_finalize(statement);
	return seq;
}

}

TEST(DatabaseApply, NewerSetsWinAndOlderOnesAreIgnored)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	EXPECT_EQ(apply(statements, "a", batch(1, 200, { set("k", "new") })), zx::DatabaseApplyResult::Applied);
	EXPECT_EQ(apply(statements, "b", batch(1, 100, { set("k", "old") })), zx::DatabaseApplyResult::Applied);
	EXPECT_EQ(value(statements, "k"), "new");
	EXPECT_EQ(apply(statements, "b", batch(2, 300, { set("k", "newer") })), zx::DatabaseApplyResult::Applied);
	EXPECT_EQ(value(statements, "k"), "newer");
}

TEST(DatabaseApply, RowsWithoutATimestampTakeAnyWrite)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','k','stock',NULL)");
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','gone','stock',NULL)");
	apply(statements, "a", batch(1, 1, { set("k", "ours"), set("gone", "") }));
	EXPECT_EQ(value(statements, "k"), "ours");
	EXPECT_EQ(value(statements, "gone"), "<missing>");
}

TEST(DatabaseApply, ADeleteKeepsOlderWritesFromBringingTheKeyBack)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	apply(statements, "a", batch(1, 100, { set("k", "v") }));
	apply(statements, "a", batch(2, 300, { set("k", "") }));
	apply(statements, "b", batch(1, 250, { set("k", "late"), inc("k", 5) }));
	EXPECT_EQ(value(statements, "k"), "<missing>");
	apply(statements, "b", batch(2, 350, { set("k", "after") }));
	EXPECT_EQ(value(statements, "k"), "after");
	// [Doruk-Aksoy] The newer set took the marker away, so a later delete starts a marker of its own.
	double marker = -1;
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(memory.db, "SELECT COUNT(*) FROM fua_Deleted", -1, &statement, nullptr);
	sqlite3_step(statement);
	marker = sqlite3_column_double(statement, 0);
	sqlite3_finalize(statement);
	EXPECT_EQ(marker, 0);
	apply(statements, "b", batch(3, 360, { inc("fresh", 2) }));
	EXPECT_EQ(value(statements, "fresh"), "2");
}

TEST(DatabaseApply, ADeleteOlderThanTheRowLeavesTheRow)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	apply(statements, "a", batch(1, 500, { set("k", "v") }));
	apply(statements, "b", batch(1, 400, { set("k", "") }));
	EXPECT_EQ(value(statements, "k"), "v");
}

TEST(DatabaseApply, IncrementsFromEveryServerAllCount)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	apply(statements, "a", batch(1, 300, { inc("kills", 3) }));
	apply(statements, "b", batch(1, 100, { inc("kills", 4) }));
	apply(statements, "c", batch(1, 200, { inc("kills", -2) }));
	EXPECT_EQ(value(statements, "kills"), "5");
}

TEST(DatabaseApply, ABatchIsAppliedOnceHoweverOftenItIsSent)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	const zx::DatabaseBatch made = batch(4, 10, { inc("k", 1) });
	EXPECT_EQ(apply(statements, "a", made), zx::DatabaseApplyResult::Applied);
	EXPECT_EQ(apply(statements, "a", made), zx::DatabaseApplyResult::AlreadyApplied);
	EXPECT_EQ(apply(statements, "a", batch(3, 11, { inc("k", 1) })), zx::DatabaseApplyResult::AlreadyApplied);
	EXPECT_EQ(value(statements, "k"), "1");
	EXPECT_EQ(appliedOf(memory.db, "a"), 4);
}

// [Doruk-Aksoy] A set then a delete of one key in one batch share a stamp, which must still compare equal once the stamp is stored as text.
TEST(DatabaseApply, OneBatchCanSetThenDeleteAKeyAtAnyClockStamp)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::DatabaseClock clock;
	std::mt19937_64 random(5);
	for (uint64_t seq = 1; seq <= 500; ++seq)
	{
		const double wall = 1.7e9 + static_cast<double>(random() % 1000000000) / 1000.0;
		const double stamp = zx::ComputeDatabaseClockStamp(clock, wall);
		ASSERT_EQ(apply(statements, "a", batch(seq, stamp, { set("k", "v"), set("k", "") })), zx::DatabaseApplyResult::Applied);
		ASSERT_EQ(value(statements, "k"), "<missing>") << std::to_string(stamp);
	}
}

// [Doruk-Aksoy] A stock server deletes with plain SQL and no marker of its own; the trigger must make one, or an older queued set brings the key back.
TEST(DatabaseApply, AStockServersDeleteKeepsAnOlderQueuedSetFromBringingTheKeyBack)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	const double now = nowSeconds();
	apply(statements, "a", batch(1, now - 100, { set("k", "v") }));
	exec(memory.db, "DELETE FROM Zandronum WHERE Namespace='ns' AND KeyName='k'");
	EXPECT_GE(markerOf(memory.db, "k"), now - 1);
	apply(statements, "a", batch(2, now - 10, { set("k", "queued earlier"), inc("k", 1) }));
	EXPECT_EQ(value(statements, "k"), "<missing>");
	apply(statements, "a", batch(3, now + 10, { set("k", "after") }));
	EXPECT_EQ(value(statements, "k"), "after");
}

TEST(DatabaseApply, TheQueuesOwnDeletesKeepTheirOwnStamps)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	apply(statements, "a", batch(1, 1000, { set("k", "v") }));
	apply(statements, "a", batch(2, 2000, { set("k", "") }));
	EXPECT_EQ(markerOf(memory.db, "k"), 2000);
	// [Doruk-Aksoy] Had the trigger stamped the delete with the time it was applied, this newer set would be refused.
	apply(statements, "b", batch(1, 2500, { set("k", "newer") }));
	EXPECT_EQ(value(statements, "k"), "newer");
	EXPECT_EQ(countOf(memory.db, "SELECT COUNT(*) FROM fua_Writing"), 0);
}

TEST(DatabaseApply, AStockDeleteKeepsANewerMarkerAndClearingMarksEveryRow)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	const double later = nowSeconds() + 1000;
	exec(memory.db, ("INSERT INTO fua_Deleted VALUES('ns','k'," + std::to_string(later) + ")").c_str());
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','k','v','1')");
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','other','v','1')");
	exec(memory.db, "INSERT INTO Zandronum VALUES('elsewhere','x','v','1')");
	exec(memory.db, "DELETE FROM Zandronum");
	EXPECT_DOUBLE_EQ(markerOf(memory.db, "k"), later);
	EXPECT_GT(markerOf(memory.db, "other"), 0);
	EXPECT_EQ(countOf(memory.db, "SELECT COUNT(*) FROM fua_Deleted"), 3);
}

TEST(DatabaseApply, FailsWithoutTheQueueTables)
{
	sqlite3 *db = nullptr;
	sqlite3_open(":memory:", &db);
	exec(db, kCreateTable);
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, db);
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, { set("k", "v") }), error), zx::DatabaseApplyResult::Failed);
	EXPECT_FALSE(error.empty());
	exec(db, "CREATE TABLE fua_Applied(Server text PRIMARY KEY, Seq integer)");
	EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, { set("k", "v") }), error), zx::DatabaseApplyResult::Failed);
	EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, { set("k", "") }), error), zx::DatabaseApplyResult::Failed);
	EXPECT_EQ(appliedOf(db, "a"), -1);
	zx::ComputeDatabaseBind(statements, nullptr);
	sqlite3_close(db);
}

TEST(DatabaseApply, FailsWithoutAConnection)
{
	zx::DatabaseStatements statements;
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, {}), error), zx::DatabaseApplyResult::Failed);
	EXPECT_FALSE(zx::ComputeDatabasePruneMarkers(statements, 0));
}

TEST(DatabaseApply, AFailingStepRollsTheWholeBatchBack)
{
	for (zx::DatabaseQuery query : { zx::DatabaseQuery::WritingBegin, zx::DatabaseQuery::AppliedSeq, zx::DatabaseQuery::MarkerStamp, zx::DatabaseQuery::SetIfNewer, zx::DatabaseQuery::IncrementStamped,
			 zx::DatabaseQuery::ClearMarker, zx::DatabaseQuery::DeleteIfOlder, zx::DatabaseQuery::MarkDeleted, zx::DatabaseQuery::SetApplied, zx::DatabaseQuery::WritingEnd })
	{
		MemoryDb memory;
		zx::DatabaseStatements statements;
		zx::ComputeDatabaseBind(statements, memory.db);
		poison(statements, query);
		std::string error;
		EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, { set("first", "v"), inc("k", 1), set("k", "") }), error), zx::DatabaseApplyResult::Failed) << static_cast<int>(query);
		EXPECT_EQ(value(statements, "first"), "<missing>");
	}
}

TEST(DatabaseApply, AFullDatabaseFailsTheBatch)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	exec(memory.db, "PRAGMA max_page_count=8");
	std::vector<zx::DatabaseOp> ops;
	for (int i = 0; i < 400; ++i)
		ops.push_back(zx::ComputeDatabaseSetOp("ns", ("key" + std::to_string(i)).c_str(), "a value long enough to fill a few pages"));
	std::string error;
	EXPECT_EQ(zx::ComputeDatabaseApplyBatch(statements, "a", batch(1, 1, ops), error), zx::DatabaseApplyResult::Failed);
	EXPECT_NE(error.find("full"), std::string::npos) << error;
}

TEST(DatabaseApply, PruningForgetsOldDeleteMarkers)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	apply(statements, "a", batch(1, 100, { set("old", "") }));
	apply(statements, "a", batch(2, 900, { set("new", "") }));
	EXPECT_TRUE(zx::ComputeDatabasePruneMarkers(statements, 500));
	apply(statements, "b", batch(1, 50, { set("old", "back"), set("new", "blocked") }));
	EXPECT_EQ(value(statements, "old"), "back");
	EXPECT_EQ(value(statements, "new"), "<missing>");
}


TEST(DatabaseApply, AnotherConnectionsWriteLockMakesItBusy)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("busy_begin");
	sqlite3 *other = nullptr;
	sqlite3_open(file.path.string().c_str(), &other);
	exec(other, "BEGIN IMMEDIATE");
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, file.db);
	EXPECT_EQ(apply(statements, "a", batch(1, 1, { set("k", "v") })), zx::DatabaseApplyResult::Busy);
	exec(other, "ROLLBACK");
	EXPECT_EQ(apply(statements, "a", batch(1, 1, { set("k", "v") })), zx::DatabaseApplyResult::Applied);
	sqlite3_close(other);
}

TEST(DatabaseApply, AReaderBlockingTheRollbackJournalsCommitMakesItBusy)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("busy_commit");
	exec(file.db, "PRAGMA journal_mode=DELETE");
	sqlite3 *reader = nullptr;
	sqlite3_open(file.path.string().c_str(), &reader);
	exec(reader, "BEGIN");
	exec(reader, "SELECT COUNT(*) FROM Zandronum");
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, file.db);
	EXPECT_EQ(apply(statements, "a", batch(1, 1, { set("k", "v") })), zx::DatabaseApplyResult::Busy);
	exec(reader, "COMMIT");
	sqlite3_close(reader);
	EXPECT_EQ(value(statements, "k"), "<missing>");
}

TEST(DatabaseWriter, DoesNotStartOnAMissingFile)
{
	ZX_REQUIRE_FILE_DATABASES();
	zx::DatabaseWriter writer;
	const std::string path = tempDatabasePath("writer_missing").string();
	EXPECT_FALSE(zx::ComputeDatabaseWriterStart(writer, path.c_str(), "a", {}, 10));
	EXPECT_EQ(writer.db, nullptr);
}

TEST(DatabaseWriter, AppliesWhatIsPushedInOrderAndRunsItsPragmas)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_order");
	zx::DatabaseWriter writer;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", { "PRAGMA max_page_count=4096" }, 10));
	zx::ComputeDatabaseWriterPush(writer, {});
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "one") }), batch(2, 11, { set("k", "two") }) });
	EXPECT_TRUE(waitFor([&]() { return writer.applied.load() == 2; }));
	EXPECT_EQ(zx::ComputeDatabaseWriterQueued(writer), 0u);
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, file.db);
	EXPECT_EQ(value(statements, "k"), "two");
	EXPECT_TRUE(zx::ComputeDatabaseWriterStop(writer, 1000));
	EXPECT_EQ(writer.db, nullptr);
}

TEST(DatabaseWriter, WaitsOutAnotherServersLockInsteadOfDroppingTheBatch)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_wait");
	sqlite3 *other = nullptr;
	sqlite3_open(file.path.string().c_str(), &other);
	exec(other, "BEGIN IMMEDIATE");
	zx::DatabaseWriter writer;
	writer.retryMs = 1;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	EXPECT_TRUE(waitFor([&]() { return writer.busyRetries.load() > 2; }));
	EXPECT_EQ(writer.applied.load(), 0u);
	exec(other, "ROLLBACK");
	EXPECT_TRUE(waitFor([&]() { return writer.applied.load() == 1; }));
	EXPECT_EQ(writer.failures.load(), 0);
	sqlite3_close(other);
}

TEST(DatabaseWriter, KeepsAFailingBatchAndRetriesItUntilItLands)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_retry");
	exec(file.db, "DROP TABLE fua_Applied");
	zx::DatabaseWriter writer;
	writer.failedRetryMs = 5;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 10));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	EXPECT_TRUE(waitFor([&]() { return writer.failures.load() > 1; }));
	EXPECT_FALSE(zx::ComputeDatabaseWriterLastError(writer).empty());
	EXPECT_EQ(zx::ComputeDatabaseWriterQueued(writer), 1u);
	// [Doruk-Aksoy] The writer takes the write lock on every retry, so this change waits its turn.
	sqlite3_busy_timeout(file.db, 5000);
	exec(file.db, "CREATE TABLE fua_Applied(Server text PRIMARY KEY, Seq integer)");
	EXPECT_TRUE(waitFor([&]() { return writer.applied.load() == 1; }));
}

TEST(DatabaseWriter, StopsWithinItsDrainTimeAndReportsWhatWasLeft)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_drain");
	sqlite3 *other = nullptr;
	sqlite3_open(file.path.string().c_str(), &other);
	exec(other, "BEGIN IMMEDIATE");
	zx::DatabaseWriter writer;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	const auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(zx::ComputeDatabaseWriterStop(writer, 50));
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
	EXPECT_EQ(zx::ComputeDatabaseWriterQueued(writer), 0u);
	exec(other, "ROLLBACK");
	sqlite3_close(other);
}

TEST(DatabaseWriter, LeavesAQueuedBatchWhenTheDrainTimeIsGoneBeforeItsTurn)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_drainover");
	std::atomic<bool> release{false};
	zx::DatabaseWriter writer;
	// [Doruk-Aksoy] Maintenance holds the writer until the stop has set its deadline, so the queued batch meets an expired one.
	writer.maintenance = [&release](zx::DatabaseStatements &) {
		while (!release.load())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	};
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	bool drained = true;
	std::thread stopper([&]() { drained = zx::ComputeDatabaseWriterStop(writer, 0); });
	EXPECT_TRUE(waitFor([&]() { std::lock_guard<std::mutex> lock(writer.mutex); return writer.stopping; }));
	release = true;
	stopper.join();
	EXPECT_FALSE(drained);
	EXPECT_EQ(writer.applied.load(), 0u);
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, file.db);
	EXPECT_EQ(value(statements, "k"), "<missing>");
}

TEST(DatabaseWriter, StopsAtOnceWhileWaitingToRetryAFailure)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_failstop");
	exec(file.db, "DROP TABLE fua_Applied");
	zx::DatabaseWriter writer;
	writer.failedRetryMs = 60000;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	EXPECT_TRUE(waitFor([&]() { return writer.failures.load() > 0; }));
	const auto started = std::chrono::steady_clock::now();
	EXPECT_FALSE(zx::ComputeDatabaseWriterStop(writer, 0));
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
}

TEST(DatabaseWriter, RunsItsMaintenanceAtStartAndThenOnSchedule)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_maintenance");
	std::atomic<int> runs{0};
	{
		zx::DatabaseWriter writer;
		writer.maintenanceMs = 5;
		writer.maintenance = [&runs](zx::DatabaseStatements &statements) {
			EXPECT_NE(statements.db, nullptr);
			++runs;
		};
		ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
		EXPECT_TRUE(waitFor([&]() { return runs.load() > 2; }));
		EXPECT_GE(writer.maintenanceRuns.load(), 2);
	}
	const int afterDestruction = runs.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(30));
	EXPECT_EQ(runs.load(), afterDestruction);
}

TEST(DatabaseWriter, RestartingResetsItsCounters)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("writer_restart");
	std::atomic<bool> release{true};
	zx::DatabaseWriter writer;
	// [Doruk-Aksoy] The restarted writer's first maintenance waits for release, so its count still reads the reset.
	writer.maintenance = [&release](zx::DatabaseStatements &) {
		while (!release.load())
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	};
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "a", {}, 1));
	zx::ComputeDatabaseWriterPush(writer, { batch(1, 10, { set("k", "v") }) });
	EXPECT_TRUE(waitFor([&]() { return writer.applied.load() == 1 && writer.maintenanceRuns.load() == 1; }));
	release = false;
	ASSERT_TRUE(zx::ComputeDatabaseWriterStart(writer, file.path.string().c_str(), "b", {}, 1));
	EXPECT_EQ(writer.applied.load(), 0u);
	EXPECT_EQ(writer.maintenanceRuns.load(), 0);
	EXPECT_EQ(writer.server, "b");
	release = true;
}

