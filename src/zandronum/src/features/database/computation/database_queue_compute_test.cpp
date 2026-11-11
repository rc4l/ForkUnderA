#include "features/database/computation/database_queue_compute.h"
#include "features/database/computation/database_test_support.h"
#include "features/database/computation/database_writer_compute.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
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

// [Doruk-Aksoy] What the engine's increment stores, run on a real text column.
std::string sqliteAdd(sqlite3 *db, const std::string &text, int64_t delta)
{
	exec(db, "CREATE TABLE IF NOT EXISTS adds(v text)");
	exec(db, "DELETE FROM adds");
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, "INSERT INTO adds VALUES(?1)", -1, &statement, nullptr);
	sqlite3_bind_text(statement, 1, text.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
	sqlite3_prepare_v2(db, "UPDATE adds SET v=CAST(v AS INTEGER)+?1", -1, &statement, nullptr);
	sqlite3_bind_int64(statement, 1, delta);
	sqlite3_step(statement);
	sqlite3_finalize(statement);
	sqlite3_prepare_v2(db, "SELECT v FROM adds", -1, &statement, nullptr);
	sqlite3_step(statement);
	const std::string value = zx::ComputeDatabaseColumnText(statement, 0);
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

zx::DatabaseOverlayOps ops(std::initializer_list<std::pair<uint64_t, zx::DatabaseOp>> list)
{
	zx::DatabaseOverlay overlay;
	for (const auto &item : list)
		zx::ComputeDatabaseOverlayAdd(overlay, item.first, item.second);
	return overlay.empty() ? zx::DatabaseOverlayOps() : overlay.begin()->second;
}

std::string resolved(const zx::DatabaseOverlayOps &list, uint64_t applied, bool fileFound, const std::string &fileValue)
{
	std::string value;
	return zx::ComputeDatabaseOverlayResolve(list, applied, fileFound, fileValue, value) ? value : "<missing>";
}

const std::string kMax = std::to_string(std::numeric_limits<int64_t>::max());
const std::string kMin = std::to_string(std::numeric_limits<int64_t>::min());

}

TEST(DatabaseQueueCast, MatchesSqliteOnHandPickedText)
{
	MemoryDb memory;
	for (const char *text : { "", "0", "42", "-42", "+7", "  12", "\t\n\v\f\r9", "12abc", "1.9", "1e3", "-", "+", "abc", " - 5", "007",
			"9223372036854775807", "9223372036854775808", "99999999999999999999", "-9223372036854775808", "-9223372036854775809", "-99999999999999999999",
			"9.22337203685478e+18", "1.0e+20", "-0", "0x10", "\xC3\xA9" })
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

TEST(DatabaseQueueAdd, MatchesSqliteIncludingOverflowIntoAReal)
{
	MemoryDb memory;
	const std::vector<std::string> bases = { "", "0", "5", "-5", "abc", "1.9", kMax, kMin, "9223372036854775806", "9.22337203685478e+18", "-9223372036854775807" };
	const std::vector<int64_t> deltas = { 0, 1, -1, 2, -2, 1000, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min() };
	for (const std::string &base : bases)
		for (int64_t delta : deltas)
			EXPECT_EQ(zx::ComputeDatabaseAddText(base, delta), sqliteAdd(memory.db, base, delta)) << "'" << base << "' + " << delta;
}

TEST(DatabaseQueueOps, SetOfEmptyOrNullIsADeleteAndNullNamesAreEmpty)
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
	EXPECT_EQ(sealed[0].seq, 2u);
	EXPECT_EQ(sealed[1].ops, (std::vector<zx::DatabaseOp>{ set("ns", "d", "4") }));
	EXPECT_EQ(sealed[1].seq, 3u);
	EXPECT_GT(sealed[1].stamp, sealed[0].stamp);
	EXPECT_EQ(queue.pending.size(), 3u);
	EXPECT_TRUE(zx::ComputeDatabaseQueueSeal(queue, 12).empty());
}

