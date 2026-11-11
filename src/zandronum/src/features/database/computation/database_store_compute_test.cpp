#include "features/database/computation/database_store_compute.h"
#include "features/database/computation/database_test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <sqlite3.h>
#include <string>
#include <vector>

using namespace zxdbtest;


namespace
{

const double kWall = 1.7e9;

// [Doruk-Aksoy] One server: its reading connection to the shared file and its store.
struct Server
{
	sqlite3 *db = nullptr;
	zx::DatabaseStatements reader;
	zx::DatabaseStore store;
	explicit Server(const std::filesystem::path &path)
	{
		sqlite3_open(path.string().c_str(), &db);
		zx::ComputeDatabaseBind(reader, db);
	}
	~Server()
	{
		zx::ComputeDatabaseStoreClose(store, kWall, 2000);
		zx::ComputeDatabaseBind(reader, nullptr);
		sqlite3_close(db);
	}
	bool open(const std::filesystem::path &path)
	{
		return zx::ComputeDatabaseStoreOpen(store, path.string(), { "PRAGMA synchronous=2" }, reader, kWall);
	}
	std::string get(const char *key)
	{
		std::string value;
		bool found = false;
		EXPECT_TRUE(zx::ComputeDatabaseStoreGet(store, reader, "ns", key, value, found, kWall));
		return found ? value : "<missing>";
	}
	std::string messages()
	{
		std::string all;
		for (const std::string &message : zx::ComputeDatabaseStoreTakeMessages(store))
			all += message + "\n";
		return all;
	}
	bool settled()
	{
		return zx::ComputeDatabaseWriterQueued(store.writer) == 0 && store.writer.applied.load() + 1 >= store.queue.nextSeq;
	}
};

std::string fileValue(sqlite3 *db, const char *key)
{
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, db);
	std::string value;
	bool found = false;
	zx::ComputeDatabaseGet(statements, "ns", key, value, found);
	return found ? value : "<missing>";
}

// [Doruk-Aksoy] What dies with a crashed process: the writer stops without draining and the outbox closes without being pruned.
void crash(zx::DatabaseStore &store)
{
	zx::ComputeDatabaseWriterStop(store.writer, 0);
	zx::ComputeDatabaseOutboxClose(store.outbox);
	store.open = false;
}

}

TEST(DatabaseStore, WritesAreReadBackAtOnceAndReachTheFileAfterTheTic)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_basic");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	EXPECT_EQ(server.store.slot, 0);
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "name", "doom"));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseIncrementOp("ns", "kills", 3));
	EXPECT_EQ(server.get("name"), "doom");
	EXPECT_EQ(server.get("kills"), "3");
	EXPECT_EQ(fileValue(file.db, "name"), "<missing>");
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "kills") == "3"; }));
	EXPECT_EQ(fileValue(file.db, "name"), "doom");
	EXPECT_TRUE(waitFor([&]() { return server.settled(); }));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(server.store.queue.pending.empty());
	zx::DatabaseEntries entries;
	EXPECT_TRUE(zx::ComputeDatabaseStoreEntries(server.store, server.reader, "ns", entries, kWall));
	EXPECT_EQ(entries.size(), 2u);
	EXPECT_TRUE(zx::ComputeDatabaseStoreSorted(server.store, server.reader, "ns", 1, 0, true, entries, kWall));
	EXPECT_EQ(entries, (zx::DatabaseEntries{ { "kills", "3" } }));
	int rank = 0;
	EXPECT_TRUE(zx::ComputeDatabaseStoreRank(server.store, server.reader, "ns", "kills", true, rank));
	EXPECT_EQ(rank, 1);
	EXPECT_EQ(server.messages(), "");
}

TEST(DatabaseStore, ModTransactionsAreBatchesWithTheOldErrors)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_txn");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	EXPECT_EQ(zx::ComputeDatabaseStoreEnd(server.store), zx::DatabaseTxnResult::NotOpen);
	EXPECT_EQ(zx::ComputeDatabaseStoreBegin(server.store), zx::DatabaseTxnResult::Ok);
	EXPECT_EQ(zx::ComputeDatabaseStoreBegin(server.store), zx::DatabaseTxnResult::AlreadyOpen);
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(server.store.queue.pending.empty());
	EXPECT_EQ(zx::ComputeDatabaseStoreEnd(server.store), zx::DatabaseTxnResult::Ok);
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_EQ(server.store.queue.pending.size(), 1u);
}

