#include "features/database/computation/database_queue_compute.h"
#include "features/database/computation/database_test_support.h"
#include "features/database/computation/database_writer_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <sqlite3.h>
#include <string>
#include <vector>

using namespace zxdbtest;

namespace
{

int64_t sqliteCast(sqlite3 *db, const std::string &text)
{
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, "SELECT CAST(?1 AS INTEGER)", -1, &statement, nullptr);
	sqlite3_bind_text(statement, 1, text.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_step(statement);
	const int64_t value = sqlite3_column_int64(statement, 0);
	sqlite3_finalize(statement);
	return value;
}

zx::DatabaseOp set(const char *ns, const char *key, const char *value)
{
	return zx::ComputeDatabaseSetOp(ns, key, value);
}

zx::DatabaseOp inc(const char *ns, const char *key, int64_t delta)
{
	return zx::ComputeDatabaseIncrementOp(ns, key, delta);
}


}

TEST(DatabaseQueueCast, MatchesSqliteOnHandPickedText)
{
	MemoryDb memory;
	for (const char *text : { "", "0", "42", "-42", "+7", "  12", "\t\n\v\f\r9", "12abc", "1.9", "1e3", "-", "+", "abc", " - 5", "007",
			"9223372036854775807", "9223372036854775808", "99999999999999999999", "-9223372036854775808", "-9223372036854775809", "-99999999999999999999" })
		EXPECT_EQ(zx::ComputeDatabaseCastInteger(text), sqliteCast(memory.db, text)) << "'" << text << "'";
}

TEST(DatabaseQueueCast, MatchesSqliteOnRandomText)
{
	MemoryDb memory;
	std::mt19937 random(99);
	const std::string alphabet = " \t+-0123456789.eEx9";
	for (int i = 0; i < 4000; ++i)
	{
		std::string text;
		const int length = static_cast<int>(random() % 24);
		for (int j = 0; j < length; ++j)
			text += alphabet[random() % alphabet.size()];
		ASSERT_EQ(zx::ComputeDatabaseCastInteger(text), sqliteCast(memory.db, text)) << "'" << text << "'";
	}
}

TEST(DatabaseQueueOps, SetOfEmptyOrNullIsADelete)
{
	EXPECT_EQ(set("ns", "k", "v").kind, zx::DatabaseOpKind::Set);
	EXPECT_EQ(set("ns", "k", "").kind, zx::DatabaseOpKind::Delete);
	EXPECT_EQ(set("ns", "k", nullptr).kind, zx::DatabaseOpKind::Delete);
	const zx::DatabaseOp nulls = zx::ComputeDatabaseSetOp(nullptr, nullptr, "v");
	EXPECT_EQ(nulls.ns, "");
	EXPECT_EQ(nulls.key, "");
	const zx::DatabaseOp increment = zx::ComputeDatabaseIncrementOp(nullptr, nullptr, 3);
	EXPECT_EQ(increment.kind, zx::DatabaseOpKind::Increment);
	EXPECT_EQ(increment.ns, "");
	EXPECT_EQ(increment.delta, 3);
	EXPECT_TRUE(set("ns", "k", "v") == set("ns", "k", "v"));
	EXPECT_FALSE(set("ns", "k", "v") == set("ns", "k", "w"));
}

TEST(DatabaseQueueClock, FollowsTheWallClockAndNeverRepeats)
{
	zx::DatabaseClock clock;
	EXPECT_EQ(zx::ComputeDatabaseClockStamp(clock, 100), 100);
	EXPECT_DOUBLE_EQ(zx::ComputeDatabaseClockStamp(clock, 100), 100 + zx::kDatabaseClockStep);
	EXPECT_DOUBLE_EQ(zx::ComputeDatabaseClockStamp(clock, 50), 100 + 2 * zx::kDatabaseClockStep);
	EXPECT_EQ(zx::ComputeDatabaseClockStamp(clock, 200), 200);
}