TEST(DatabaseQueueBatcher, AnEmptyModTransactionMakesNoBatchAndUsesNoNumber)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseBatcherEnd(queue.batcher);
	EXPECT_TRUE(zx::ComputeDatabaseQueueSeal(queue, 1).empty());
	EXPECT_EQ(queue.nextSeq, 1u);
}

TEST(DatabaseQueueBatcher, AModTransactionLeftOpenIsClosedAfterAMinuteOfTics)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "k", "v"));
	bool expired = true;
	for (int tic = 1; tic < zx::kDatabaseModTransactionTics; ++tic)
	{
		ASSERT_TRUE(zx::ComputeDatabaseQueueSeal(queue, tic, &expired).empty());
		ASSERT_FALSE(expired);
	}
	const std::vector<zx::DatabaseBatch> sealed = zx::ComputeDatabaseQueueSeal(queue, 9999, &expired);
	EXPECT_TRUE(expired);
	ASSERT_EQ(sealed.size(), 1u);
	EXPECT_EQ(sealed[0].ops.size(), 1u);
	EXPECT_EQ(zx::ComputeDatabaseBatcherEnd(queue.batcher), zx::DatabaseTxnResult::NotOpen);
	// [Doruk-Aksoy] A new transaction starts its own minute.
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseQueueSeal(queue, 10000, &expired);
	EXPECT_FALSE(expired);
}

TEST(DatabaseQueueOverlay, ASetOrDeleteDecidesTheKeyWhateverTheFileHolds)
{
	const zx::DatabaseOverlayOps setThenIncrements = ops({ { 1, inc("ns", "k", 7) }, { 2, set("ns", "k", "10") }, { 3, inc("ns", "k", 1) }, { 4, inc("ns", "k", 2) } });
	EXPECT_FALSE(zx::ComputeDatabaseOverlayReadsFile(setThenIncrements));
	for (uint64_t applied = 0; applied <= 4; ++applied)
		EXPECT_EQ(resolved(setThenIncrements, applied, true, "ignored"), "13") << applied;
	const zx::DatabaseOverlayOps deleted = ops({ { 1, set("ns", "k", "v") }, { 2, set("ns", "k", "") } });
	EXPECT_EQ(resolved(deleted, 0, true, "file"), "<missing>");
	const zx::DatabaseOverlayOps deletedThenCounted = ops({ { 1, set("ns", "k", "") }, { 2, inc("ns", "k", 0) } });
	EXPECT_EQ(resolved(deletedThenCounted, 0, true, "file"), "0");
}

TEST(DatabaseQueueOverlay, IncrementsAloneLandOnTheFileOnlyWhereItLacksThem)
{
	const zx::DatabaseOverlayOps increments = ops({ { 3, inc("ns", "k", 1) }, { 4, inc("ns", "k", 10) }, { 5, inc("ns", "k", 100) } });
	EXPECT_TRUE(zx::ComputeDatabaseOverlayReadsFile(increments));
	EXPECT_EQ(resolved(increments, 2, true, "1000"), "1111");
	EXPECT_EQ(resolved(increments, 4, true, "1011"), "1111");
	EXPECT_EQ(resolved(increments, 5, true, "1111"), "1111");
	EXPECT_EQ(resolved(increments, 2, false, ""), "111");
	EXPECT_EQ(resolved(increments, 5, false, ""), "<missing>");
	EXPECT_EQ(resolved({}, 0, true, "file"), "file");
}

TEST(DatabaseQueueOverlay, IncrementsOverflowStepByStepAsSqliteDoes)
{
	// [Doruk-Aksoy] Once the sum overflows SQLite stores a REAL, whose text casts back to its leading digits, so each step must be taken alone.
	MemoryDb memory;
	const zx::DatabaseOverlayOps steps = ops({ { 1, set("ns", "k", kMax.c_str()) }, { 2, inc("ns", "k", 1) }, { 3, inc("ns", "k", 1) } });
	EXPECT_EQ(resolved(steps, 0, false, ""), sqliteAdd(memory.db, sqliteAdd(memory.db, kMax, 1), 1));
}

