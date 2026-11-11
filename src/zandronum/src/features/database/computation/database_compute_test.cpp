#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_test_support.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <vector>

using namespace zxdbtest;

namespace
{

std::string get(zx::DatabaseStatements &statements, const char *ns, const char *key)
{
	std::string value;
	bool found = false;
	EXPECT_TRUE(zx::ComputeDatabaseGet(statements, ns, key, value, found));
	EXPECT_EQ(found, !value.empty());
	return value;
}

int rank(zx::DatabaseStatements &statements, const char *ns, const char *key, bool descending)
{
	int result = 0;
	EXPECT_TRUE(zx::ComputeDatabaseRank(statements, ns, key, descending, result));
	return result;
}

zx::DatabaseEntries sorted(zx::DatabaseStatements &statements, const char *ns, int count, int offset, bool descending)
{
	zx::DatabaseEntries entries;
	EXPECT_TRUE(zx::ComputeDatabaseSorted(statements, ns, count, offset, descending, entries));
	return entries;
}

zx::DatabaseEntries entries(zx::DatabaseStatements &statements, const char *ns)
{
	zx::DatabaseEntries result;
	EXPECT_TRUE(zx::ComputeDatabaseEntries(statements, ns, result));
	return result;
}

std::string queryPlan(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, (std::string("EXPLAIN QUERY PLAN ") + sql).c_str(), -1, &statement, nullptr);
	std::string plan;
	while (sqlite3_step(statement) == SQLITE_ROW)
		plan += reinterpret_cast<const char *>(sqlite3_column_text(statement, 3)) + std::string("\n");
	sqlite3_finalize(statement);
	return plan;
}

}

TEST(DatabaseCompute, PragmasLeaveMemoryDatabasesAlone)
{
	EXPECT_TRUE(zx::ComputeDatabasePragmas(true, true, 2).empty());
}

TEST(DatabaseCompute, PragmasSetTheJournalBothWaysAndTheSyncLevel)
{
	EXPECT_EQ(zx::ComputeDatabasePragmas(false, true, 2), (std::vector<std::string>{ "PRAGMA journal_mode=WAL", "PRAGMA synchronous=2" }));
	EXPECT_EQ(zx::ComputeDatabasePragmas(false, false, 1), (std::vector<std::string>{ "PRAGMA journal_mode=DELETE", "PRAGMA synchronous=1" }));
}

TEST(DatabaseCompute, PragmasClampTheSyncLevel)
{
	EXPECT_EQ(zx::ComputeDatabasePragmas(false, false, -4).back(), "PRAGMA synchronous=0");
	EXPECT_EQ(zx::ComputeDatabasePragmas(false, false, 9).back(), "PRAGMA synchronous=3");
}

TEST(DatabaseCompute, EveryQueryPrepares)
{
	MemoryDb memory;
	for (int i = 0; i < static_cast<int>(zx::DatabaseQuery::Count); ++i)
	{
		sqlite3_stmt *statement = nullptr;
		EXPECT_EQ(sqlite3_prepare_v2(memory.db, zx::ComputeDatabaseQuerySql(static_cast<zx::DatabaseQuery>(i)), -1, &statement, nullptr), SQLITE_OK) << sqlite3_errmsg(memory.db);
		sqlite3_finalize(statement);
	}
}

TEST(DatabaseCompute, SortAndRankReadTheValueIndex)
{
	MemoryDb memory;
	for (zx::DatabaseQuery query : { zx::DatabaseQuery::SortAscending, zx::DatabaseQuery::SortDescending, zx::DatabaseQuery::RankAscending, zx::DatabaseQuery::RankDescending })
		EXPECT_NE(queryPlan(memory.db, zx::ComputeDatabaseQuerySql(query)).find("fua_ValueOrder"), std::string::npos) << zx::ComputeDatabaseQuerySql(query);
}

TEST(DatabaseCompute, GetOfAMissingKeyIsNotFound)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	std::string value = "stale";
	bool found = true;
	EXPECT_TRUE(zx::ComputeDatabaseGet(statements, "ns", "missing", value, found));
	EXPECT_FALSE(found);
	EXPECT_EQ(value, "");
}

