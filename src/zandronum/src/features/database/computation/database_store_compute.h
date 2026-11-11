#ifndef ZX_DATABASE_STORE_COMPUTE_H
#define ZX_DATABASE_STORE_COMPUTE_H

#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_outbox_compute.h"
#include "features/database/computation/database_queue_compute.h"
#include "features/database/computation/database_writer_compute.h"

#include <cstdint>
#include <mutex>
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
	int slot = -1;
	bool open = false;
	bool clockWarned = false;
	std::string reportedError;
	std::mutex messagesMutex;
	std::vector<std::string> messages;
};

// [Doruk-Aksoy] Claims an outbox, restores what it held and starts the writer; false (with a message) when the queue cannot run, so the caller writes directly.
bool ComputeDatabaseStoreOpen(DatabaseStore &store, const std::string &databasePath, const std::vector<std::string> &writerPragmas, DatabaseStatements &reader, double wallSeconds);

void ComputeDatabaseStoreWrite(DatabaseStore &store, const DatabaseOp &op);
DatabaseTxnResult ComputeDatabaseStoreBegin(DatabaseStore &store);
DatabaseTxnResult ComputeDatabaseStoreEnd(DatabaseStore &store);

// [Doruk-Aksoy] The tic's end: forgets what the shared file took, makes the tic's batches durable in the outbox, and hands them to the writer.
void ComputeDatabaseStoreTicEnd(DatabaseStore &store, double wallSeconds);

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