TEST(DatabaseQueueOverlay, NamespaceQueriesSeeOnlyTheirKeys)
{
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, 1, set("a", "set", "v"));
	zx::ComputeDatabaseOverlayAdd(overlay, 1, inc("b", "count", 1));
	const std::string set = "set", count = "count", other = "other";
	EXPECT_FALSE(zx::ComputeDatabaseOverlayReadsFile(overlay, "a", nullptr));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayReadsFile(overlay, "b", nullptr));
	EXPECT_FALSE(zx::ComputeDatabaseOverlayReadsFile(overlay, "c", nullptr));
	EXPECT_FALSE(zx::ComputeDatabaseOverlayReadsFile(overlay, "a", &set));
	EXPECT_TRUE(zx::ComputeDatabaseOverlayReadsFile(overlay, "b", &count));
	EXPECT_FALSE(zx::ComputeDatabaseOverlayReadsFile(overlay, "b", &other));
}

TEST(DatabaseQueue, AcknowledgingTrimsOnlyWhatTheFileHolds)
{
	zx::DatabaseQueue queue;
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "1"));
	zx::ComputeDatabaseQueueWrite(queue, inc("ns", "shared", 1));
	zx::ComputeDatabaseQueueSeal(queue, 1);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "b", "2"));
	zx::ComputeDatabaseQueueWrite(queue, inc("ns", "shared", 1));
	zx::ComputeDatabaseQueueSeal(queue, 2);
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "c", "3"));
	zx::ComputeDatabaseBatcherEnd(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "d", "4"));
	EXPECT_FALSE(zx::ComputeDatabaseQueueAcknowledge(queue, 0));
	EXPECT_TRUE(zx::ComputeDatabaseQueueAcknowledge(queue, 1));
	EXPECT_EQ(queue.overlay.count({ "ns", "a" }), 0u);
	ASSERT_EQ(queue.overlay.count({ "ns", "shared" }), 1u);
	EXPECT_EQ(queue.overlay[std::make_pair(std::string("ns"), std::string("shared"))].size(), 1u);
	EXPECT_EQ(queue.overlay.count({ "ns", "b" }), 1u);
	EXPECT_EQ(queue.overlay.count({ "ns", "c" }), 1u);
	EXPECT_EQ(queue.overlay.count({ "ns", "d" }), 1u);
	EXPECT_FALSE(zx::ComputeDatabaseQueueAcknowledge(queue, 1));
	EXPECT_EQ(queue.applied, 1u);
	EXPECT_TRUE(zx::ComputeDatabaseQueueAcknowledge(queue, 2));
	EXPECT_EQ(queue.overlay.count({ "ns", "shared" }), 0u);
}

TEST(DatabaseQueue, TrimmingSkipsKeysAlreadyGone)
{
	zx::DatabaseOverlay overlay;
	zx::DatabaseBatch batch;
	batch.seq = 1;
	batch.ops = { set("ns", "k", "v"), set("ns", "k", "w") };
	zx::ComputeDatabaseOverlayAdd(overlay, 1, batch.ops[0]);
	zx::ComputeDatabaseOverlayAdd(overlay, 1, batch.ops[1]);
	zx::ComputeDatabaseOverlayTrim(overlay, batch, 1);
	EXPECT_TRUE(overlay.empty());
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
	EXPECT_EQ(resolved(queue.overlay[std::make_pair(std::string("ns"), std::string("a"))], 0, false, ""), "kept");
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "new"));
	const std::vector<zx::DatabaseBatch> sealed = zx::ComputeDatabaseQueueSeal(queue, 100);
	EXPECT_EQ(sealed[0].seq, 8u);
	EXPECT_GT(sealed[0].stamp, 900);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "open"));
	zx::ComputeDatabaseBatcherBegin(queue.batcher);
	zx::ComputeDatabaseQueueWrite(queue, set("ns", "a", "in a transaction"));
	zx::ComputeDatabaseQueueRestore(queue, {}, 2);
	EXPECT_EQ(queue.nextSeq, 11u);
	EXPECT_EQ(resolved(queue.overlay[std::make_pair(std::string("ns"), std::string("a"))], 0, false, ""), "in a transaction");
}