TEST(DatabaseCompute, SaveSetStoresReplacesAndDeletes)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	EXPECT_TRUE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", "one"));
	EXPECT_EQ(get(statements, "ns", "k"), "one");
	EXPECT_TRUE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", "two"));
	EXPECT_EQ(get(statements, "ns", "k"), "two");
	EXPECT_TRUE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", ""));
	EXPECT_EQ(get(statements, "ns", "k"), "");
	EXPECT_TRUE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", "three"));
	EXPECT_TRUE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", nullptr));
	EXPECT_TRUE(entries(statements, "ns").empty());
}

TEST(DatabaseCompute, IncrementCreatesThenAdds)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	EXPECT_TRUE(zx::ComputeDatabaseIncrement(statements, "ns", "k", 5));
	EXPECT_TRUE(zx::ComputeDatabaseIncrement(statements, "ns", "k", -7));
	EXPECT_EQ(get(statements, "ns", "k"), "-2");
	zx::ComputeDatabaseSaveSet(statements, "ns", "word", "abc");
	EXPECT_TRUE(zx::ComputeDatabaseIncrement(statements, "ns", "word", 3));
	EXPECT_EQ(get(statements, "ns", "word"), "3");
}

TEST(DatabaseCompute, RankCountsStrictlyBetterEntries)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "lb", "a", "10");
	zx::ComputeDatabaseSaveSet(statements, "lb", "b", "30");
	zx::ComputeDatabaseSaveSet(statements, "lb", "c", "30");
	zx::ComputeDatabaseSaveSet(statements, "other", "z", "99");
	EXPECT_EQ(rank(statements, "lb", "b", true), 1);
	EXPECT_EQ(rank(statements, "lb", "c", true), 1);
	EXPECT_EQ(rank(statements, "lb", "a", true), 3);
	EXPECT_EQ(rank(statements, "lb", "a", false), 1);
	EXPECT_EQ(rank(statements, "lb", "b", false), 2);
	EXPECT_EQ(rank(statements, "lb", "missing", true), -1);
}

TEST(DatabaseCompute, SortedBreaksTiesByKeyInBothDirections)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	const char *const keys[] = { "zed", "amy", "mo", "bob", "kai", "eve" };
	const char *const values[] = { "5", "5", "3", "5", "3", "7" };
	for (int i = 0; i < 6; ++i)
		zx::ComputeDatabaseSaveSet(statements, "lb", keys[i], values[i]);
	EXPECT_EQ(sorted(statements, "lb", 10, 0, true), (zx::DatabaseEntries{ { "eve", "7" }, { "amy", "5" }, { "bob", "5" }, { "zed", "5" }, { "kai", "3" }, { "mo", "3" } }));
	EXPECT_EQ(sorted(statements, "lb", 2, 1, false), (zx::DatabaseEntries{ { "mo", "3" }, { "amy", "5" } }));
}

TEST(DatabaseCompute, EntriesComeInKeyOrder)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "b", "2");
	zx::ComputeDatabaseSaveSet(statements, "ns", "a", "1");
	zx::ComputeDatabaseSaveSet(statements, "elsewhere", "c", "3");
	EXPECT_EQ(entries(statements, "ns"), (zx::DatabaseEntries{ { "a", "1" }, { "b", "2" } }));
}