TEST(DatabaseStore, AClosedStoreDoesNothingAtTicEnd)
{
	zx::DatabaseStore store;
	zx::ComputeDatabaseStoreTicEnd(store, kWall);
	zx::ComputeDatabaseStoreClose(store, kWall, 0);
	EXPECT_EQ(zx::ComputeDatabaseStoreStatus(store), (std::vector<std::string>{ "Database writes: direct (no queue)." }));
}

TEST(DatabaseStore, ACrashedServersWritesLandWhenItStartsAgain)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_crash");
	sqlite3 *blocker = nullptr;
	sqlite3_open(file.path.string().c_str(), &blocker);
	{
		Server server(file.path);
		ASSERT_TRUE(server.open(file.path));
		// [Doruk-Aksoy] Another server holds the file, so the batch is durable in the outbox but not yet in the file when the process dies.
		exec(blocker, "BEGIN IMMEDIATE");
		zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseIncrementOp("ns", "gold", 25));
		zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
		crash(server.store);
		exec(blocker, "ROLLBACK");
	}
	EXPECT_EQ(fileValue(file.db, "gold"), "<missing>");
	Server restarted(file.path);
	ASSERT_TRUE(restarted.open(file.path));
	EXPECT_EQ(restarted.get("gold"), "25");
	EXPECT_NE(restarted.messages().find("Resuming 1"), std::string::npos);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "gold") == "25"; }));
	sqlite3_close(blocker);
}

TEST(DatabaseStore, TheNextServerOnAFreedSlotResumesItsBatches)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_orphan");
	sqlite3 *blocker = nullptr;
	sqlite3_open(file.path.string().c_str(), &blocker);
	auto first = std::make_unique<Server>(file.path);
	ASSERT_TRUE(first->open(file.path));
	auto second = std::make_unique<Server>(file.path);
	ASSERT_TRUE(second->open(file.path));
	EXPECT_EQ(second->store.slot, 1);
	exec(blocker, "BEGIN IMMEDIATE");
	zx::ComputeDatabaseStoreWrite(second->store, zx::ComputeDatabaseSetOp("ns", "left", "behind"));
	zx::ComputeDatabaseStoreTicEnd(second->store, kWall);
	crash(second->store);
	exec(blocker, "ROLLBACK");
	// [Doruk-Aksoy] Slot 0 stays held, so the next server takes the freed slot 1 and resumes what it kept.
	Server third(file.path);
	ASSERT_TRUE(third.open(file.path));
	EXPECT_EQ(third.store.slot, 1);
	EXPECT_EQ(third.get("left"), "behind");
	second.reset();
	Server fourth(file.path);
	ASSERT_TRUE(fourth.open(file.path));
	// [Doruk-Aksoy] Any free slot will do: another writer's orphan scan can hold one for a moment while a server starts.
	EXPECT_GT(fourth.store.slot, 1);
	sqlite3_close(blocker);
}

TEST(DatabaseStore, OrphanedSlotsAreReplayedByTheWriterOnItsSchedule)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_schedule");
	sqlite3 *blocker = nullptr;
	sqlite3_open(file.path.string().c_str(), &blocker);
	Server holder(file.path);
	ASSERT_TRUE(holder.open(file.path));
	{
		Server gone(file.path);
		ASSERT_TRUE(gone.open(file.path));
		Server middle(file.path);
		ASSERT_TRUE(middle.open(file.path));
		EXPECT_EQ(middle.store.slot, 2);
		exec(blocker, "BEGIN IMMEDIATE");
		zx::ComputeDatabaseStoreWrite(middle.store, zx::ComputeDatabaseSetOp("ns", "orphan", "kept"));
		zx::ComputeDatabaseStoreTicEnd(middle.store, kWall);
		crash(middle.store);
		exec(blocker, "ROLLBACK");
	}
	// [Doruk-Aksoy] Slot 1 is free again and taken by the newcomer, so slot 2 is an orphan only replay reaches.
	Server newcomer(file.path);
	ASSERT_TRUE(newcomer.open(file.path));
	EXPECT_EQ(newcomer.store.slot, 1);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "orphan") == "kept"; }));
	std::string seen;
	EXPECT_TRUE(waitFor([&]() { seen += newcomer.messages(); return seen.find("a stopped server left") != std::string::npos; })) << seen;
	sqlite3_close(blocker);
}

