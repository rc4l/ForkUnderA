#include "features/database/computation/database_store_compute.h"
#include "features/database/computation/database_test_support.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>
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

// [Doruk-Aksoy] Between the writer applying a batch and the next tic end acknowledging it, the file and the overlay both hold the increment.
TEST(DatabaseStore, AnAppliedButUnacknowledgedIncrementIsCountedOnce)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_doublecount");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseIncrementOp("ns", "k", 5));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	ASSERT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "5"; }));
	EXPECT_EQ(server.get("k"), "5");
	zx::DatabaseEntries entries;
	EXPECT_TRUE(zx::ComputeDatabaseStoreEntries(server.store, server.reader, "ns", entries, kWall));
	EXPECT_EQ(entries, (zx::DatabaseEntries{ { "k", "5" } }));
	EXPECT_TRUE(zx::ComputeDatabaseStoreSorted(server.store, server.reader, "ns", 5, 0, true, entries, kWall));
	EXPECT_EQ(entries, (zx::DatabaseEntries{ { "k", "5" } }));
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

TEST(DatabaseStore, AStockServersDeleteWinsOverThisServersOlderQueuedWrite)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_stock");
	sqlite3 *stock = nullptr;
	sqlite3_open(file.path.string().c_str(), &stock);
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "first"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	ASSERT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "first"; }));
	// [Doruk-Aksoy] The stock server holds the file while this server queues a write, then deletes the key with the old engine's plain SQL.
	exec(stock, "BEGIN IMMEDIATE");
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "queued"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	exec(stock, "DELETE FROM Zandronum WHERE Namespace='ns' AND KeyName='k'");
	exec(stock, "COMMIT");
	ASSERT_TRUE(waitFor([&]() { return server.settled(); }));
	EXPECT_EQ(fileValue(file.db, "k"), "<missing>");
	sqlite3_close(stock);
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

TEST(DatabaseStore, AClosePrunesEverythingTheFileTookFromTheOutbox)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_prune");
	{
		Server server(file.path);
		ASSERT_TRUE(server.open(file.path));
		zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
		zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
		ASSERT_TRUE(waitFor([&]() { return server.settled(); }));
		zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	}
	sqlite3 *outbox = nullptr;
	sqlite3_open(zx::ComputeDatabaseOutboxPath(file.path.string(), 0).c_str(), &outbox);
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(outbox, "SELECT COUNT(*) FROM Ops", -1, &statement, nullptr);
	ASSERT_EQ(sqlite3_step(statement), SQLITE_ROW);
	EXPECT_EQ(sqlite3_column_int(statement, 0), 0);
	sqlite3_finalize(statement);
	sqlite3_close(outbox);
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

TEST(DatabaseStore, OutboxWritesThatFailAreRetriedUntilTheyLand)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_retry");
	sqlite3 *blocker = nullptr;
	sqlite3_open(file.path.string().c_str(), &blocker);
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	server.messages();
	// [Doruk-Aksoy] The shared file is held, so the batch stays pending while the outbox refuses it, then takes it on a later tic.
	exec(blocker, "BEGIN IMMEDIATE");
	exec(server.store.outbox.db, "PRAGMA query_only=1");
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_EQ(server.store.unsaved.size(), 1u);
	std::string messages = server.messages();
	EXPECT_NE(messages.find("retried every tic"), std::string::npos);
	EXPECT_EQ(messages.find("retried every tic"), messages.rfind("retried every tic"));
	exec(server.store.outbox.db, "PRAGMA query_only=0");
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(server.store.unsaved.empty());
	EXPECT_NE(server.messages().find("took the database writes it missed"), std::string::npos);
	// [Doruk-Aksoy] The batch is in the outbox now, so it survives the process dying before the file frees up.
	crash(server.store);
	exec(blocker, "ROLLBACK");
	Server restarted(file.path);
	ASSERT_TRUE(restarted.open(file.path));
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "v"; }));
	sqlite3_close(blocker);
}

TEST(DatabaseStore, BatchesTheFileAlreadyHoldsAreNotRetriedIntoTheOutbox)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_retryapplied");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	exec(server.store.outbox.db, "PRAGMA query_only=1");
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	ASSERT_TRUE(waitFor([&]() { return server.settled(); }));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(server.store.unsaved.empty());
	exec(server.store.outbox.db, "PRAGMA query_only=0");
}

TEST(DatabaseStore, WarnsOnceWhenTheWriterMakesNoProgress)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_stall");
	sqlite3 *blocker = nullptr;
	sqlite3_open(file.path.string().c_str(), &blocker);
	Server server(file.path);
	server.store.stallWarnSeconds = 0.05;
	ASSERT_TRUE(server.open(file.path));
	server.messages();
	exec(blocker, "BEGIN IMMEDIATE");
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	std::string seen;
	EXPECT_TRUE(waitFor([&]() {
		zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
		seen += server.messages();
		return seen.find("has waited") != std::string::npos;
	})) << seen;
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	seen += server.messages();
	EXPECT_EQ(seen.find("has waited"), seen.rfind("has waited"));
	exec(blocker, "ROLLBACK");
	EXPECT_TRUE(waitFor([&]() { return server.settled(); }));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_FALSE(server.store.stallWarned);
	sqlite3_close(blocker);
}

TEST(DatabaseStore, NamesAnOutboxItCannotOpen)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_unreadable");
	std::ofstream(zx::ComputeDatabaseOutboxPath(file.path.string(), 0), std::ios::binary) << std::string(4096, 'x');
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	EXPECT_EQ(server.store.slot, 1);
	EXPECT_NE(server.messages().find("cannot be opened as a database"), std::string::npos);
}