TEST(DatabaseCompute, MatchesTheLegacyCallsOverRandomOperations)
{
	MemoryDb memory;
	LegacyDb legacy;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	std::mt19937 random(1234);
	const char *const spaces[] = { "a", "b" };
	const char *const keys[] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
	const char *const values[] = { "", "0", "5", "-3", "12", "abc", "7", "5" };
	for (int step = 0; step < 3000; ++step)
	{
		const char *ns = spaces[random() % 2];
		const char *key = keys[random() % 8];
		const bool descending = random() % 2 == 0;
		switch (random() % 6)
		{
		case 0:
		{
			const char *value = values[random() % 8];
			legacy.saveSet(ns, key, value);
			zx::ComputeDatabaseSaveSet(statements, ns, key, value);
			break;
		}
		case 1:
		{
			const int amount = static_cast<int>(random() % 21) - 10;
			legacy.increment(ns, key, amount);
			zx::ComputeDatabaseIncrement(statements, ns, key, amount);
			break;
		}
		case 2:
			ASSERT_EQ(get(statements, ns, key), legacy.get(ns, key)) << "step " << step;
			break;
		case 3:
			ASSERT_EQ(rank(statements, ns, key, descending), legacy.rank(ns, key, descending)) << "step " << step;
			break;
		case 4:
		{
			const int count = static_cast<int>(random() % 6), offset = static_cast<int>(random() % 4);
			ASSERT_EQ(sorted(statements, ns, count, offset, descending), legacy.sorted(ns, count, offset, descending)) << "step " << step;
			break;
		}
		default:
			ASSERT_EQ(entries(statements, ns), legacy.entries(ns)) << "step " << step;
			break;
		}
	}
}

TEST(DatabaseCompute, EveryCallFailsWithoutAConnection)
{
	zx::DatabaseStatements statements;
	std::string value;
	bool found = true;
	int result = 0;
	zx::DatabaseEntries list;
	EXPECT_FALSE(zx::ComputeDatabaseGet(statements, "ns", "k", value, found));
	EXPECT_FALSE(found);
	EXPECT_FALSE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", "v"));
	EXPECT_FALSE(zx::ComputeDatabaseIncrement(statements, "ns", "k", 1));
	EXPECT_FALSE(zx::ComputeDatabaseRank(statements, "ns", "k", true, result));
	EXPECT_EQ(result, -1);
	EXPECT_FALSE(zx::ComputeDatabaseSorted(statements, "ns", 1, 0, true, list));
	EXPECT_FALSE(zx::ComputeDatabaseEntries(statements, "ns", list));
}

TEST(DatabaseCompute, PreparedCallsFailOnceTheTableIsGone)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	std::string value;
	bool found = false;
	int result = 0;
	zx::DatabaseEntries list;
	// [Doruk-Aksoy] Prepares every statement, then drops the table so each step fails rather than each prepare.
	zx::ComputeDatabaseGet(statements, "ns", "k", value, found);
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "v");
	zx::ComputeDatabaseIncrement(statements, "ns", "k", 1);
	zx::ComputeDatabaseRank(statements, "ns", "k", true, result);
	zx::ComputeDatabaseSorted(statements, "ns", 1, 0, true, list);
	zx::ComputeDatabaseEntries(statements, "ns", list);
	exec(memory.db, "DROP TABLE Zandronum");
	EXPECT_FALSE(zx::ComputeDatabaseGet(statements, "ns", "k", value, found));
	EXPECT_FALSE(zx::ComputeDatabaseSaveSet(statements, "ns", "k", "v"));
	EXPECT_FALSE(zx::ComputeDatabaseIncrement(statements, "ns", "k", 1));
	EXPECT_FALSE(zx::ComputeDatabaseRank(statements, "ns", "k", true, result));
	EXPECT_FALSE(zx::ComputeDatabaseSorted(statements, "ns", 1, 0, true, list));
	EXPECT_FALSE(zx::ComputeDatabaseEntries(statements, "ns", list));
}

TEST(DatabaseCompute, RebindingMovesToTheNewConnection)
{
	MemoryDb first, second;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, first.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "first");
	zx::ComputeDatabaseBind(statements, second.db);
	EXPECT_EQ(get(statements, "ns", "k"), "");
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "second");
	zx::ComputeDatabaseBind(statements, first.db);
	EXPECT_EQ(get(statements, "ns", "k"), "first");
}