TEST(DatabaseQueueClock, ObservedStampsCarryCausalOrderPastASlowClock)
{
	zx::DatabaseClock clock;
	EXPECT_FALSE(zx::ComputeDatabaseClockObserve(clock, 500, 480));
	EXPECT_GT(zx::ComputeDatabaseClockStamp(clock, 480), 500);
	EXPECT_FALSE(zx::ComputeDatabaseClockObserve(clock, 10, 480));
	EXPECT_TRUE(zx::ComputeDatabaseClockObserve(clock, 480 + zx::kDatabaseClockAheadWarning + 1, 480));
}

TEST(DatabaseQueueBatcher, TicsAndModTransactionsMakeSeparateBatches)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "1"));
	EXPECT_EQ(zx::ComputeDatabaseBatcherBegin(queue.batcher), zx::DatabaseTxnResult::Ok);
	EXPECT_EQ(zx::ComputeDatabaseBatcherBegin(queue.batcher), zx::DatabaseTxnResult::AlreadyOpen);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "b", "2"));
	// [Doruk-Aksoy] A mod transaction spanning the tic stays open, so only the write before it leaves.
	std::vector<zx::DatabaseBatch> sealed = zx::ComputeDatabaseQueueSeal(queue, 10);
	ASSERT_EQ(sealed.size(), 1u);
	EXPECT_EQ(sealed[0].ops, (std::vector<zx::DatabaseOp>{ set("ns", "a", "1") }));
	EXPECT_EQ(sealed[0].seq, 1u);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "c", "3"));
	EXPECT_EQ(zx::ComputeDatabaseBatcherEnd(queue.batcher), zx::DatabaseTxnResult::Ok);
	EXPECT_EQ(zx::ComputeDatabaseBatcherEnd(queue.batcher), zx::DatabaseTxnResult::NotOpen);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "d", "4"));
	sealed = zx::ComputeDatabaseQueueSeal(queue, 11);
	ASSERT_EQ(sealed.size(), 2u);
	EXPECT_EQ(sealed[0].ops, (std::vector<zx::DatabaseOp>{ set("ns", "b", "2"), set("ns", "c", "3") }));
	EXPECT_EQ(sealed[1].ops, (std::vector<zx::DatabaseOp>{ set("ns", "d", "4") }));
	EXPECT_EQ(sealed[1].seq, 3u);
	EXPECT_GT(sealed[1].stamp, sealed[0].stamp);
	EXPECT_EQ(queue.pending.size(), 3u);
	EXPECT_TRUE(zx::ComputeDatabaseQueueSeal(queue, 12).empty());
}

TEST(DatabaseQueueBatcher, AnEmptyModTransactionMakesNoBatch)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseBatcherEnd(queue.batcher);
	EXPECT_TRUE(zx::ComputeDatabaseQueueSeal(queue, 1).empty());
}

TEST(DatabaseQueueOverlay, ResolvesSetsDeletesAndIncrements)
{
	zx::DatabaseOverlay overlay;
	std::string value;
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "only", 4));
	const zx::DatabaseOverlayEntry &only = overlay[{ "ns", "only" }];
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(only, false, "", value));
	EXPECT_EQ(value, "4");
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(only, true, "10", value));
	EXPECT_EQ(value, "14");
	zx::ComputeDatabaseOverlayAdd(overlay, set("ns", "s", "abc"));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(overlay[{ "ns", "s" }], true, "ignored", value));
	EXPECT_EQ(value, "abc");
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "s", 2));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(overlay[{ "ns", "s" }], false, "", value));
	EXPECT_EQ(value, "2");
	zx::ComputeDatabaseOverlayAdd(overlay, set("ns", "s", ""));
	EXPECT_FALSE(zx::ComputeDatabaseOverlayResolve(overlay[{ "ns", "s" }], true, "file", value));
	EXPECT_EQ(value, "");
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "s", 0));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(overlay[{ "ns", "s" }], true, "file", value));
	EXPECT_EQ(value, "0");
	zx::DatabaseOverlayEntry untouched;
	EXPECT_FALSE(zx::ComputeDatabaseOverlayResolve(untouched, false, "", value));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayResolve(untouched, true, "file", value));
	EXPECT_EQ(value, "file");
}

TEST(DatabaseQueueOverlay, IncrementsWrapAsSixtyFourBitIntegers)
{
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "k", std::numeric_limits<int64_t>::max()));
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "k", 1));
	std::string value;
	zx::ComputeDatabaseOverlayResolve(overlay[{ "ns", "k" }], false, "", value);
	EXPECT_EQ(value, std::to_string(std::numeric_limits<int64_t>::min()));
}