TEST(DatabaseStore, TheOrphanScanNamesAnUnreadableOutboxOnce)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_scanunreadable");
	std::ofstream(zx::ComputeDatabaseOutboxPath(file.path.string(), 1), std::ios::binary) << std::string(4096, 'x');
	Server server(file.path);
	server.store.writer.maintenanceMs = 5;
	ASSERT_TRUE(server.open(file.path));
	EXPECT_EQ(server.store.slot, 0);
	std::string seen;
	EXPECT_TRUE(waitFor([&]() { seen += server.messages(); return seen.find("fua-outbox-1 cannot be opened") != std::string::npos; })) << seen;
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	seen += server.messages();
	EXPECT_EQ(seen.find("fua-outbox-1 cannot be opened"), seen.rfind("fua-outbox-1 cannot be opened"));
}

TEST(DatabaseStore, SaysWhenItClosesAModTransactionLeftOpen)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_modexpiry");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	zx::ComputeDatabaseStoreBegin(server.store);
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	for (int tic = 0; tic < zx::kDatabaseModTransactionTics; ++tic)
		zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_NE(server.messages().find("stayed open for a minute"), std::string::npos);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "v"; }));
}

TEST(DatabaseStore, NullNamesAreTheEmptyString)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_null");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseIncrementOp(nullptr, nullptr, 2));
	std::string value;
	bool found = false;
	EXPECT_TRUE(zx::ComputeDatabaseStoreGet(server.store, server.reader, nullptr, nullptr, value, found, kWall));
	EXPECT_EQ(value, "2");
	zx::DatabaseEntries entries;
	EXPECT_TRUE(zx::ComputeDatabaseStoreEntries(server.store, server.reader, nullptr, entries, kWall));
	EXPECT_EQ(entries, (zx::DatabaseEntries{ { "", "2" } }));
	int rank = 0;
	EXPECT_TRUE(zx::ComputeDatabaseStoreRank(server.store, server.reader, nullptr, nullptr, true, rank));
	EXPECT_EQ(rank, 1);
}

TEST(DatabaseStore, AReadThatCannotOpenItsSnapshotFails)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_snapshot");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseIncrementOp("ns", "k", 1));
	exec(server.db, "BEGIN");
	std::string value;
	bool found = false;
	EXPECT_FALSE(zx::ComputeDatabaseStoreGet(server.store, server.reader, "ns", "k", value, found, kWall));
	exec(server.db, "COMMIT");
	exec(file.db, "DROP TABLE fua_Applied");
	EXPECT_FALSE(zx::ComputeDatabaseStoreGet(server.store, server.reader, "ns", "k", value, found, kWall));
	exec(file.db, "CREATE TABLE fua_Applied(Server text PRIMARY KEY, Seq integer)");
}

TEST(DatabaseStore, ATransactionItsLevelLeftOpenIsEndedWithItsWritesKept)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_orphantxn");
	Server server(file.path);
	ASSERT_TRUE(server.open(file.path));
	EXPECT_FALSE(zx::ComputeDatabaseStoreEndOrphanedTransaction(server.store));
	zx::ComputeDatabaseStoreBegin(server.store);
	zx::ComputeDatabaseStoreWrite(server.store, zx::ComputeDatabaseSetOp("ns", "k", "v"));
	EXPECT_TRUE(zx::ComputeDatabaseStoreEndOrphanedTransaction(server.store));
	zx::ComputeDatabaseStoreTicEnd(server.store, kWall);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "k") == "v"; }));
	EXPECT_EQ(zx::ComputeDatabaseStoreEnd(server.store), zx::DatabaseTxnResult::NotOpen);
}

TEST(DatabaseStore, LeftoversTheFileAlreadyHoldsAreNeitherShownNorResent)
{
	ZX_REQUIRE_FILE_DATABASES();
	FileDb file("store_partial");
	std::string server;
	{
		zx::DatabaseOutbox outbox;
		std::vector<zx::DatabaseBatch> leftover;
		uint64_t next = 0;
		ASSERT_EQ(zx::ComputeDatabaseOutboxOpen(outbox, zx::ComputeDatabaseOutboxPath(file.path.string(), 0), true, leftover, next), zx::DatabaseOutboxOpen::Opened);
		server = outbox.server;
		zx::DatabaseBatch applied, pending;
		applied.seq = 1;
		applied.stamp = 100;
		applied.ops = { zx::ComputeDatabaseSetOp("ns", "k", "stale") };
		pending.seq = 2;
		pending.stamp = 101;
		pending.ops = { zx::ComputeDatabaseIncrementOp("ns", "n", 5) };
		zx::ComputeDatabaseOutboxAppend(outbox, { applied, pending }, 3, 0);
	}
	// [Doruk-Aksoy] The dead server's writer had applied batch 1 and a newer write replaced it since.
	exec(file.db, ("INSERT INTO fua_Applied VALUES('" + server + "', 1)").c_str());
	exec(file.db, "INSERT INTO Zandronum VALUES('ns','k','newer','200')");
	Server restarted(file.path);
	ASSERT_TRUE(restarted.open(file.path));
	EXPECT_EQ(restarted.get("k"), "newer");
	EXPECT_EQ(restarted.get("n"), "5");
	EXPECT_NE(restarted.messages().find("Resuming 1 "), std::string::npos);
	EXPECT_TRUE(waitFor([&]() { return fileValue(file.db, "n") == "5"; }));
	EXPECT_EQ(fileValue(file.db, "k"), "newer");
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

