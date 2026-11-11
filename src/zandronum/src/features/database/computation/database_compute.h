#ifndef ZX_DATABASE_COMPUTE_H
#define ZX_DATABASE_COMPUTE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace zx
{

// [Doruk-Aksoy] The statements the ACS database functions and the write queue run, each prepared once per connection on first use.
enum class DatabaseQuery
{
	Get,
	Upsert,
	Delete,
	Increment,
	RankAscending,
	RankDescending,
	SortAscending,
	SortDescending,
	Entries,
	CountAbove,
	CountBelow,
	MaxStamp,
	MarkerStamp,
	SetIfNewer,
	DeleteIfOlder,
	MarkDeleted,
	ClearMarker,
	IncrementStamped,
	AppliedSeq,
	SetApplied,
	PruneMarkers,
	WritingBegin,
	WritingEnd,
	Count,
};

// [Doruk-Aksoy] One connection's prepared statements; destroying it finalizes them, which must happen before the connection closes.
struct DatabaseStatements
{
	sqlite3 *db = nullptr;
	sqlite3_stmt *prepared[static_cast<int>(DatabaseQuery::Count)] = {};

	DatabaseStatements() = default;
	DatabaseStatements(const DatabaseStatements &) = delete;
	DatabaseStatements &operator=(const DatabaseStatements &) = delete;
	~DatabaseStatements();
};

using DatabaseEntries = std::vector<std::pair<std::string, std::string>>;

// [Doruk-Aksoy] Resets and unbinds a statement when the call using it returns, so no cached statement keeps a read open.
struct DatabaseStatementUse
{
	sqlite3_stmt *statement;
	explicit DatabaseStatementUse(sqlite3_stmt *s) : statement(s) {}
	DatabaseStatementUse(const DatabaseStatementUse &) = delete;
	DatabaseStatementUse &operator=(const DatabaseStatementUse &) = delete;
	~DatabaseStatementUse();
};

// [Doruk-Aksoy] The SQL of one statement, over the table Zandronum has always used and the queue's own two.
const char *ComputeDatabaseQuerySql(DatabaseQuery query);

// [Doruk-Aksoy] The statement for a query on this connection, prepared on first use; null when it cannot be prepared.
sqlite3_stmt *ComputeDatabaseStatement(DatabaseStatements &statements, DatabaseQuery query);

// [Doruk-Aksoy] A column as text, empty for NULL.
std::string ComputeDatabaseColumnText(sqlite3_stmt *statement, int column);

// [Doruk-Aksoy] What a database gets beside the Zandronum table: the value-order index, delete markers and each server's last applied batch.
std::vector<std::string> ComputeDatabaseSchemaSql();

// [Doruk-Aksoy] The pragmas a database file gets on open and on a cvar change: WAL or the rollback journal, then the sync level; none in memory.
std::vector<std::string> ComputeDatabasePragmas(bool memory, bool wal, int synchronous);

// [Doruk-Aksoy] Points the cache at a connection, finalizing statements prepared for an earlier one.
void ComputeDatabaseBind(DatabaseStatements &statements, sqlite3 *db);

// [Doruk-Aksoy] Reads a value; a missing key gives true with found false; `seenStamp`, when given, rises to the row's timestamp.
bool ComputeDatabaseGet(DatabaseStatements &statements, const char *ns, const char *key, std::string &value, bool &found, double *seenStamp = nullptr);

// [Doruk-Aksoy] Stores a value, where an empty or null value deletes the key as SetDBEntry always has.
bool ComputeDatabaseSaveSet(DatabaseStatements &statements, const char *ns, const char *key, const char *value);

// [Doruk-Aksoy] Adds to a key's integer value, creating the key with the increment when it is missing.
bool ComputeDatabaseIncrement(DatabaseStatements &statements, const char *ns, const char *key, int increment);

// [Doruk-Aksoy] One plus the number of entries ranked above the key, or -1 when the key is missing.
bool ComputeDatabaseRank(DatabaseStatements &statements, const char *ns, const char *key, bool descending, int &rank);

// [Doruk-Aksoy] Up to `count` entries from `offset` in value order, ties in key order, as SQL's LIMIT and OFFSET read negatives.
bool ComputeDatabaseSorted(DatabaseStatements &statements, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double *seenStamp = nullptr);

// [Doruk-Aksoy] Every entry of a namespace in key order.
bool ComputeDatabaseEntries(DatabaseStatements &statements, const char *ns, DatabaseEntries &entries, double *seenStamp = nullptr);

// [Doruk-Aksoy] How many entries of a namespace have an integer value above (or below) `value`.
bool ComputeDatabaseCountBeyond(DatabaseStatements &statements, const char *ns, int64_t value, bool above, int64_t &count);

// [Doruk-Aksoy] The newest timestamp in the table or its delete markers, 0 for an empty database.
bool ComputeDatabaseMaxStamp(DatabaseStatements &statements, double &stamp);

// [Doruk-Aksoy] How full the file is for this connection: its pages, the most it may have (max_page_count) and the bytes in a page.
bool ComputeDatabasePageUse(DatabaseStatements &statements, int64_t &pages, int64_t &maxPages, int64_t &pageSize);

// [Doruk-Aksoy] The last batch of `server` the file holds, 0 when it holds none.
bool ComputeDatabaseAppliedSeq(DatabaseStatements &statements, const std::string &server, uint64_t &seq);

// [Doruk-Aksoy] A thread with its own connection that checkpoints the write-ahead log, so the game thread never waits on that disk flush.
struct DatabaseCheckpointer
{
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	bool stopping = false;
	sqlite3 *db = nullptr;
	std::atomic<int> checkpoints{0};
	// [Doruk-Aksoy] Log frames the last checkpoint copied into the main file, -1 when the file is not in WAL mode or the checkpoint failed.
	std::atomic<int> lastCopiedFrames{0};

	DatabaseCheckpointer() = default;
	DatabaseCheckpointer(const DatabaseCheckpointer &) = delete;
	DatabaseCheckpointer &operator=(const DatabaseCheckpointer &) = delete;
	~DatabaseCheckpointer();
};

// [Doruk-Aksoy] Opens `path` again and checkpoints it every `intervalMs`; false when the file cannot be opened.
bool ComputeDatabaseCheckpointerStart(DatabaseCheckpointer &checkpointer, const char *path, int intervalMs);

// [Doruk-Aksoy] Runs a last checkpoint, joins the thread and closes its connection; safe to call when not running.
void ComputeDatabaseCheckpointerStop(DatabaseCheckpointer &checkpointer);

}

#endif
