#ifndef ZX_DATABASE_QUEUE_COMPUTE_H
#define ZX_DATABASE_QUEUE_COMPUTE_H

#include "features/database/computation/database_compute.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace zx
{

// [Doruk-Aksoy] SQLite's CAST(text AS INTEGER): the longest integer prefix after leading spaces, clamped to 64 bits.
int64_t ComputeDatabaseCastInteger(const std::string &text);

// [Doruk-Aksoy] What SQLite stores for CAST(text AS INTEGER) + delta in a text column, a REAL's text once the sum leaves 64 bits.
std::string ComputeDatabaseAddText(const std::string &text, int64_t delta);

// [Doruk-Aksoy] One ACS write as the queue carries it.
enum class DatabaseOpKind
{
	Set,
	Delete,
	Increment,
};

struct DatabaseOp
{
	DatabaseOpKind kind = DatabaseOpKind::Set;
	std::string ns;
	std::string key;
	std::string value;
	int64_t delta = 0;
};

bool operator==(const DatabaseOp &a, const DatabaseOp &b);

// [Doruk-Aksoy] The writes of one tic or of one mod transaction, applied to the shared file as one transaction.
struct DatabaseBatch
{
	uint64_t seq = 0;
	double stamp = 0;
	std::vector<DatabaseOp> ops;
};

// [Doruk-Aksoy] The op SetDBEntry makes: a set, or a delete for an empty or null value; a null namespace or key is the empty string.
DatabaseOp ComputeDatabaseSetOp(const char *ns, const char *key, const char *value);

DatabaseOp ComputeDatabaseIncrementOp(const char *ns, const char *key, int64_t delta);

// [Doruk-Aksoy] A hybrid logical clock: stamps follow the wall clock but never fall behind a stamp already made or seen.
constexpr double kDatabaseClockStep = 1e-4;
constexpr double kDatabaseClockAheadWarning = 60.0;

struct DatabaseClock
{
	double last = 0;
};

double ComputeDatabaseClockStamp(DatabaseClock &clock, double wallSeconds);

// [Doruk-Aksoy] Takes in a stamp read from the file; true when it is far enough ahead of this machine's clock to warn about.
bool ComputeDatabaseClockObserve(DatabaseClock &clock, double seenStamp, double wallSeconds);

// [Doruk-Aksoy] A mod transaction still open after this many tics (a minute) is ended, so a script that never called End cannot hold writes back forever.
constexpr int kDatabaseModTransactionTics = 35 * 60;

// [Doruk-Aksoy] Splits the game's writes into batches: one per tic, and one per mod transaction however many tics it spans; a batch is numbered at its first write.
struct DatabaseBatcher
{
	std::vector<DatabaseOp> open;
	uint64_t openSeq = 0;
	bool modOpen = false;
	int modTics = 0;
	std::vector<DatabaseBatch> sealed;
};

enum class DatabaseTxnResult
{
	Ok,
	AlreadyOpen,
	NotOpen,
};

DatabaseTxnResult ComputeDatabaseBatcherBegin(DatabaseBatcher &batcher);
DatabaseTxnResult ComputeDatabaseBatcherEnd(DatabaseBatcher &batcher);

// [Doruk-Aksoy] One write this server made that the shared file may not hold yet, with the batch it belongs to.
struct DatabaseOverlayOp
{
	uint64_t seq = 0;
	DatabaseOpKind kind = DatabaseOpKind::Set;
	std::string value;
	int64_t delta = 0;
};

using DatabaseOverlayOps = std::vector<DatabaseOverlayOp>;
using DatabaseOverlay = std::map<std::pair<std::string, std::string>, DatabaseOverlayOps>;

void ComputeDatabaseOverlayAdd(DatabaseOverlay &overlay, uint64_t seq, const DatabaseOp &op);

// [Doruk-Aksoy] True when the key's ops are increments only, so its value depends on what the file holds and on which batches it already holds.
bool ComputeDatabaseOverlayReadsFile(const DatabaseOverlayOps &ops);

// [Doruk-Aksoy] Whether any key of the namespace (or just `key`, when given) reads the file under the overlay.
bool ComputeDatabaseOverlayReadsFile(const DatabaseOverlay &overlay, const std::string &ns, const std::string *key);

// [Doruk-Aksoy] A key's value with the ops the file does not hold yet (batches past `applied`) applied on top of it; false when the key ends up missing.
bool ComputeDatabaseOverlayResolve(const DatabaseOverlayOps &ops, uint64_t applied, bool fileFound, const std::string &fileValue, std::string &value);

// [Doruk-Aksoy] Forgets the ops of a batch the file now holds, along with any of its keys left with nothing pending.
void ComputeDatabaseOverlayTrim(DatabaseOverlay &overlay, const DatabaseBatch &batch, uint64_t applied);

// [Doruk-Aksoy] The game-side half of the queue: batches being built, batches durable but not yet in the shared file, and the overlay over both.
struct DatabaseQueue
{
	DatabaseBatcher batcher;
	DatabaseOverlay overlay;
	std::deque<DatabaseBatch> pending;
	DatabaseClock clock;
	uint64_t nextSeq = 1;
	uint64_t applied = 0;
};

void ComputeDatabaseQueueWrite(DatabaseQueue &queue, const DatabaseOp &op);

// [Doruk-Aksoy] Closes the tic's batch (a mod transaction stays open until its time runs out, which sets `modExpired`), stamps every closed batch, and keeps them pending.
std::vector<DatabaseBatch> ComputeDatabaseQueueSeal(DatabaseQueue &queue, double wallSeconds, bool *modExpired = nullptr);

// [Doruk-Aksoy] Puts batches recovered from an outbox back in front of the queue, numbered as they were.
void ComputeDatabaseQueueRestore(DatabaseQueue &queue, const std::vector<DatabaseBatch> &batches, uint64_t nextSeq);

// [Doruk-Aksoy] Forgets batches the shared file now holds; true when any were dropped.
bool ComputeDatabaseQueueAcknowledge(DatabaseQueue &queue, uint64_t appliedSeq);

// [Doruk-Aksoy] Reads through the overlay at a file snapshot that holds this server's batches through `applied`, so a script sees its own writes exactly once.
bool ComputeDatabaseMergedGet(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, const char *key, std::string &value, bool &found, double *seenStamp = nullptr);
bool ComputeDatabaseMergedEntries(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, DatabaseEntries &entries, double *seenStamp = nullptr);
bool ComputeDatabaseMergedSorted(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double *seenStamp = nullptr);
bool ComputeDatabaseMergedRank(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, const char *key, bool descending, int &rank);

}

#endif
