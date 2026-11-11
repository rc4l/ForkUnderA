#include "features/database/computation/database_store_compute.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <sqlite3.h>
#include <string>
#include <utility>

namespace zx
{

namespace
{

void say(DatabaseStore &store, const std::string &message)
{
	std::lock_guard<std::mutex> lock(store.messagesMutex);
	store.messages.push_back(message);
}

// [Doruk-Aksoy] Names each outbox that cannot be opened once, whether the claim or a later scan finds it.
void sayUnreadable(DatabaseStore &store, const std::vector<std::string> &paths)
{
	for (const std::string &path : paths)
	{
		bool fresh = false;
		{
			std::lock_guard<std::mutex> lock(store.messagesMutex);
			fresh = store.reportedUnreadable.insert(path).second;
		}
		if (fresh)
			say(store, "The outbox " + path + " cannot be opened as a database and was passed over; any writes it holds are not being applied.");
	}
}

std::string text(const char *s)
{
	return s != nullptr ? s : "";
}

// [Doruk-Aksoy] Takes in the newest stamp a read saw, warning once when this machine's clock is well behind the file's.
void observe(DatabaseStore &store, double seen, double wallSeconds)
{
	if (ComputeDatabaseClockObserve(store.queue.clock, seen, wallSeconds) && !store.clockWarned)
	{
		store.clockWarned = true;
		say(store, "This machine's clock is more than a minute behind the database's newest write; sync it (NTP) so this server's writes are not ordered before older ones.");
	}
}

double nowSeconds()
{
	return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// [Doruk-Aksoy] One read transaction for a read that depends on the file, so the rows and the batches of this server they hold come from one moment.
struct ReadSnapshot
{
	sqlite3 *db = nullptr;
	uint64_t applied = 0;
	bool ok = true;
	ReadSnapshot(DatabaseStatements &reader, const std::string &server, bool needed)
	{
		if (!needed)
			return;
		if (sqlite3_exec(reader.db, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
		{
			ok = false;
			return;
		}
		db = reader.db;
		ok = ComputeDatabaseAppliedSeq(reader, server, applied);
	}
	~ReadSnapshot()
	{
		if (db != nullptr)
			sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
	}
};

// [Doruk-Aksoy] A namespace read with any of this server's pending keys in it reads several rows and values, all of which must come from one snapshot.
bool namespaceTouched(const DatabaseOverlay &overlay, const char *ns)
{
	const auto first = overlay.lower_bound(std::make_pair(text(ns), std::string()));
	return first != overlay.end() && first->first.first == text(ns);
}

// [Doruk-Aksoy] Seals the tic's batches, makes them durable (with any the outbox missed before), and queues the new ones for the writer; the caller holds the store's lock.
void flushTic(DatabaseStore &store, double wallSeconds)
{
	ComputeDatabaseQueueAcknowledge(store.queue, store.writer.applied.load());
	bool expired = false;
	const std::vector<DatabaseBatch> batches = ComputeDatabaseQueueSeal(store.queue, wallSeconds, &expired);
	if (expired)
		say(store, "A mod's database transaction stayed open for a minute of tics without ending; its writes are saved and the transaction is closed.");
	const uint64_t applied = store.queue.applied;
	store.unsaved.erase(std::remove_if(store.unsaved.begin(), store.unsaved.end(), [applied](const DatabaseBatch &batch) { return batch.seq <= applied; }), store.unsaved.end());
	std::vector<DatabaseBatch> toSave = store.unsaved;
	toSave.insert(toSave.end(), batches.begin(), batches.end());
	if (!toSave.empty())
	{
		const bool retrying = !store.unsaved.empty();
		if (ComputeDatabaseOutboxAppend(store.outbox, toSave, store.queue.nextSeq, applied))
		{
			if (retrying)
				say(store, "The outbox " + store.outbox.path + " took the database writes it missed.");
			store.unsaved.clear();
		}
		else
		{
			if (!retrying)
				say(store, "Could not write database writes to the outbox " + store.outbox.path + "; they are in memory only until it takes them, retried every tic.");
			store.unsaved = toSave;
		}
	}
	ComputeDatabaseWriterPush(store.writer, batches);
}

// [Doruk-Aksoy] Tells the console once when the writer holds batches but has applied none for a while, which is another connection holding the file.
void watchStall(DatabaseStore &store)
{
	const auto now = std::chrono::steady_clock::now();
	const uint64_t applied = store.writer.applied.load();
	if (ComputeDatabaseWriterQueued(store.writer) == 0 || applied != store.stallApplied)
	{
		store.stallApplied = applied;
		store.stallSince = now;
		store.stallWarned = false;
		return;
	}
	const double waited = std::chrono::duration<double>(now - store.stallSince).count();
	if (!store.stallWarned && waited >= store.stallWarnSeconds)
	{
		store.stallWarned = true;
		say(store, "The database writer has waited " + std::to_string(static_cast<int>(waited)) + " s (" + std::to_string(store.writer.busyRetries.load()) +
			" lock retries) to apply its next batch; another connection may be holding a transaction open on the file. The batches are kept and retried.");
	}
}

}

bool ComputeDatabaseStoreOpen(DatabaseStore &store, const std::string &databasePath, const std::vector<std::string> &writerPragmas, DatabaseStatements &reader, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	std::vector<DatabaseBatch> leftover;
	uint64_t nextSeq = 1;
	std::vector<std::string> unreadable;
	{
		std::lock_guard<std::mutex> named(store.messagesMutex);
		store.reportedUnreadable.clear();
	}
	store.slot = ComputeDatabaseOutboxClaim(store.outbox, databasePath, kDatabaseOutboxSlots, leftover, nextSeq, &unreadable);
	sayUnreadable(store, unreadable);
	if (store.slot < 0)
	{
		say(store, "No outbox could be made beside " + databasePath + "; database writes go straight to the file.");
		return false;
	}
	store.databasePath = databasePath;
	store.queue = DatabaseQueue();
	store.unsaved.clear();
	double newest = 0;
	ComputeDatabaseMaxStamp(reader, newest);
	observe(store, newest, wallSeconds);
	// [Doruk-Aksoy] A server that died after its writer applied a batch but before pruning it left that batch behind; the file says which, so those are not shown or sent again.
	uint64_t alreadyApplied = 0;
	ComputeDatabaseAppliedSeq(reader, store.outbox.server, alreadyApplied);
	leftover.erase(std::remove_if(leftover.begin(), leftover.end(), [alreadyApplied](const DatabaseBatch &batch) { return batch.seq <= alreadyApplied; }), leftover.end());
	ComputeDatabaseQueueRestore(store.queue, leftover, nextSeq);
	store.queue.applied = alreadyApplied;
	const int slot = store.slot;
	store.writer.maintenance = [&store, databasePath, slot](DatabaseStatements &shared) {
		std::string error;
		std::vector<std::string> unreadableNow;
		const int replayed = ComputeDatabaseOutboxReplayOrphans(shared, databasePath, kDatabaseOutboxSlots, slot, error, &unreadableNow);
		sayUnreadable(store, unreadableNow);
		if (replayed > 0)
			say(store, "Applied " + std::to_string(replayed) + " database batch(es) a stopped server left in its outbox.");
		ComputeDatabasePruneMarkers(shared, nowSeconds() - kDatabaseMarkerHorizonSeconds);
	};
	if (!ComputeDatabaseWriterStart(store.writer, databasePath.c_str(), store.outbox.server, writerPragmas, 100))
	{
		ComputeDatabaseOutboxClose(store.outbox);
		store.slot = -1;
		say(store, "Could not open " + databasePath + " for the database writer; database writes go straight to the file.");
		return false;
	}
	ComputeDatabaseWriterPush(store.writer, leftover);
	if (!leftover.empty())
		say(store, "Resuming " + std::to_string(leftover.size()) + " database batch(es) this server's outbox kept from its last run.");
	store.stallSince = std::chrono::steady_clock::now();
	store.stallApplied = 0;
	store.stallWarned = false;
	store.open = true;
	return true;
}

void ComputeDatabaseStoreWrite(DatabaseStore &store, const DatabaseOp &op)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	ComputeDatabaseQueueWrite(store.queue, op);
}

DatabaseTxnResult ComputeDatabaseStoreBegin(DatabaseStore &store)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	return ComputeDatabaseBatcherBegin(store.queue.batcher);
}

DatabaseTxnResult ComputeDatabaseStoreEnd(DatabaseStore &store)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	return ComputeDatabaseBatcherEnd(store.queue.batcher);
}

bool ComputeDatabaseStoreEndOrphanedTransaction(DatabaseStore &store)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	return ComputeDatabaseBatcherEnd(store.queue.batcher) == DatabaseTxnResult::Ok;
}

