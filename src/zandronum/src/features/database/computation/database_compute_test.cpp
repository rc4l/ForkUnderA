#include "features/database/computation/database_compute.h"

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

namespace
{

const char *const kCreateTable = "CREATE TABLE if not exists Zandronum(Namespace text, KeyName text, Value text, Timestamp text, PRIMARY KEY (Namespace, KeyName))";

void exec(sqlite3 *db, const char *sql)
{
	ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK) << sql << ": " << sqlite3_errmsg(db);
}

// [Doruk-Aksoy] An in-memory database with the engine's table and the value index.
struct MemoryDb
{
	sqlite3 *db = nullptr;
	MemoryDb()
	{
		sqlite3_open(":memory:", &db);
		exec(db, kCreateTable);
		exec(db, zx::ComputeDatabaseIndexSql());
	}
	~MemoryDb() { sqlite3_close(db); }
};

// [Doruk-Aksoy] The pre-cache za_database.cpp logic, statement for statement, as the reference the cached path must match.
struct LegacyDb
{
	sqlite3 *db = nullptr;
	LegacyDb()
	{
		sqlite3_open(":memory:", &db);
		exec(db, kCreateTable);
	}
	~LegacyDb() { sqlite3_close(db); }

	sqlite3_stmt *prepare(const std::string &sql)
	{
		sqlite3_stmt *statement = nullptr;
		sqlite3_prepare(db, sql.c_str(), -1, &statement, nullptr);
		return statement;
	}
	void keyed(sqlite3_stmt *statement, const char *ns, const char *key)
	{
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 2, key, -1, SQLITE_STATIC);
	}
	void run(const std::string &sql, const char *ns, const char *key, const char *value)
	{
		sqlite3_stmt *statement = prepare(sql);
		keyed(statement, ns, key);
		if (value != nullptr)
			sqlite3_bind_text(statement, 3, value, -1, SQLITE_STATIC);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}
	bool exists(const char *ns, const char *key)
	{
		sqlite3_stmt *statement = prepare("SELECT * FROM Zandronum WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		const bool found = sqlite3_step(statement) == SQLITE_ROW;
		sqlite3_finalize(statement);
		return found;
	}
	void saveSet(const char *ns, const char *key, const char *value)
	{
		const bool nonEmpty = value != nullptr && *value != '\0';
		if (exists(ns, key))
		{
			if (nonEmpty)
				run("UPDATE Zandronum SET Value=?3,Timestamp=(SELECT (julianday('now') - 2440587.5)*86400.0) WHERE Namespace=?1 AND KeyName=?2", ns, key, value);
			else
				run("DELETE FROM Zandronum WHERE Namespace=?1 AND KeyName=?2", ns, key, nullptr);
		}
		else if (nonEmpty)
			run("INSERT INTO Zandronum VALUES(?1,?2,?3,(SELECT (julianday('now') - 2440587.5)*86400.0))", ns, key, value);
	}
	std::string get(const char *ns, const char *key)
	{
		if (!exists(ns, key))
			return "";
		sqlite3_stmt *statement = prepare("SELECT * FROM Zandronum WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		sqlite3_step(statement);
		std::string value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
		sqlite3_finalize(statement);
		return value;
	}
	void increment(const char *ns, const char *key, int increment)
	{
		if (!exists(ns, key))
		{
			const std::string value = std::to_string(increment);
			run("INSERT INTO Zandronum VALUES(?1,?2,?3,(SELECT (julianday('now') - 2440587.5)*86400.0))", ns, key, value.c_str());
			return;
		}
		sqlite3_stmt *statement = prepare("UPDATE Zandronum SET Value=(SELECT CAST(Value AS INTEGER) FROM Zandronum WHERE Namespace=?1 AND KeyName=?2)+?3,Timestamp=(SELECT (julianday('now') - 2440587.5)*86400.0) WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		sqlite3_bind_int(statement, 3, increment);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}
	int rank(const char *ns, const char *key, bool descending)
	{
		if (!exists(ns, key))
			return -1;
		sqlite3_stmt *statement = prepare(std::string("SELECT COUNT(*) from Zandronum WHERE Namespace=?1 AND CAST(Value AS INTEGER)") + (descending ? ">" : "<") + "(SELECT CAST(Value AS INTEGER) FROM Zandronum WHERE Namespace=?2 AND KeyName=?3)");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 2, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 3, key, -1, SQLITE_STATIC);
		sqlite3_step(statement);
		const int result = sqlite3_column_int(statement, 0) + 1;
		sqlite3_finalize(statement);
		return result;
	}
	zx::DatabaseEntries rows(sqlite3_stmt *statement)
	{
		zx::DatabaseEntries entries;
		while (sqlite3_step(statement) == SQLITE_ROW)
			entries.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(statement, 1)), reinterpret_cast<const char *>(sqlite3_column_text(statement, 2)));
		sqlite3_finalize(statement);
		return entries;
	}
	zx::DatabaseEntries sorted(const char *ns, int count, int offset, bool descending)
	{
		sqlite3_stmt *statement = prepare(std::string("SELECT * from Zandronum WHERE Namespace=?1 ORDER BY CAST(Value AS INTEGER) ") + (descending ? "DESC" : "ASC") + " LIMIT ?2 OFFSET ?3");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_int(statement, 2, count);
		sqlite3_bind_int(statement, 3, offset);
		return rows(statement);
	}
	zx::DatabaseEntries entries(const char *ns)
	{
		sqlite3_stmt *statement = prepare("SELECT * from Zandronum WHERE Namespace=?1");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		return rows(statement);
	}
};

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

