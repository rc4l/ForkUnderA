#include "features/database/computation/database_compute.h"

#include <chrono>
#include <cstddef>
#include <sqlite3.h>

namespace zx
{

namespace
{

// [Doruk-Aksoy] Binds the namespace and key every keyed statement starts with.
void bindKey(sqlite3_stmt *statement, const char *ns, const char *key)
{
	sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
	sqlite3_bind_text(statement, 2, key, -1, SQLITE_STATIC);
}

bool runToDone(sqlite3_stmt *statement)
{
	return sqlite3_step(statement) == SQLITE_DONE;
}

// [Doruk-Aksoy] Raises `seen` to a row's timestamp column when the caller asked for it.
void noteStamp(double *seen, sqlite3_stmt *statement, int column)
{
	if (seen != nullptr && sqlite3_column_type(statement, column) != SQLITE_NULL)
	{
		const double stamp = sqlite3_column_double(statement, column);
		if (stamp > *seen)
			*seen = stamp;
	}
}

bool collectRows(sqlite3_stmt *statement, DatabaseEntries &entries, double *seenStamp)
{
	entries.clear();
	int result;
	while ((result = sqlite3_step(statement)) == SQLITE_ROW)
	{
		entries.emplace_back(ComputeDatabaseColumnText(statement, 0), ComputeDatabaseColumnText(statement, 1));
		noteStamp(seenStamp, statement, 2);
	}
	return result == SQLITE_DONE;
}

#define ZX_DB_TABLE "Zandronum"
#define ZX_DB_NOW "(SELECT (julianday('now') - 2440587.5)*86400.0)"
#define ZX_DB_RANK(op) "SELECT (SELECT COUNT(*) FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND CAST(Value AS INTEGER)" op "CAST(v.Value AS INTEGER))+1 FROM " ZX_DB_TABLE " v WHERE v.Namespace=?1 AND v.KeyName=?2"
#define ZX_DB_SORT(dir) "SELECT KeyName, Value, CAST(Timestamp AS REAL) FROM " ZX_DB_TABLE " WHERE Namespace=?1 ORDER BY CAST(Value AS INTEGER) " dir ", KeyName ASC LIMIT ?2 OFFSET ?3"
#define ZX_DB_OLDER(stamp) "(" ZX_DB_TABLE ".Timestamp IS NULL OR CAST(" ZX_DB_TABLE ".Timestamp AS REAL)<=" stamp ")"

const char *const kQuerySql[] = {
	"SELECT Value, CAST(Timestamp AS REAL) FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND KeyName=?2",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3," ZX_DB_NOW ") ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=excluded.Value,Timestamp=excluded.Timestamp",
	"DELETE FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND KeyName=?2",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3," ZX_DB_NOW ") ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=CAST(Value AS INTEGER)+?3,Timestamp=excluded.Timestamp",
	ZX_DB_RANK("<"),
	ZX_DB_RANK(">"),
	ZX_DB_SORT("ASC"),
	ZX_DB_SORT("DESC"),
	"SELECT KeyName, Value, CAST(Timestamp AS REAL) FROM " ZX_DB_TABLE " WHERE Namespace=?1 ORDER BY KeyName",
	"SELECT COUNT(*) FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND CAST(Value AS INTEGER)>?2",
	"SELECT COUNT(*) FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND CAST(Value AS INTEGER)<?2",
	"SELECT MAX(m) FROM (SELECT MAX(CAST(Timestamp AS REAL)) AS m FROM " ZX_DB_TABLE " UNION ALL SELECT MAX(Timestamp) FROM fua_Deleted)",
	"SELECT Timestamp FROM fua_Deleted WHERE Namespace=?1 AND KeyName=?2",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3,?4) ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=excluded.Value,Timestamp=excluded.Timestamp WHERE " ZX_DB_OLDER("CAST(excluded.Timestamp AS REAL)"),
	"DELETE FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND KeyName=?2 AND " ZX_DB_OLDER("?3"),
	"INSERT INTO fua_Deleted VALUES(?1,?2,?3) ON CONFLICT(Namespace,KeyName) DO UPDATE SET Timestamp=MAX(Timestamp,excluded.Timestamp)",
	"DELETE FROM fua_Deleted WHERE Namespace=?1 AND KeyName=?2 AND Timestamp<=?3",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3,?4) ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=CAST(Value AS INTEGER)+?3,Timestamp=MAX(COALESCE(CAST(" ZX_DB_TABLE ".Timestamp AS REAL),0),CAST(excluded.Timestamp AS REAL))",
	"SELECT Seq FROM fua_Applied WHERE Server=?1",
	"INSERT INTO fua_Applied VALUES(?1,?2) ON CONFLICT(Server) DO UPDATE SET Seq=MAX(Seq,excluded.Seq)",
	"DELETE FROM fua_Deleted WHERE Timestamp<?1",
	"INSERT INTO fua_Writing VALUES(1)",
	"DELETE FROM fua_Writing",
};
static_assert(sizeof(kQuerySql) / sizeof(kQuerySql[0]) == static_cast<size_t>(DatabaseQuery::Count), "one SQL string per DatabaseQuery");

}