// [Doruk-Aksoy] The queue with a writer that lags at random, and acknowledgements that lag behind the writer, must read exactly as the old immediate calls did.
TEST(DatabaseQueue, MergedReadsMatchTheLegacyCallsWhileTheWriterAndAcknowledgementsLag)
{
	MemoryDb shared;
	LegacyDb legacy;
	zx::DatabaseStatements reader, writer;
	zx::ComputeDatabaseBind(reader, shared.db);
	zx::ComputeDatabaseBind(writer, shared.db);
	zx::DatabaseQueue queue;
	std::vector<zx::DatabaseBatch> inFlight;
	uint64_t fileHolds = 0;
	std::mt19937 random(4321);
	const char *const spaces[] = { "a", "b", "" };
	const char *const keys[] = { "k0", "k1", "k2", "k3", "", "K0", "\xC3\xA9", "k 7" };
	const char *const values[] = { "", "0", "5", "-3", "12", "abc", "7", "5", "1.9", "1e3", " 4", "9223372036854775807", "-9223372036854775808", "00", "\xC3\xA9t\xC3\xA9", "x y" };
	double wall = 1000;
	bool modOpen = false;
	for (int step = 0; step < 12000; ++step)
	{
		const char *ns = spaces[random() % 3];
		const char *key = keys[random() % 8];
		const bool descending = random() % 2 == 0;
		switch (random() % 11)
		{
		case 0:
		case 1:
		{
			const char *value = values[random() % 16];
			legacy.saveSet(ns, key, value);
			zx::ComputeDatabaseQueueWrite(queue, set(ns, key, value));
			break;
		}
		case 2:
		{
			const int64_t amount = random() % 8 == 0 ? (random() % 2 ? std::numeric_limits<int>::max() : std::numeric_limits<int>::min()) : static_cast<int64_t>(random() % 21) - 10;
			legacy.increment(ns, key, static_cast<int>(amount));
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
			// [Doruk-Aksoy] The writer catches up on some of what is in flight, without telling the queue yet.
			const size_t take = inFlight.empty() ? 0 : random() % (inFlight.size() + 1);
			std::string error;
			for (size_t i = 0; i < take; ++i)
			{
				ASSERT_EQ(zx::ComputeDatabaseApplyBatch(writer, "server", inFlight[i], error), zx::DatabaseApplyResult::Applied) << error;
				fileHolds = inFlight[i].seq;
			}
			inFlight.erase(inFlight.begin(), inFlight.begin() + static_cast<std::ptrdiff_t>(take));
			break;
		}
		case 5:
			// [Doruk-Aksoy] The tic end hears of some of it, never more than the file holds.
			zx::ComputeDatabaseQueueAcknowledge(queue, random() % 2 ? fileHolds : fileHolds / 2);
			break;
		case 6:
		{
			uint64_t applied = 0;
			ASSERT_TRUE(zx::ComputeDatabaseAppliedSeq(reader, "server", applied));
			std::string value;
			bool found = false;
			ASSERT_TRUE(zx::ComputeDatabaseMergedGet(reader, queue.overlay, applied, ns, key, value, found));
			ASSERT_EQ(value, legacy.get(ns, key)) << "step " << step;
			ASSERT_EQ(found, !value.empty());
			int rank = 0;
			ASSERT_TRUE(zx::ComputeDatabaseMergedRank(reader, queue.overlay, applied, ns, key, descending, rank));
			ASSERT_EQ(rank, legacy.rank(ns, key, descending)) << "step " << step;
			break;
		}
		case 7:
		case 8:
		{
			uint64_t applied = 0;
			ASSERT_TRUE(zx::ComputeDatabaseAppliedSeq(reader, "server", applied));
			const int count = static_cast<int>(random() % 8) - 1, offset = static_cast<int>(random() % 6) - 1;
			zx::DatabaseEntries entries;
			ASSERT_TRUE(zx::ComputeDatabaseMergedSorted(reader, queue.overlay, applied, ns, count, offset, descending, entries));
			ASSERT_EQ(entries, legacy.sorted(ns, count, offset, descending)) << "step " << step << " count " << count << " offset " << offset;
			break;
		}
		default:
		{
			uint64_t applied = 0;
			ASSERT_TRUE(zx::ComputeDatabaseAppliedSeq(reader, "server", applied));
			zx::DatabaseEntries entries;
			ASSERT_TRUE(zx::ComputeDatabaseMergedEntries(reader, queue.overlay, applied, ns, entries));
			ASSERT_EQ(entries, legacy.entries(ns)) << "step " << step;
			break;
		}
		}
	}
}