TEST(DatabaseQueue, AcknowledgingDropsAppliedBatchesAndRebuildsTheOverlay)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "1"));
	zx::ComputeDatabaseQueueSeal(queue, 1);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "b", "2"));
	zx::ComputeDatabaseQueueSeal(queue, 2);
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "c", "3"));
	zx::ComputeDatabaseBatcherEnd(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "d", "4"));
	EXPECT_FALSE(zx::ComputeDatabaseQueueAcknowledge(queue, 0));
	EXPECT_TRUE(zx::ComputeDatabaseQueueAcknowledge(queue, 1));
	EXPECT_EQ(queue.overlay.count({ "ns", "a" }), 0u);
	EXPECT_EQ(queue.overlay.count({ "ns", "b" }), 1u);
	EXPECT_EQ(queue.overlay.count({ "ns", "c" }), 1u);
	EXPECT_EQ(queue.overlay.count({ "ns", "d" }), 1u);
	EXPECT_FALSE(zx::ComputeDatabaseQueueAcknowledge(queue, 1));
	EXPECT_EQ(queue.applied, 1u);
}

TEST(DatabaseQueue, RestoredBatchesComeFirstAndKeepTheirNumbers)
{
	zx::DatabaseQueue queue;
	zx::DatabaseBatch kept;
	kept.seq = 7;
	kept.stamp = 900;
	kept.ops = { set("ns", "a", "kept") };
	zx::ComputeDatabaseQueueRestore(queue, { kept }, 8);
	EXPECT_EQ(queue.nextSeq, 8u);
	EXPECT_EQ(queue.overlay[std::make_pair(std::string("ns"), std::string("a"))].base, "kept");
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "new"));
	const std::vector<zx::DatabaseBatch> sealed = zx::ComputeDatabaseQueueSeal(queue, 100);
	EXPECT_EQ(sealed[0].seq, 8u);
	EXPECT_GT(sealed[0].stamp, 900);
	zx::ComputeDatabaseQueueRestore(queue, {}, 2);
	EXPECT_EQ(queue.nextSeq, 9u);
	EXPECT_EQ(queue.overlay[std::make_pair(std::string("ns"), std::string("a"))].base, "new");
}