DatabaseStatementUse::~DatabaseStatementUse()
{
	if (statement != nullptr)
	{
		sqlite3_reset(statement);
		sqlite3_clear_bindings(statement);
	}
}

DatabaseStatements::~DatabaseStatements()
{
	ComputeDatabaseBind(*this, nullptr);
}

const char *ComputeDatabaseQuerySql(DatabaseQuery query)
{
	return kQuerySql[static_cast<int>(query)];
}

sqlite3_stmt *ComputeDatabaseStatement(DatabaseStatements &statements, DatabaseQuery query)
{
	sqlite3_stmt *&slot = statements.prepared[static_cast<int>(query)];
	if (slot == nullptr && sqlite3_prepare_v2(statements.db, ComputeDatabaseQuerySql(query), -1, &slot, nullptr) != SQLITE_OK)
	{
		sqlite3_finalize(slot);
		slot = nullptr;
	}
	return slot;
}

std::string ComputeDatabaseColumnText(sqlite3_stmt *statement, int column)
{
	const unsigned char *text = sqlite3_column_text(statement, column);
	return text != nullptr ? reinterpret_cast<const char *>(text) : "";
}

std::vector<std::string> ComputeDatabaseSchemaSql()
{
	return {
		"CREATE INDEX IF NOT EXISTS fua_ValueOrder ON " ZX_DB_TABLE "(Namespace, CAST(Value AS INTEGER), KeyName)",
		"CREATE TABLE IF NOT EXISTS fua_Deleted(Namespace text, KeyName text, Timestamp real, PRIMARY KEY (Namespace, KeyName))",
		"CREATE TABLE IF NOT EXISTS fua_Applied(Server text PRIMARY KEY, Seq integer)",
		// [Doruk-Aksoy] Holds a row only inside the queue writer's own transaction, which deletes it before committing, so no other connection ever sees one.
		"CREATE TABLE IF NOT EXISTS fua_Writing(Id integer)",
		// [Doruk-Aksoy] Any other connection's delete, a stock server's included, leaves a marker stamped now; old SQL only, since every engine sharing the file must parse it.
		"CREATE TRIGGER IF NOT EXISTS fua_MarkStockDelete AFTER DELETE ON " ZX_DB_TABLE " WHEN NOT EXISTS (SELECT 1 FROM fua_Writing) BEGIN "
		"INSERT OR REPLACE INTO fua_Deleted (Namespace, KeyName, Timestamp) VALUES (old.Namespace, old.KeyName, "
		"MAX((julianday('now') - 2440587.5)*86400.0, COALESCE((SELECT Timestamp FROM fua_Deleted WHERE Namespace=old.Namespace AND KeyName=old.KeyName), 0))); END",
	};
}

std::vector<std::string> ComputeDatabasePragmas(bool memory, bool wal, int synchronous)
{
	std::vector<std::string> pragmas;
	if (memory)
		return pragmas;
	pragmas.push_back(wal ? "PRAGMA journal_mode=WAL" : "PRAGMA journal_mode=DELETE");
	const int level = synchronous < 0 ? 0 : synchronous > 3 ? 3 : synchronous;
	pragmas.push_back("PRAGMA synchronous=" + std::to_string(level));
	return pragmas;
}

void ComputeDatabaseBind(DatabaseStatements &statements, sqlite3 *db)
{
	for (sqlite3_stmt *&slot : statements.prepared)
	{
		sqlite3_finalize(slot);
		slot = nullptr;
	}
	statements.db = db;
}

bool ComputeDatabaseGet(DatabaseStatements &statements, const char *ns, const char *key, std::string &value, bool &found, double *seenStamp)
{
	value.clear();
	found = false;
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::Get));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW)
	{
		value = ComputeDatabaseColumnText(use.statement, 0);
		found = true;
		noteStamp(seenStamp, use.statement, 1);
	}
	return result == SQLITE_ROW || result == SQLITE_DONE;
}

bool ComputeDatabaseSaveSet(DatabaseStatements &statements, const char *ns, const char *key, const char *value)
{
	const bool erase = value == nullptr || *value == '\0';
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, erase ? DatabaseQuery::Delete : DatabaseQuery::Upsert));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	if (!erase)
		sqlite3_bind_text(use.statement, 3, value, -1, SQLITE_STATIC);
	return runToDone(use.statement);
}

bool ComputeDatabaseIncrement(DatabaseStatements &statements, const char *ns, const char *key, int increment)
{
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::Increment));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	sqlite3_bind_int(use.statement, 3, increment);
	return runToDone(use.statement);
}

bool ComputeDatabaseRank(DatabaseStatements &statements, const char *ns, const char *key, bool descending, int &rank)
{
	rank = -1;
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, descending ? DatabaseQuery::RankDescending : DatabaseQuery::RankAscending));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW)
		rank = sqlite3_column_int(use.statement, 0);
	return result == SQLITE_ROW || result == SQLITE_DONE;
}