TEST(DatabaseQueue, MergedReadsTakeNullNamesAsEmpty)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, 1, set(nullptr, nullptr, "v"));
	std::string value;
	bool found = false;
	EXPECT_TRUE(zx::ComputeDatabaseMergedGet(statements, overlay, 0, nullptr, nullptr, value, found));
	EXPECT_EQ(value, "v");
	zx::DatabaseEntries entries;
	EXPECT_TRUE(zx::ComputeDatabaseMergedEntries(statements, overlay, 0, nullptr, entries));
	EXPECT_EQ(entries.size(), 1u);
	EXPECT_TRUE(zx::ComputeDatabaseMergedSorted(statements, overlay, 0, nullptr, 3, 0, true, entries));
	EXPECT_EQ(entries.size(), 1u);
	int rank = 0;
	EXPECT_TRUE(zx::ComputeDatabaseMergedRank(statements, overlay, 0, nullptr, nullptr, true, rank));
	EXPECT_EQ(rank, 1);
	EXPECT_TRUE(zx::ComputeDatabaseMergedGet(statements, {}, 0, nullptr, nullptr, value, found));
	EXPECT_TRUE(zx::ComputeDatabaseMergedRank(statements, {}, 0, nullptr, nullptr, true, rank));
	EXPECT_EQ(rank, -1);
}

TEST(DatabaseQueue, MergedReadsFailWhenTheFileCannotBeRead)
{
	zx::DatabaseStatements noConnection;
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, 1, inc("ns", "delta", 1));
	std::string value;
	bool found = false;
	zx::DatabaseEntries entries;
	int rank = 0;
	EXPECT_FALSE(zx::ComputeDatabaseMergedGet(noConnection, overlay, 0, "ns", "delta", value, found));
	EXPECT_FALSE(zx::ComputeDatabaseMergedEntries(noConnection, overlay, 0, "ns", entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedSorted(noConnection, overlay, 0, "ns", 3, 0, true, entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(noConnection, overlay, 0, "ns", "delta", true, rank));
	// [Doruk-Aksoy] A key the overlay sets needs no read for itself, so the count is what fails.
	zx::ComputeDatabaseOverlayAdd(overlay, 1, set("ns", "based", "4"));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(noConnection, overlay, 0, "ns", "based", true, rank));
}

TEST(DatabaseQueue, MergedSortedAndRankFailOnAFailingKeyRead)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "file", "3");
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, 1, set("ns", "based", "4"));
	zx::ComputeDatabaseOverlayAdd(overlay, 1, inc("ns", "delta", 1));
	poison(statements, zx::DatabaseQuery::Get);
	zx::DatabaseEntries entries;
	int rank = 0;
	EXPECT_FALSE(zx::ComputeDatabaseMergedSorted(statements, overlay, 0, "ns", 3, 0, true, entries));
	EXPECT_FALSE(zx::ComputeDatabaseMergedRank(statements, overlay, 0, "ns", "based", true, rank));
}

TEST(DatabaseQueue, MergedRankOfAKeyTheOverlayDeletedIsMissing)
{
	MemoryDb memory;
	zx::DatabaseStatements statements;
	zx::ComputeDatabaseBind(statements, memory.db);
	zx::ComputeDatabaseSaveSet(statements, "ns", "k", "3");
	zx::DatabaseOverlay overlay;
	zx::ComputeDatabaseOverlayAdd(overlay, 1, set("ns", "k", ""));
	int rank = 0;
	EXPECT_TRUE(zx::ComputeDatabaseMergedRank(statements, overlay, 0, "ns", "k", true, rank));
	EXPECT_EQ(rank, -1);
}

