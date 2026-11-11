#include "features/database/computation/database_compute.h"

#include <chrono>
#include <cstddef>
#include <sqlite3.h>

namespace zx
{

namespace
{

// [Doruk-Aksoy] Resets a statement when the call using it returns, so no cached statement keeps a read open across tics.
struct StatementUse
{
	sqlite3_stmt *statement;
	explicit StatementUse(sqlite3_stmt *s) : statement(s) {}
	~StatementUse()
	{
		if (statement != nullptr)
		{
			sqlite3_reset(statement);
			sqlite3_clear_bindings(statement);
		}
	}
};

sqlite3_stmt *statementFor(DatabaseStatements &statements, DatabaseQuery query)
{
	sqlite3_stmt *&slot = statements.prepared[static_cast<int>(query)];
	if (slot == nullptr && sqlite3_prepare_v2(statements.db, ComputeDatabaseQuerySql(query), -1, &slot, nullptr) != SQLITE_OK)
	{
		sqlite3_finalize(slot);
		slot = nullptr;
	}
	return slot;
}

std::string columnText(sqlite3_stmt *statement, int column)
{
	const unsigned char *text = sqlite3_column_text(statement, column);
	return text != nullptr ? reinterpret_cast<const char *>(text) : "";
}

// [Doruk-Aksoy] Binds the namespace and key every statement starts with.
void bindKey(sqlite3_stmt *statement, const char *ns, const char *key)
{
	sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
	sqlite3_bind_text(statement, 2, key, -1, SQLITE_STATIC);
}

bool runToDone(sqlite3_stmt *statement)
{
	return sqlite3_step(statement) == SQLITE_DONE;
}

bool collectRows(sqlite3_stmt *statement, DatabaseEntries &entries)
{
	entries.clear();
	int result;
	while ((result = sqlite3_step(statement)) == SQLITE_ROW)
		entries.emplace_back(columnText(statement, 0), columnText(statement, 1));
	return result == SQLITE_DONE;
}

#define ZX_DB_TABLE "Zandronum"
#define ZX_DB_NOW "(SELECT (julianday('now') - 2440587.5)*86400.0)"
#define ZX_DB_RANK(op) "SELECT (SELECT COUNT(*) FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND CAST(Value AS INTEGER)" op "CAST(v.Value AS INTEGER))+1 FROM " ZX_DB_TABLE " v WHERE v.Namespace=?1 AND v.KeyName=?2"
#define ZX_DB_SORT(dir) "SELECT KeyName, Value FROM " ZX_DB_TABLE " WHERE Namespace=?1 ORDER BY CAST(Value AS INTEGER) " dir ", KeyName ASC LIMIT ?2 OFFSET ?3"

const char *const kQuerySql[] = {
	"SELECT Value FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND KeyName=?2",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3," ZX_DB_NOW ") ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=excluded.Value,Timestamp=excluded.Timestamp",
	"DELETE FROM " ZX_DB_TABLE " WHERE Namespace=?1 AND KeyName=?2",
	"INSERT INTO " ZX_DB_TABLE " VALUES(?1,?2,?3," ZX_DB_NOW ") ON CONFLICT(Namespace,KeyName) DO UPDATE SET Value=CAST(Value AS INTEGER)+?3,Timestamp=excluded.Timestamp",
	ZX_DB_RANK("<"),
	ZX_DB_RANK(">"),
	ZX_DB_SORT("ASC"),
	ZX_DB_SORT("DESC"),
	"SELECT KeyName, Value FROM " ZX_DB_TABLE " WHERE Namespace=?1 ORDER BY KeyName",
};
static_assert(sizeof(kQuerySql) / sizeof(kQuerySql[0]) == static_cast<size_t>(DatabaseQuery::Count), "one SQL string per DatabaseQuery");

}

DatabaseStatements::~DatabaseStatements()
{
	ComputeDatabaseBind(*this, nullptr);
}

const char *ComputeDatabaseQuerySql(DatabaseQuery query)
{
	return kQuerySql[static_cast<int>(query)];
}

const char *ComputeDatabaseIndexSql()
{
	return "CREATE INDEX IF NOT EXISTS fua_ValueOrder ON " ZX_DB_TABLE "(Namespace, CAST(Value AS INTEGER), KeyName)";
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

bool ComputeDatabaseGet(DatabaseStatements &statements, const char *ns, const char *key, std::string &value, bool &found)
{
	value.clear();
	found = false;
	StatementUse use(statementFor(statements, DatabaseQuery::Get));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW)
	{
		value = columnText(use.statement, 0);
		found = true;
	}
	return result == SQLITE_ROW || result == SQLITE_DONE;
}

bool ComputeDatabaseSaveSet(DatabaseStatements &statements, const char *ns, const char *key, const char *value)
{
	const bool erase = value == nullptr || *value == '\0';
	StatementUse use(statementFor(statements, erase ? DatabaseQuery::Delete : DatabaseQuery::Upsert));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	if (!erase)
		sqlite3_bind_text(use.statement, 3, value, -1, SQLITE_STATIC);
	return runToDone(use.statement);
}

bool ComputeDatabaseIncrement(DatabaseStatements &statements, const char *ns, const char *key, int increment)
{
	StatementUse use(statementFor(statements, DatabaseQuery::Increment));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	sqlite3_bind_int(use.statement, 3, increment);
	return runToDone(use.statement);
}

bool ComputeDatabaseRank(DatabaseStatements &statements, const char *ns, const char *key, bool descending, int &rank)
{
	rank = -1;
	StatementUse use(statementFor(statements, descending ? DatabaseQuery::RankDescending : DatabaseQuery::RankAscending));
	if (use.statement == nullptr)
		return false;
	bindKey(use.statement, ns, key);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW)
		rank = sqlite3_column_int(use.statement, 0);
	return result == SQLITE_ROW || result == SQLITE_DONE;
}

bool ComputeDatabaseSorted(DatabaseStatements &statements, const char *ns, int count, int offset, bool descending, DatabaseEntries &entries)
{
	entries.clear();
	StatementUse use(statementFor(statements, descending ? DatabaseQuery::SortDescending : DatabaseQuery::SortAscending));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, ns, -1, SQLITE_STATIC);
	sqlite3_bind_int(use.statement, 2, count);
	sqlite3_bind_int(use.statement, 3, offset);
	return collectRows(use.statement, entries);
}

bool ComputeDatabaseEntries(DatabaseStatements &statements, const char *ns, DatabaseEntries &entries)
{
	entries.clear();
	StatementUse use(statementFor(statements, DatabaseQuery::Entries));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_text(use.statement, 1, ns, -1, SQLITE_STATIC);
	return collectRows(use.statement, entries);
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