void ComputeDatabaseStoreTicEnd(DatabaseStore &store, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.open)
		return;
	flushTic(store, wallSeconds);
	watchStall(store);
	const std::string error = ComputeDatabaseWriterLastError(store.writer);
	if (!error.empty() && error != store.reportedError)
		say(store, "The database writer keeps a batch and retries it: " + error);
	store.reportedError = error;
}

bool ComputeDatabaseStoreGet(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, std::string &value, bool &found, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	const std::string name = text(key);
	ReadSnapshot snapshot(reader, store.outbox.server, ComputeDatabaseOverlayReadsFile(store.queue.overlay, text(ns), &name));
	double seen = 0;
	const bool ok = snapshot.ok && ComputeDatabaseMergedGet(reader, store.queue.overlay, snapshot.applied, ns, key, value, found, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreEntries(DatabaseStore &store, DatabaseStatements &reader, const char *ns, DatabaseEntries &entries, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	ReadSnapshot snapshot(reader, store.outbox.server, namespaceTouched(store.queue.overlay, ns));
	double seen = 0;
	const bool ok = snapshot.ok && ComputeDatabaseMergedEntries(reader, store.queue.overlay, snapshot.applied, ns, entries, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreSorted(DatabaseStore &store, DatabaseStatements &reader, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	ReadSnapshot snapshot(reader, store.outbox.server, namespaceTouched(store.queue.overlay, ns));
	double seen = 0;
	const bool ok = snapshot.ok && ComputeDatabaseMergedSorted(reader, store.queue.overlay, snapshot.applied, ns, count, offset, descending, entries, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreRank(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, bool descending, int &rank)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	ReadSnapshot snapshot(reader, store.outbox.server, namespaceTouched(store.queue.overlay, ns));
	return snapshot.ok && ComputeDatabaseMergedRank(reader, store.queue.overlay, snapshot.applied, ns, key, descending, rank);
}

void ComputeDatabaseStoreClose(DatabaseStore &store, double wallSeconds, int drainMs)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.open)
		return;
	// [Doruk-Aksoy] A mod transaction still open is dropped, as closing SQLite in the middle of one always did.
	store.queue.batcher.modOpen = false;
	store.queue.batcher.open.clear();
	flushTic(store, wallSeconds);
	if (ComputeDatabaseWriterStop(store.writer, drainMs) && store.unsaved.empty())
		ComputeDatabaseOutboxAppend(store.outbox, {}, store.queue.nextSeq, store.writer.applied.load());
	ComputeDatabaseOutboxClose(store.outbox);
	store.queue = DatabaseQueue();
	store.unsaved.clear();
	store.writer.maintenance = nullptr;
	store.slot = -1;
	store.open = false;
}

std::vector<std::string> ComputeDatabaseStoreTakeMessages(DatabaseStore &store)
{
	std::lock_guard<std::mutex> lock(store.messagesMutex);
	std::vector<std::string> messages;
	messages.swap(store.messages);
	return messages;
}

std::vector<std::string> ComputeDatabaseStoreStatus(DatabaseStore &store)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.open)
		return { "Database writes: direct (no queue)." };
	return {
		"Database writes: queued through outbox slot " + std::to_string(store.slot) + " (" + store.outbox.path + ")",
		"Server id: " + store.outbox.server,
		"Batches waiting for the shared file: " + std::to_string(store.queue.pending.size()) + " (writer holds " + std::to_string(ComputeDatabaseWriterQueued(store.writer)) +
			", not yet in the outbox " + std::to_string(store.unsaved.size()) + ")",
		"Last applied batch: " + std::to_string(store.writer.applied.load()) + ", next: " + std::to_string(store.queue.nextSeq),
		"Lock retries: " + std::to_string(store.writer.busyRetries.load()) + ", failed attempts: " + std::to_string(store.writer.failures.load()),
		"Last writer error: " + (store.reportedError.empty() ? std::string("none") : store.reportedError),
	};
}

}