TEST(DatabaseStore, TwoServersIncrementingOneKeyBothCount)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_two");
	Server a(file.path), b(file.path);
	ASSERT_TRUE(a.open(file.path));
	ASSERT_TRUE(b.open(file.path));
	for (int tic = 0; tic < 50; ++tic)
	{
		zx::ComputeDatabaseStoreWrite(a.store, zx::ComputeDatabaseIncrementOp("ns", "global", 1));
		zx::ComputeDatabaseStoreWrite(b.store, zx::ComputeDatabaseIncrementOp("ns", "global", 2));
		zx::ComputeDatabaseStoreTicEnd(a.store, kWall + tic);
		zx::ComputeDatabaseStoreTicEnd(b.store, kWall + tic);
	}
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "global") == "150"; }));
}

TEST(DatabaseStore, ClosingDrainsSoTheOutboxIsEmptyOnTheNextStart)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_close");
	{
		Server server(file.path);
		ASSERT_TRUE(server.open(file.path));
		zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
		zx::ComputeDatabaseStoreBegin(server.store);
		zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "unfinished", "v"));
	}
	EXPECT_EQ(fileValue(file.db, "k"), "v");
	EXPECT_EQ(fileValue(file.db, "unfinished"), "<missing>");
	Server again(file.path);
	ASSERT_TRUE(again.open(file.path));
	EXPECT_TRUE(again.store.queue.pending.empty());
	EXPECT_EQ(again.messages().find("Resuming"), std::string::npos);
}

TEST(DatabaseStore, WarnsOnceWhenThisClockIsBehindTheFile)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_clock");
	exec(file.db, "INSERT INTO Zandronum VALUES('ns','future','v','1800000000')");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	server.get("future");
	const std::string messages = server.messages();
	EXPECT_NE(messages.find("clock"), std::string::npos);
	EXPECT_EQ(messages.find("clock"), messages.rfind("clock"));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "future", "ours"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "future") == "ours"; }));
}

TEST(DatabaseStore, ReportsAWriterThatKeepsFailingOnce)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_failing");
	Server server(file.path);
	server.store.writer.failedRetryMs = 5;
	ASSERT_TRUE(server.open(file.path));
	server.messages();
	exec(file.db, "DROP TABLE fua_Applied");
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(waitFor([&]() { return server.store.writer.failures.load() > 1; }));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	const std::string messages = server.messages();
	EXPECT_NE(messages.find("retries it"), std::string::npos);
	EXPECT_EQ(messages.find("retries it"), messages.rfind("retries it"));
	const std::vector<std::string> status = zx::ComputeDatabaseStoreStatus(server.store);
	EXPECT_NE(status.back().find("no such table"), std::string::npos) << status.back();
	// [Doruk-Aksoy] The writer takes the write lock on every retry, so this change waits its turn.
	sqlite3_busy_timeout(file.db, 5000);
	exec(file.db, "CREATE TABLE fua_Applied(Server text PRIMARY KEY, Seq integer)");
}

TEST(DatabaseStore, KeepsGoingWhenTheOutboxCannotBeWritten)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_nooutbox");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	server.messages();
	zx::ComputeDatabaseOutboxClose(server.store.outbox);
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "memory"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_NE(server.messages().find("Could not write"), std::string::npos);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "memory"; }));
}

TEST(DatabaseStore, DoesNotOpenWithoutAnOutboxOrAFile)
{
	ZX_REQUIRE_FILE_DATABASES();
	zx::DatabaseStatements reader;
	zx::DatabaseStore nowhere;
	const std::filesystem::path unreachable = std::filesystem::temp_directory_path() / "zx_db_no_such_folder" / "x.db";
	EXPECT_FALSE(zx::ComputeDatabaseStoreOpen(nowhere, unreachable.string(), {}, reader, kWall));
	EXPECT_NE(zx::ComputeDatabaseStoreTakeMessages(nowhere).front().find("No outbox"), std::string::npos);
	const std::filesystem::path missing = tempDatabasePath("store_missing");
	zx::DatabaseStore noFile;
	EXPECT_FALSE(zx::ComputeDatabaseStoreOpen(noFile, missing.string(), {}, reader, kWall));
	EXPECT_NE(zx::ComputeDatabaseStoreTakeMessages(noFile).front().find("Could not open"), std::string::npos);
	EXPECT_FALSE(noFile.open);
	removeDatabaseFiles(missing);
}

TEST(DatabaseStore, StatusNamesTheOutboxAndTheQueue)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_status");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	const std::vector<std::string> status = zx::ComputeDatabaseStoreStatus(server.store);
	ASSERT_EQ(status.size(), 6u);
	EXPECT_NE(status[0].find("slot 0"), std::string::npos);
	EXPECT_NE(status[1].find(server.store.outbox.server), std::string::npos);
	EXPECT_NE(status.back().find("none"), std::string::npos);
}