TEST(DatabaseCompute, CachedReadsDoNotHoldTheDatabaseOpen)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "v");
	get(statements, "ns", "k");
	sorted(statements, "ns", 1, 0, true);
	// [Doruk-Aksoy] A statement left mid-row would keep this connection's read open.
	EXPECT_NE(sqlite3_next_stmt(memory.db, nullptr), nullptr);
	for (sqlite3_stmt *statement = sqlite3_next_stmt(memory.db, nullptr); statement != nullptr; statement = sqlite3_next_stmt(memory.db, statement))
		EXPECT_FALSE(sqlite3_stmt_busy(statement));
}

TEST(DatabaseCheckpointer, MissingFileDoesNotStart)
{
	ZX_REQUIRE_FILE_DATABASES();
	zx::DatabaseCheckpointer checkpointer;
	const std::string path = (std::filesystem::temp_directory_path() / "zx_db_does_not_exist.db").string();
	EXPECT_FALSE(zx::ComputeDatabaseCheckpointerStart(checkpointer, path.c_str(), 10));
	EXPECT_EQ(checkpointer.db, nullptr);
	EXPECT_FALSE(checkpointer.thread.joinable());
}

TEST(DatabaseCheckpointer, MovesTheLogIntoTheMainFileOffTheCallingThread)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("checkpoint");
	const std::uintmax_t before = file.mainFileSize();
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, file.db);
	exec(file.db, "BEGIN");
	for (int i = 0; i < 2000; ++i)
		zx::ComputeDatabaseSaveSet(statements, "ns", ("key" + std::to_string(i)).c_str(), "a value long enough to fill pages");
	exec(file.db, "END");
	// [Doruk-Aksoy] With automatic checkpoints off, the rows sit in the log and the main file keeps its size.
	EXPECT_EQ(file.mainFileSize(), before);
	zx::DatabaseCheckpointer checkpointer;
	ASSERT_TRUE(zx::ComputeDatabaseCheckpointerStart(checkpointer, file.path.string().c_str(), 10));
	EXPECT_TRUE(waitFor([&]() { return file.mainFileSize() > before; }));
	// [Doruk-Aksoy] The file grows while the checkpoint runs, and the count rises only once it returns.
	EXPECT_TRUE(waitFor([&]() { return checkpointer.checkpoints.load() > 0; }));
	EXPECT_TRUE(waitFor([&]() { return checkpointer.lastCopiedFrames.load() > 0; }));
	zx::ComputeDatabaseCheckpointerStop(checkpointer);
	EXPECT_EQ(checkpointer.db, nullptr);
	EXPECT_FALSE(checkpointer.thread.joinable());
}

TEST(DatabaseCheckpointer, RestartsAndStopsSafely)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("restart");
	zx::DatabaseCheckpointer checkpointer;
	zx::ComputeDatabaseCheckpointerStop(checkpointer);
	ASSERT_TRUE(zx::ComputeDatabaseCheckpointerStart(checkpointer, file.path.string().c_str(), 0));
	EXPECT_TRUE(waitFor([&]() { return checkpointer.checkpoints.load() > 0; }));
	ASSERT_TRUE(zx::ComputeDatabaseCheckpointerStart(checkpointer, file.path.string().c_str(), 60000));
	// [Doruk-Aksoy] A long interval still stops at once, running its last checkpoint on the way out.
	const int before = checkpointer.checkpoints.load();
	const auto started = std::chrono::steady_clock::now();
	zx::ComputeDatabaseCheckpointerStop(checkpointer);
	EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));
	EXPECT_EQ(checkpointer.checkpoints.load(), before + 1);
}

TEST(DatabaseCheckpointer, DestructionStopsTheThread)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("destroy");
	auto checkpointer = std::make_unique<zx::DatabaseCheckpointer>();
	ASSERT_TRUE(zx::ComputeDatabaseCheckpointerStart(*checkpointer, file.path.string().c_str(), 60000));
	checkpointer.reset();
	SUCCEED();
}