// [Doruk-Aksoy] Three servers with skewed clocks write one file through lagging, interleaved writers: every increment counts once, and sets and deletes end on the newest.
TEST(DatabaseQueue, ServersWithSkewedClocksConvergeOnOneFile)
{
	MemoryDb shared;
	zx::DatabaseStatements writer, reader;
	zx::ComputeDatabaseBind(writer, shared.db);
	zx::ComputeDatabaseBind(reader, shared.db);
	struct Server
	{
		std::string id;
		double skew;
		zx::DatabaseQueue queue;
		std::vector<zx::DatabaseBatch> inFlight;
	};
	std::vector<Server> servers(3);
	for (size_t i = 0; i < servers.size(); ++i)
	{
		servers[i].id = "server" + std::to_string(i);
		servers[i].skew = static_cast<double>(i) * 7.5 - 5;
	}
	std::mt19937 random(77);
	std::map<std::string, int64_t> counted;
	struct Newest
	{
		double stamp = -1;
		std::string value;
	};
	std::map<std::string, Newest> newest;
	double wall = 2000;
	const char *const counters[] = { "kills", "gold" };
	const char *const fields[] = { "name", "class", "slot" };
	const char *const values[] = { "", "a", "b", "c" };
	for (int step = 0; step < 4000; ++step)
	{
		Server &server = servers[random() % servers.size()];
		wall += 0.01;
		switch (random() % 4)
		{
		case 0:
		{
			const char *key = counters[random() % 2];
			const int64_t amount = static_cast<int64_t>(random() % 9) - 4;
			zx::ComputeDatabaseQueueWrite(server.queue, inc("ns", key, amount));
			counted[key] += amount;
			break;
		}
		case 1:
		{
			// [Doruk-Aksoy] Sets and deletes go one per batch, so each has its batch's stamp to be judged by.
			const char *key = fields[random() % 3];
			const char *value = values[random() % 4];
			zx::ComputeDatabaseQueueWrite(server.queue, set("ns", key, value));
			for (zx::DatabaseBatch &batch : zx::ComputeDatabaseQueueSeal(server.queue, wall + server.skew))
			{
				Newest &record = newest[key];
				if (batch.stamp > record.stamp)
					record = { batch.stamp, value };
				server.inFlight.push_back(batch);
			}
			break;
		}
		case 2:
			for (zx::DatabaseBatch &batch : zx::ComputeDatabaseQueueSeal(server.queue, wall + server.skew))
				server.inFlight.push_back(batch);
			break;
		default:
		{
			std::string error;
			if (!server.inFlight.empty())
			{
				ASSERT_EQ(zx::ComputeDatabaseApplyBatch(writer, server.id, server.inFlight.front(), error), zx::DatabaseApplyResult::Applied) << error;
				zx::ComputeDatabaseQueueAcknowledge(server.queue, server.inFlight.front().seq);
				server.inFlight.erase(server.inFlight.begin());
			}
			break;
		}
		}
	}
	for (Server &server : servers)
	{
		for (zx::DatabaseBatch &batch : zx::ComputeDatabaseQueueSeal(server.queue, wall + server.skew))
			server.inFlight.push_back(batch);
		std::string error;
		for (const zx::DatabaseBatch &batch : server.inFlight)
			ASSERT_EQ(zx::ComputeDatabaseApplyBatch(writer, server.id, batch, error), zx::DatabaseApplyResult::Applied) << error;
	}
	std::string value;
	bool found = false;
	for (const auto &counter : counted)
	{
		zx::ComputeDatabaseGet(reader, "ns", counter.first.c_str(), value, found);
		EXPECT_EQ(value, std::to_string(counter.second)) << counter.first;
	}
	for (const auto &field : newest)
	{
		zx::ComputeDatabaseGet(reader, "ns", field.first.c_str(), value, found);
		EXPECT_EQ(value, field.second.value) << field.first;
	}
}
