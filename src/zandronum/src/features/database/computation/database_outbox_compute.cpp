#include "features/database/computation/database_outbox_compute.h"

#include "features/database/computation/database_writer_compute.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <sqlite3.h>

namespace zx
{

namespace
{

bool exec(sqlite3 *db, const char *sql)
{
	return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// [Doruk-Aksoy] A server id no other outbox will have: 128 random bits as hex.
std::string newServerId()
{
	std::random_device device;
	std::mt19937_64 random((static_cast<uint64_t>(device()) << 32) ^ device() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	static const char digits[] = "0123456789abcdef";
	std::string id;
	for (int half = 0; half < 2; ++half)
	{
		uint64_t bits = random();
		for (int i = 0; i < 16; ++i, bits >>= 4)
			id += digits[bits & 15];
	}
	return id;
}

std::string singleText(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *statement = nullptr;
	std::string text;
	if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
		text = ComputeDatabaseColumnText(statement, 0);
	sqlite3_finalize(statement);
	return text;
}

std::vector<DatabaseBatch> readBatches(sqlite3 *db)
{
	std::vector<DatabaseBatch> batches;
	sqlite3_stmt *statement = nullptr;
	sqlite3_prepare_v2(db, "SELECT Seq, Stamp, Kind, Namespace, KeyName, Value, Delta FROM Ops ORDER BY Seq, Idx", -1, &statement, nullptr);
	while (sqlite3_step(statement) == SQLITE_ROW)
	{
		const uint64_t seq = static_cast<uint64_t>(sqlite3_column_int64(statement, 0));
		if (batches.empty() || batches.back().seq != seq)
		{
			batches.emplace_back();
			batches.back().seq = seq;
			batches.back().stamp = sqlite3_column_double(statement, 1);
		}
		DatabaseOp op;
		op.kind = static_cast<DatabaseOpKind>(sqlite3_column_int(statement, 2));
		op.ns = ComputeDatabaseColumnText(statement, 3);
		op.key = ComputeDatabaseColumnText(statement, 4);
		op.value = ComputeDatabaseColumnText(statement, 5);
		op.delta = sqlite3_column_int64(statement, 6);
		batches.back().ops.push_back(std::move(op));
	}
	sqlite3_finalize(statement);
	return batches;
}

}

DatabaseOutbox::~DatabaseOutbox()
{
	ComputeDatabaseOutboxClose(*this);
}

std::string ComputeDatabaseOutboxPath(const std::string &databasePath, int slot)
{
	return databasePath + ".fua-outbox-" + std::to_string(slot);
}

DatabaseOutboxOpen ComputeDatabaseOutboxOpen(DatabaseOutbox &outbox, const std::string &path, bool create, std::vector<DatabaseBatch> &leftover, uint64_t &nextSeq)
{
	ComputeDatabaseOutboxClose(outbox);
	leftover.clear();
	nextSeq = 1;
	// [Doruk-Aksoy] Without the create flag a missing file fails to open, which is how a missing outbox is told apart.
	const int opened = sqlite3_open_v2(path.c_str(), &outbox.db, SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0), nullptr);
	if (opened != SQLITE_OK)
	{
		ComputeDatabaseOutboxClose(outbox);
		return create ? DatabaseOutboxOpen::Failed : DatabaseOutboxOpen::Missing;
	}
	// [Doruk-Aksoy] In exclusive locking mode the first write takes the file's lock until close, so a second process gets Busy, and the lock dies with a crashed one.
	exec(outbox.db, "PRAGMA locking_mode=EXCLUSIVE");
	const int begin = sqlite3_exec(outbox.db, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr);
	if (begin != SQLITE_OK)
	{
		ComputeDatabaseOutboxClose(outbox);
		return (begin & 0xff) == SQLITE_BUSY ? DatabaseOutboxOpen::Busy : DatabaseOutboxOpen::Failed;
	}
	const std::string claim = "INSERT INTO Meta VALUES('server','" + newServerId() + "') ON CONFLICT(Key) DO UPDATE SET Value=Value";
	const bool made = exec(outbox.db, "CREATE TABLE IF NOT EXISTS Meta(Key text PRIMARY KEY, Value text)")
		&& exec(outbox.db, "CREATE TABLE IF NOT EXISTS Ops(Seq integer, Idx integer, Stamp real, Kind integer, Namespace text, KeyName text, Value text, Delta integer, PRIMARY KEY (Seq, Idx))")
		&& exec(outbox.db, claim.c_str()) && exec(outbox.db, "COMMIT")
		&& exec(outbox.db, "PRAGMA journal_mode=WAL") && exec(outbox.db, "PRAGMA synchronous=FULL");
	if (!made)
	{
		ComputeDatabaseOutboxClose(outbox);
		return DatabaseOutboxOpen::Failed;
	}
	outbox.path = path;
	outbox.server = singleText(outbox.db, "SELECT Value FROM Meta WHERE Key='server'");
	const uint64_t stored = std::strtoull(singleText(outbox.db, "SELECT Value FROM Meta WHERE Key='next'").c_str(), nullptr, 10);
	leftover = readBatches(outbox.db);
	nextSeq = std::max<uint64_t>(stored, 1);
	if (!leftover.empty() && leftover.back().seq >= nextSeq)
		nextSeq = leftover.back().seq + 1;
	sqlite3_prepare_v2(outbox.db, "INSERT INTO Ops VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &outbox.insertOp, nullptr);
	sqlite3_prepare_v2(outbox.db, "INSERT INTO Meta VALUES('next',?1) ON CONFLICT(Key) DO UPDATE SET Value=excluded.Value", -1, &outbox.setNext, nullptr);
	sqlite3_prepare_v2(outbox.db, "DELETE FROM Ops WHERE Seq<=?1", -1, &outbox.prune, nullptr);
	return DatabaseOutboxOpen::Opened;
}

int ComputeDatabaseOutboxClaim(DatabaseOutbox &outbox, const std::string &databasePath, int slots, std::vector<DatabaseBatch> &leftover, uint64_t &nextSeq)
{
	for (int slot = 0; slot < slots; ++slot)
		if (ComputeDatabaseOutboxOpen(outbox, ComputeDatabaseOutboxPath(databasePath, slot), true, leftover, nextSeq) == DatabaseOutboxOpen::Opened)
			return slot;
	return -1;
}

bool ComputeDatabaseOutboxAppend(DatabaseOutbox &outbox, const std::vector<DatabaseBatch> &batches, uint64_t nextSeq, uint64_t appliedThrough)
{
	if (outbox.db == nullptr || !exec(outbox.db, "BEGIN"))
		return false;
	bool ok = true;
	for (const DatabaseBatch &batch : batches)
		for (size_t i = 0; ok && i < batch.ops.size(); ++i)
		{
			const DatabaseOp &op = batch.ops[i];
			sqlite3_bind_int64(outbox.insertOp, 1, static_cast<int64_t>(batch.seq));
			sqlite3_bind_int64(outbox.insertOp, 2, static_cast<int64_t>(i));
			sqlite3_bind_double(outbox.insertOp, 3, batch.stamp);
			sqlite3_bind_int(outbox.insertOp, 4, static_cast<int>(op.kind));
			sqlite3_bind_text(outbox.insertOp, 5, op.ns.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(outbox.insertOp, 6, op.key.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(outbox.insertOp, 7, op.value.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int64(outbox.insertOp, 8, op.delta);
			ok = sqlite3_step(outbox.insertOp) == SQLITE_DONE;
			sqlite3_reset(outbox.insertOp);
		}
	sqlite3_bind_int64(outbox.setNext, 1, static_cast<int64_t>(nextSeq));
	ok = ok && sqlite3_step(outbox.setNext) == SQLITE_DONE;
	sqlite3_reset(outbox.setNext);
	sqlite3_bind_int64(outbox.prune, 1, static_cast<int64_t>(appliedThrough));
	ok = ok && sqlite3_step(outbox.prune) == SQLITE_DONE;
	sqlite3_reset(outbox.prune);
	if (ok && exec(outbox.db, "COMMIT"))
		return true;
	exec(outbox.db, "ROLLBACK");
	return false;
}

void ComputeDatabaseOutboxClose(DatabaseOutbox &outbox)
{
	for (sqlite3_stmt **statement : { &outbox.insertOp, &outbox.setNext, &outbox.prune })
	{
		sqlite3_finalize(*statement);
		*statement = nullptr;
	}
	sqlite3_close(outbox.db);
	outbox.db = nullptr;
	outbox.path.clear();
	outbox.server.clear();
}

int ComputeDatabaseOutboxReplayOrphans(DatabaseStatements &shared, const std::string &databasePath, int slots, int ownSlot, std::string &error)
{
	int replayed = 0;
	for (int slot = 0; slot < slots; ++slot)
	{
		if (slot == ownSlot)
			continue;
		DatabaseOutbox orphan;
		std::vector<DatabaseBatch> leftover;
		uint64_t nextSeq = 0;
		if (ComputeDatabaseOutboxOpen(orphan, ComputeDatabaseOutboxPath(databasePath, slot), false, leftover, nextSeq) != DatabaseOutboxOpen::Opened)
			continue;
		uint64_t done = 0;
		for (const DatabaseBatch &batch : leftover)
		{
			const DatabaseApplyResult result = ComputeDatabaseApplyBatch(shared, orphan.server, batch, error);
			if (result != DatabaseApplyResult::Applied && result != DatabaseApplyResult::AlreadyApplied)
				break;
			replayed += result == DatabaseApplyResult::Applied ? 1 : 0;
			done = batch.seq;
		}
		if (done > 0)
			ComputeDatabaseOutboxAppend(orphan, {}, nextSeq, done);
	}
	return replayed;
}

}