bool ComputeDatabaseSorted(DatabaseStatements &statements, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double *seenStamp)
{
	entries.clear();
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, descending ? DatabaseQuery::SortDescending : DatabaseQuery::SortAscending));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, ns, -1, SQLITE_STATIC);
	sqlite3_bind_int64(use.statement, 2, count);
	sqlite3_bind_int64(use.statement, 3, offset);
	return collectRows(use.statement, entries, seenStamp);
}

bool ComputeDatabaseEntries(DatabaseStatements &statements, const char *ns, DatabaseEntries &entries, double *seenStamp)
{
	entries.clear();
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::Entries));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, ns, -1, SQLITE_STATIC);
	return collectRows(use.statement, entries, seenStamp);
}

bool ComputeDatabaseCountBeyond(DatabaseStatements &statements, const char *ns, int64_t value, bool above, int64_t &count)
{
	count = 0;
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, above ? DatabaseQuery::CountAbove : DatabaseQuery::CountBelow));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, ns, -1, SQLITE_STATIC);
	sqlite3_bind_int64(use.statement, 2, value);
	if (sqlite3_step(use.statement) != SQLITE_ROW)
		return false;
	count = sqlite3_column_int64(use.statement, 0);
	return true;
}

bool ComputeDatabaseMaxStamp(DatabaseStatements &statements, double &stamp)
{
	stamp = 0;
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::MaxStamp));
	if (use.statement == nullptr || sqlite3_step(use.statement) != SQLITE_ROW)
		return false;
	noteStamp(&stamp, use.statement, 0);
	return true;
}

bool ComputeDatabasePageUse(DatabaseStatements &statements, int64_t &pages, int64_t &maxPages, int64_t &pageSize)
{
	int64_t *const outputs[] = { &pages, &maxPages, &pageSize };
	const char *const pragmas[] = { "PRAGMA page_count", "PRAGMA max_page_count", "PRAGMA page_size" };
	bool ok = true;
	for (int i = 0; i < 3; ++i)
	{
		sqlite3_stmt *statement = nullptr;
		*outputs[i] = 0;
		if (sqlite3_prepare_v2(statements.db, pragmas[i], -1, &statement, nullptr) == SQLITE_OK && sqlite3_step(statement) == SQLITE_ROW)
			*outputs[i] = sqlite3_column_int64(statement, 0);
		else
			ok = false;
		sqlite3_finalize(statement);
	}
	return ok;
}

bool ComputeDatabaseAppliedSeq(DatabaseStatements &statements, const std::string &server, uint64_t &seq)
{
	seq = 0;
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::AppliedSeq));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, server.c_str(), -1, SQLITE_STATIC);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW)
		seq = static_cast<uint64_t>(sqlite3_column_int64(use.statement, 0));
	return result == SQLITE_ROW || result == SQLITE_DONE;
}

DatabaseCheckpointer::~DatabaseCheckpointer()
{
	ComputeDatabaseCheckpointerStop(*this);
}

bool ComputeDatabaseCheckpointerStart(DatabaseCheckpointer &checkpointer, const char *path, int intervalMs)
{
	ComputeDatabaseCheckpointerStop(checkpointer);
	if (sqlite3_open_v2(path, &checkpointer.db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
	{
		sqlite3_close(checkpointer.db);
		checkpointer.db = nullptr;
		return false;
	}
	checkpointer.stopping = false;
	const std::chrono::milliseconds interval(intervalMs > 0 ? intervalMs : 1);
	checkpointer.thread = std::thread([&checkpointer, interval]() {
		bool stop = false;
		while (!stop)
		{
			{
				std::unique_lock<std::mutex> lock(checkpointer.mutex);
				checkpointer.wake.wait_for(lock, interval, [&checkpointer]() { return checkpointer.stopping; });
				stop = checkpointer.stopping;
			}
			// [Doruk-Aksoy] A connection learns the file is in WAL mode only by reading it, and checkpoints nothing until then.
			sqlite3_exec(checkpointer.db, "PRAGMA schema_version", nullptr, nullptr, nullptr);
			int logFrames = 0, copiedFrames = 0;
			const int result = sqlite3_wal_checkpoint_v2(checkpointer.db, nullptr, SQLITE_CHECKPOINT_PASSIVE, &logFrames, &copiedFrames);
			checkpointer.lastCopiedFrames = result == SQLITE_OK ? copiedFrames : -1;
			++checkpointer.checkpoints;
		}
	});
	return true;
}

void ComputeDatabaseCheckpointerStop(DatabaseCheckpointer &checkpointer)
{
	if (checkpointer.thread.joinable())
	{
		{
			std::lock_guard<std::mutex> lock(checkpointer.mutex);
			checkpointer.stopping = true;
		}
		checkpointer.wake.notify_all();
		checkpointer.thread.join();
	}
	sqlite3_close(checkpointer.db);
	checkpointer.db = nullptr;
}

}