// [Doruk-Aksoy] The queue with a writer that lags at random must read exactly as the old immediate calls did.
TEST(DatabaseQueue, MergedReadsMatchTheLegacyCallsWhileTheWriterLags)
{
	MemoryDb shared;
	LegacyDb legacy;
	zx::DatabaseStatements reader, writer;
	zx::ComputeDatabaseBind(reader, shared.db);
	zx::ComputeDatabaseBind(writer, shared.db);
	zx::DatabaseQueue queue;
	std::vector<zx::DatabaseBatch> inFlight;
	std::mt19937 random(4321);
	const char *const spaces[] = { "a", "b" };
	const char *const keys[] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
	const char *const values[] = { "", "0", "5", "-3", "12", "abc", "7", "5" };
	double wall = 1000;
	bool modOpen = false;
	for (int step = 0; step < 6000; ++step)
	{
		const char *ns = spaces[random() % 2];
		const char *key = keys[random() % 8];
		const bool descending = random() % 2 == 0;
		switch (random() % 10)
		{
		case 0:
		case 1:
		{
			const char *value = values[random() % 8];
			legacy.saveSet(ns, key, value);
			zx::ComputeDatabaseQueueWrite(queue, set(ns, key, value));
			break;
		}
		case 2:
		{
			const int amount = static_cast<int>(random() % 21) - 10;
			legacy.increment(ns, key, amount);
			zx::ComputeDatabaseQueueWrite(queue, inc(ns, key, amount));
			break;
		}
		case 3:
		{
			// [Doruk-Aksoy] A tic ends, sometimes inside a mod transaction.
			if (random() % 4 == 0)
				modOpen = modOpen ? zx::ComputeDatabaseBatcherEnd(queue.batcher) != zx::DatabaseTxnResult::Ok : zx::ComputeDatabaseBatcherBegin(queue.batcher) == zx::DatabaseTxnResult::Ok;
			wall += 0.028;
			for (zx::DatabaseBatch &batch : zx::ComputeDatabaseQueueSeal(queue, wall))
				inFlight.push_back(batch);
			break;
		}
		case 4:
		{
			// [Doruk-Aksoy] The writer catches up on some of what is in flight.
			const size_t take = inFlight.empty() ? 0 : random() % (inFlight.size() + 1);
			uint64_t applied = 0;
			std::string error;
			for (size_t i = 0; i < take; ++i)
			{
				ASSERT_EQ(zx::ComputeDatabaseApplyBatch(writer, "server", inFlight[i], error), zx::DatabaseApplyResult::Applied) << error;
				applied = inFlight[i].seq;
			}
			inFlight.erase(inFlight.begin(), inFlight.begin() + static_cast<std::ptrdiff_t>(take));
			zx::ComputeDatabaseQueueAcknowledge(queue, applied);
			break;
		}
		case 5:
		{
			std::string value;
			bool found = false;
			ASSERT_TRUE(zx::ComputeDatabaseMergedGet(reader, queue.overlay, ns, key, value, found));
			ASSERT_EQ(value, legacy.get(ns, key)) << "step " << step;
			ASSERT_EQ(found, !value.empty());
			break;
		}
		case 6:
		{
			int rank = 0;
			ASSERT_TRUE(zx::ComputeDatabaseMergedRank(reader, queue.overlay, ns, key, descending, rank));
			ASSERT_EQ(rank, legacy.rank(ns, key, descending)) << "step " << step;
			break;
		}
		case 7:
		case 8:
		{
			const int count = static_cast<int>(random() % 8) - 1, offset = static_cast<int>(random() % 6) - 1;
			zx::DatabaseEntries entries;
			ASSERT_TRUE(zx::ComputeDatabaseMergedSorted(reader, queue.overlay, ns, count, offset, descending, entries));
			ASSERT_EQ(entries, legacy.sorted(ns, count, offset, descending)) << "step " << step << " count " << count << " offset " << offset;
			break;
		}
		default:
		{
			zx::DatabaseEntries entries;
			ASSERT_TRUE(zx::ComputeDatabaseMergedEntries(reader, queue.overlay, ns, entries));
			ASSERT_EQ(entries, legacy.entries(ns)) << "step " << step;
			break;
		}
		}
	}
}

TEST(DatabaseQueue, MergedReadsFailWhenTheFileCannotBeRead)
{
	zx::DatabaseStatements noConnection;
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "delta", 1));
	std::string value;
	bool found = false;
	zx::DatabaseEntries entries;
	int rank = 0;
	EXPECT_FALSE(zx::ComputeDatabaseMergedGet(noConnection, overlay, "ns", "delta", value, found));
	EXPECT_FALSE(zx::ComputeDatabaseMergedEntries(noConnection, overlay, "ns", entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedSorted(noConnection, overlay, "ns", 3, 0, true, entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(noConnection, overlay, "ns", "delta", true, rank));
	// [Doruk-Aksoy] A key the overlay sets needs no read for itself, so the count is what fails.
	zx::ComputeDatabaseOverlayAdd(overlay, set("ns", "based", "4"));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(noConnection, overlay, "ns", "based", true, rank));
}

TEST(DatabaseQueue, MergedSortedAndRankFailOnAFailingKeyRead)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "file", "3");
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, set("ns", "based", "4"));
	zx::ComputeDatabaseOverlayAdd(overlay, inc("ns", "delta", 1));
	poison(statements, zx::DatabaseQuery::Get);
	zx::DatabaseEntries entries;
	int rank = 0;
	EXPECT_FALSE(zx::ComputeDatabaseMergedSorted(statements, overlay, "ns", 3, 0, true, entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(statements, overlay, "ns", "based", true, rank));
}

TEST(DatabaseQueue, MergedRankOfAKeyTheOverlayDeletedIsMissing)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "3");
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, set("ns", "k", ""));
	int rank = 0;
	EXPECT_TRUE(zx::ComputeDatabaseMergedRank(statements, overlay, "ns", "k", true, rank));
	EXPECT_EQ(rank, -1);
}