TEST(DatabaseCompute, CountBeyondCountsStrictlyAboveAndBelow)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "lb", "a", "1");
	zx::ComputeDatabaseSaveSet(statements, "lb", "b", "5");
	zx::ComputeDatabaseSaveSet(statements, "lb", "c", "5");
	zx::ComputeDatabaseSaveSet(statements, "lb", "d", "9");
	int64_t count = -1;
	EXPECT_TRUE(zx::ComputeDatabaseCountBeyond(statements, "lb", 5, true, count));
	EXPECT_EQ(count, 1);
	EXPECT_TRUE(zx::ComputeDatabaseCountBeyond(statements, "lb", 5, false, count));
	EXPECT_EQ(count, 1);
	EXPECT_TRUE(zx::ComputeDatabaseCountBeyond(statements, "other", 0, true, count));
	EXPECT_EQ(count, 0);
}

TEST(DatabaseCompute, MaxStampReadsRowsAndDeleteMarkers)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	double stamp = -1;
	EXPECT_TRUE(zx::ComputeDatabaseMaxStamp(statements, stamp));
	EXPECT_EQ(stamp, 0);
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','k','v','100.5')");
	EXPECT_TRUE(zx::ComputeDatabaseMaxStamp(statements, stamp));
	EXPECT_DOUBLE_EQ(stamp, 100.5);
	exec(memory.db, "INSERT INTO fua_Deleted VALUES('ns','gone',250.25)");
	EXPECT_TRUE(zx::ComputeDatabaseMaxStamp(statements, stamp));
	EXPECT_DOUBLE_EQ(stamp, 250.25);
}

TEST(DatabaseCompute, ReadsRaiseTheSeenStampToTheirRows)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','a','1','10')");
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','b','2','30')");
	exec(memory.db, "INSERT INTO Zandronum VALUES('ns','c','3',NULL)");
	std::string value;
	bool found = false;
	double seen = 20;
	EXPECT_TRUE(zx::ComputeDatabaseGet(statements, "ns", "a", value, found, &seen));
	EXPECT_EQ(seen, 20);
	EXPECT_TRUE(zx::ComputeDatabaseGet(statements, "ns", "c", value, found, &seen));
	EXPECT_EQ(seen, 20);
	zx::DatabaseEntries list;
	EXPECT_TRUE(zx::ComputeDatabaseEntries(statements, "ns", list, &seen));
	EXPECT_EQ(seen, 30);
	seen = 0;
	EXPECT_TRUE(zx::ComputeDatabaseSorted(statements, "ns", 1, 0, true, list, &seen));
	EXPECT_EQ(seen, 0);
	EXPECT_TRUE(zx::ComputeDatabaseSorted(statements, "ns", 3, 0, true, list, &seen));
	EXPECT_EQ(seen, 30);
}

TEST(DatabaseCompute, ColumnTextOfNullIsEmpty)
{
	MemoryDb memory;
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(memory.db, "SELECT NULL", -1, &statement, nullptr);
	sqlite3_step(statement);
	EXPECT_EQ(zx::ComputeDatabaseColumnText(statement, 0), "");
	sqlite3_finalize(statement);
}

TEST(DatabaseCompute, QueueQueriesFailWithoutAConnection)
{
	zx::DatabaseStatements statements;
	int64_t count = 7;
	double stamp = 7;
	EXPECT_EQ(zx::ComputeDatabaseStatement(statements, zx::DatabaseQuery::MaxStamp), nullptr);
	EXPECT_FALSE(zx::ComputeDatabaseCountBeyond(statements, "ns", 0, true, count));
	EXPECT_EQ(count, 0);
	EXPECT_FALSE(zx::ComputeDatabaseMaxStamp(statements, stamp));
	EXPECT_EQ(stamp, 0);
}

TEST(DatabaseCompute, CountBeyondFailsOnceTheTableIsGone)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	int64_t count = 0;
	double stamp = 0;
	zx::ComputeDatabaseCountBeyond(statements, "ns", 0, true, count);
	zx::ComputeDatabaseMaxStamp(statements, stamp);
	exec(memory.db, "DROP TABLE Zandronum");
	EXPECT_FALSE(zx::ComputeDatabaseCountBeyond(statements, "ns", 0, true, count));
	EXPECT_FALSE(zx::ComputeDatabaseMaxStamp(statements, stamp));
}
