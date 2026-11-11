#ifndef ZX_DATABASE_OUTBOX_COMPUTE_H
#define ZX_DATABASE_OUTBOX_COMPUTE_H

#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_queue_compute.h"

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace zx
{

// [Doruk-Aksoy] One server's private, locked file of batches not yet known to be in the shared file; it outlives an engine crash and a power cut.
struct DatabaseOutbox
{
	sqlite3 *db = nullptr;
	sqlite3_stmt *insertOp = nullptr;
	sqlite3_stmt *setNext = nullptr;
	sqlite3_stmt *prune = nullptr;
	std::string path;
	std::string server;

	DatabaseOutbox() = default;
	DatabaseOutbox(const DatabaseOutbox &) = delete;
	DatabaseOutbox &operator=(const DatabaseOutbox &) = delete;
	~DatabaseOutbox();
};

enum class DatabaseOutboxOpen
{
	Opened,
	Missing,
	Busy,
	// [Doruk-Aksoy] The file is there but is not an outbox SQLite can open, so whatever it holds is not being applied.
	Unreadable,
	Failed,
};

// [Doruk-Aksoy] The outbox of slot `slot` beside the shared file, `databasePath` being the stem ComputeDatabaseOutboxStem gives.
std::string ComputeDatabaseOutboxPath(const std::string &databasePath, int slot);

// [Doruk-Aksoy] The database's resolved path in one spelling (forward slashes, no repeated separators or . and .. segments), never folding case, since two files a case-sensitive volume tells apart must never share outboxes.
std::string ComputeDatabaseOutboxStem(const std::string &resolvedPath);

// [Doruk-Aksoy] Opens and locks an outbox for this process, returning the batches it still holds; Busy when a live process holds it.
DatabaseOutboxOpen ComputeDatabaseOutboxOpen(DatabaseOutbox &outbox, const std::string &path, bool create, std::vector<DatabaseBatch> &leftover, uint64_t &nextSeq);

// [Doruk-Aksoy] Takes the first slot no live process holds, listing the unreadable ones passed over; -1 when every slot is taken or none can be made.
int ComputeDatabaseOutboxClaim(DatabaseOutbox &outbox, const std::string &databasePath, int slots, std::vector<DatabaseBatch> &leftover, uint64_t &nextSeq, std::vector<std::string> *unreadable = nullptr);

// [Doruk-Aksoy] Makes batches durable in one transaction with a full flush, and forgets those through `appliedThrough`.
bool ComputeDatabaseOutboxAppend(DatabaseOutbox &outbox, const std::vector<DatabaseBatch> &batches, uint64_t nextSeq, uint64_t appliedThrough);

void ComputeDatabaseOutboxClose(DatabaseOutbox &outbox);

// [Doruk-Aksoy] Applies what other slots' outboxes still hold, for servers that stopped and never came back, listing the unreadable ones; returns the batches applied.
int ComputeDatabaseOutboxReplayOrphans(DatabaseStatements &shared, const std::string &databasePath, int slots, int ownSlot, std::string &error, std::vector<std::string> *unreadable = nullptr);

}

#endif