#ifndef __EMSCRIPTEN__

namespace
{

// [Doruk-Aksoy] A database file of its own in the temp folder, removed with its -wal and -shm files.
struct FileDb
{
	std::filesystem::path path;
	sqlite3 *db = nullptr;
	explicit FileDb(const char *name)
	{
		path = std::filesystem::temp_directory_path() / (std::string("zx_db_") + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
		sqlite3_open(path.string().c_str(), &db);
		exec(db, kCreateTable);
		for (const std::string &pragma : zx::ComputeDatabasePragmas(false, true, 2))
			exec(db, pragma.c_str());
		exec(db, "PRAGMA wal_autocheckpoint=0");
	}
	~FileDb()
	{
		sqlite3_close(db);
		std::error_code ignored;
		for (const char *suffix : { "", "-wal", "-shm" })
			std::filesystem::remove(path.string() + suffix, ignored);
	}
	// [Doruk-Aksoy] Read through a handle of its own, since Windows can report a stale size for a file another handle holds open.
	std::uintmax_t mainFileSize() const
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		return static_cast<std::uintmax_t>(stream.tellg());
	}
};

bool waitFor(const std::function<bool()> &condition)
{
	for (int i = 0; i < 500 && !condition(); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return condition();
}

}

TEST(DatabaseCheckpointer, MissingFileDoesNotStart)
{
	zx::DatabaseCheckpointer checkpointer;
	const std::string path = (std::filesystem::temp_directory_path() / "zx_db_does_not_exist.db").string();
	EXPECT_FALSE(zx::ComputeDatabaseCheckpointerStart(checkpointer, path.c_str(), 10));
	EXPECT_EQ(checkpointer.db, nullptr);
	EXPECT_FALSE(checkpointer.thread.joinable());
}

TEST(DatabaseCheckpointer, MovesTheLogIntoTheMainFileOffTheCallingThread)
{
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
	EXPECT_GT(checkpointer.checkpoints.load(), 0);
	EXPECT_TRUE(waitFor([&]() { return checkpointer.lastCopiedFrames.load() > 0; }));
	zx::ComputeDatabaseCheckpointerStop(checkpointer);
	EXPECT_EQ(checkpointer.db, nullptr);
	EXPECT_FALSE(checkpointer.thread.joinable());
}

TEST(DatabaseCheckpointer, RestartsAndStopsSafely)
{
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
	FileDb file("destroy");
	auto checkpointer = std::make_unique<zx::DatabaseCheckpointer>();
	ASSERT_TRUE(zx::ComputeDatabaseCheckpointerStart(*checkpointer, file.path.string().c_str(), 60000));
	checkpointer.reset();
	SUCCEED();
}

#endif
