#include "features/database/computation/database_store_compute.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace zx
{

namespace
{

void say(DatabaseStore &store, const std::string &message)
{
	std::lock_guard<std::mutex> lock(store.messagesMutex);
	store.messages.push_back(message);
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

// [Doruk-Aksoy] Seals the tic's batches, writes them to the outbox and queues them for the writer; the caller holds the store's lock.
void flushTic(DatabaseStore &store, double wallSeconds)
{
	ComputeDatabaseQueueAcknowledge(store.queue, store.writer.applied.load());
	const std::vector<DatabaseBatch> batches = ComputeDatabaseQueueSeal(store.queue, wallSeconds);
	if (batches.empty())
		return;
	if (!ComputeDatabaseOutboxAppend(store.outbox, batches, store.queue.nextSeq, store.queue.applied))
		say(store, "Could not write this tic's database writes to the outbox " + store.outbox.path + "; they are only in memory until the shared file takes them.");
	ComputeDatabaseWriterPush(store.writer, batches);
}

}

bool ComputeDatabaseStoreOpen(DatabaseStore &store, const std::string &databasePath, const std::vector<std::string> &writerPragmas, DatabaseStatements &reader, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	std::vector<DatabaseBatch> leftover;
	uint64_t nextSeq = 1;
	store.slot = ComputeDatabaseOutboxClaim(store.outbox, databasePath, kDatabaseOutboxSlots, leftover, nextSeq);
	if (store.slot < 0)
	{
		say(store, "No outbox could be made beside " + databasePath + "; database writes go straight to the file.");
		return false;
	}
	store.databasePath = databasePath;
	store.queue = DatabaseQueue();
	double newest = 0;
	ComputeDatabaseMaxStamp(reader, newest);
	observe(store, newest, wallSeconds);
	ComputeDatabaseQueueRestore(store.queue, leftover, nextSeq);
	const int slot = store.slot;
	store.writer.maintenance = [&store, databasePath, slot](DatabaseStatements &shared) {
		std::string error;
		const int replayed = ComputeDatabaseOutboxReplayOrphans(shared, databasePath, kDatabaseOutboxSlots, slot, error);
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

void ComputeDatabaseStoreTicEnd(DatabaseStore &store, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	if (!store.open)
		return;
	flushTic(store, wallSeconds);
	const std::string error = ComputeDatabaseWriterLastError(store.writer);
	if (!error.empty() && error != store.reportedError)
		say(store, "The database writer keeps a batch and retries it: " + error);
	store.reportedError = error;
}

bool ComputeDatabaseStoreGet(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, std::string &value, bool &found, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	double seen = 0;
	const bool ok = ComputeDatabaseMergedGet(reader, store.queue.overlay, ns, key, value, found, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreEntries(DatabaseStore &store, DatabaseStatements &reader, const char *ns, DatabaseEntries &entries, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	double seen = 0;
	const bool ok = ComputeDatabaseMergedEntries(reader, store.queue.overlay, ns, entries, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreSorted(DatabaseStore &store, DatabaseStatements &reader, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double wallSeconds)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	double seen = 0;
	const bool ok = ComputeDatabaseMergedSorted(reader, store.queue.overlay, ns, count, offset, descending, entries, &seen);
	observe(store, seen, wallSeconds);
	return ok;
}

bool ComputeDatabaseStoreRank(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, bool descending, int &rank)
{
	std::lock_guard<std::mutex> lock(store.mutex);
	return ComputeDatabaseMergedRank(reader, store.queue.overlay, ns, key, descending, rank);
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
	if (ComputeDatabaseWriterStop(store.writer, drainMs))
		ComputeDatabaseOutboxAppend(store.outbox, {}, store.queue.nextSeq, store.writer.applied.load());
	ComputeDatabaseOutboxClose(store.outbox);
	store.queue = DatabaseQueue();
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
		"Batches waiting for the shared file: " + std::to_string(store.queue.pending.size()) + " (writer holds " + std::to_string(ComputeDatabaseWriterQueued(store.writer)) + ")",
		"Last applied batch: " + std::to_string(store.writer.applied.load()) + ", next: " + std::to_string(store.queue.nextSeq),
		"Lock retries: " + std::to_string(store.writer.busyRetries.load()) + ", failed attempts: " + std::to_string(store.writer.failures.load()),
		"Last writer error: " + (store.reportedError.empty() ? std::string("none") : store.reportedError),
	};
}

}
