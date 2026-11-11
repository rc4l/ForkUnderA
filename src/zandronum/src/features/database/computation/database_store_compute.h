#ifndef ZX_DATABASE_STORE_COMPUTE_H
#define ZX_DATABASE_STORE_COMPUTE_H

#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_outbox_compute.h"
#include "features/database/computation/database_queue_compute.h"
#include "features/database/computation/database_writer_compute.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace zx
{

constexpr int kDatabaseOutboxSlots = 64;
constexpr double kDatabaseMarkerHorizonSeconds = 30.0 * 24 * 60 * 60;

// [Doruk-Aksoy] The queued write path for a database file: writes go to this server's outbox at tic end and reach the shared file from the writer thread.
struct DatabaseStore
{
	std::mutex mutex;
	DatabaseQueue queue;
	DatabaseOutbox outbox;
	DatabaseWriter writer;
	std::string databasePath;
	// [Doruk-Aksoy] The stem the outboxes are named from; the orphan scan also looks under databasePath as given, for outboxes an older spelling left.
	std::string outboxStem;
	int slot = -1;
	bool open = false;
	bool clockWarned = false;
	std::string reportedError;
	// [Doruk-Aksoy] Batches the outbox could not take yet, written again at every tic end until it does.
	std::vector<DatabaseBatch> unsaved;
	// [Doruk-Aksoy] How long the writer may make no progress on what it holds before the console hears of it.
	double stallWarnSeconds = 10;
	std::chrono::steady_clock::time_point stallSince;
	uint64_t stallApplied = 0;
	bool stallWarned = false;
	// [Doruk-Aksoy] How often a full file is named again while batches wait on it, since a single line scrolls away on a busy server.
	double fullWarnSeconds = 60;
	std::chrono::steady_clock::time_point fullWarnedAt;
	bool fullWarned = false;
	std::mutex messagesMutex;
	std::vector<std::string> messages;
	// [Doruk-Aksoy] Unreadable outboxes already named, so the minute-by-minute scan names each once.
	std::set<std::string> reportedUnreadable;
};

// [Doruk-Aksoy] Claims an outbox, restores what it held and starts the writer; false (with a message) when the queue cannot run, so the caller writes directly.
bool ComputeDatabaseStoreOpen(DatabaseStore &store, const std::string &databasePath, const std::string &outboxStem, const std::vector<std::string> &writerPragmas, DatabaseStatements &reader, double wallSeconds);

void ComputeDatabaseStoreWrite(DatabaseStore &store, const DatabaseOp &op);
DatabaseTxnResult ComputeDatabaseStoreBegin(DatabaseStore &store);
DatabaseTxnResult ComputeDatabaseStoreEnd(DatabaseStore &store);

// [Doruk-Aksoy] Ends a mod transaction its level's scripts can no longer end; true when one was open.
bool ComputeDatabaseStoreEndOrphanedTransaction(DatabaseStore &store);

// [Doruk-Aksoy] The tic's end: forgets what the shared file took, makes the tic's batches durable in the outbox, and hands them to the writer.
void ComputeDatabaseStoreTicEnd(DatabaseStore &store, double wallSeconds);

// [Doruk-Aksoy] Reads through the overlay; one that depends on the file reads it in a single snapshot with the batches of this server it holds.
bool ComputeDatabaseStoreGet(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, std::string &value, bool &found, double wallSeconds);
bool ComputeDatabaseStoreEntries(DatabaseStore &store, DatabaseStatements &reader, const char *ns, DatabaseEntries &entries, double wallSeconds);
bool ComputeDatabaseStoreSorted(DatabaseStore &store, DatabaseStatements &reader, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double wallSeconds);
bool ComputeDatabaseStoreRank(DatabaseStore &store, DatabaseStatements &reader, const char *ns, const char *key, bool descending, int &rank);

// [Doruk-Aksoy] Makes the last tic's writes durable, gives the writer up to drainMs, and closes; whatever is left waits in the outbox for the next start.
void ComputeDatabaseStoreClose(DatabaseStore &store, double wallSeconds, int drainMs);

std::vector<std::string> ComputeDatabaseStoreTakeMessages(DatabaseStore &store);

// [Doruk-Aksoy] One line per fact for fua_db_status.
std::vector<std::string> ComputeDatabaseStoreStatus(DatabaseStore &store);

}

#endif
