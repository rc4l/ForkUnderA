#ifndef ZX_DATABASE_TEST_SUPPORT_H
#define ZX_DATABASE_TEST_SUPPORT_H

// [Doruk-Aksoy] Databases and the legacy reference the features/database tests share.

#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_outbox_compute.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <vector>

namespace zxdbtest
{

inline const char *const kCreateTable = "CREATE TABLE if not exists Zandronum(Namespace text, KeyName text, Value text, Timestamp text, PRIMARY KEY (Namespace, KeyName))";

inline void exec(sqlite3 *db, const char *sql)
{
	ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK) << sql << ": " << sqlite3_errmsg(db);
}

// [Doruk-Aksoy] The engine's table and everything ComputeDatabaseSchemaSql adds beside it.
inline void createSchema(sqlite3 *db)
{
	exec(db, kCreateTable);
	for (const std::string &command : zx::ComputeDatabaseSchemaSql())
		exec(db, command.c_str());
}

// [Doruk-Aksoy] An in-memory database with the engine's schema.
struct MemoryDb
{
	sqlite3 *db = nullptr;
	MemoryDb()
	{
		sqlite3_open(":memory:", &db);
		createSchema(db);
	}
	~MemoryDb() { sqlite3_close(db); }
};

// [Doruk-Aksoy] The pre-cache za_database.cpp logic, statement for statement, as the reference the cached path must match.
struct LegacyDb
{
	sqlite3 *db = nullptr;
	LegacyDb()
	{
		sqlite3_open(":memory:", &db);
		exec(db, kCreateTable);
	}
	~LegacyDb() { sqlite3_close(db); }

	sqlite3_stmt *prepare(const std::string &sql)
	{
		sqlite3_stmt *statement = nullptr;
		sqlite3_prepare(db, sql.c_str(), -1, &statement, nullptr);
		return statement;
	}
	void keyed(sqlite3_stmt *statement, const char *ns, const char *key)
	{
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 2, key, -1, SQLITE_STATIC);
	}
	void run(const std::string &sql, const char *ns, const char *key, const char *value)
	{
		sqlite3_stmt *statement = prepare(sql);
		keyed(statement, ns, key);
		if (value != nullptr)
			sqlite3_bind_text(statement, 3, value, -1, SQLITE_STATIC);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}
	bool exists(const char *ns, const char *key)
	{
		sqlite3_stmt *statement = prepare("SELECT * FROM Zandronum WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		const bool found = sqlite3_step(statement) == SQLITE_ROW;
		sqlite3_finalize(statement);
		return found;
	}
	void saveSet(const char *ns, const char *key, const char *value)
	{
		const bool nonEmpty = value != nullptr && *value != '\0';
		if (exists(ns, key))
		{
			if (nonEmpty)
				run("UPDATE Zandronum SET Value=?3,Timestamp=(SELECT (julianday('now') - 2440587.5)*86400.0) WHERE Namespace=?1 AND KeyName=?2", ns, key, value);
			else
				run("DELETE FROM Zandronum WHERE Namespace=?1 AND KeyName=?2", ns, key, nullptr);
		}
		else if (nonEmpty)
			run("INSERT INTO Zandronum VALUES(?1,?2,?3,(SELECT (julianday('now') - 2440587.5)*86400.0))", ns, key, value);
	}
	std::string get(const char *ns, const char *key)
	{
		if (!exists(ns, key))
			return "";
		sqlite3_stmt *statement = prepare("SELECT * FROM Zandronum WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		sqlite3_step(statement);
		std::string value = reinterpret_cast<const char *>(sqlite3_column_text(statement, 2));
		sqlite3_finalize(statement);
		return value;
	}
	void increment(const char *ns, const char *key, int increment)
	{
		if (!exists(ns, key))
		{
			const std::string value = std::to_string(increment);
			run("INSERT INTO Zandronum VALUES(?1,?2,?3,(SELECT (julianday('now') - 2440587.5)*86400.0))", ns, key, value.c_str());
			return;
		}
		sqlite3_stmt *statement = prepare("UPDATE Zandronum SET Value=(SELECT CAST(Value AS INTEGER) FROM Zandronum WHERE Namespace=?1 AND KeyName=?2)+?3,Timestamp=(SELECT (julianday('now') - 2440587.5)*86400.0) WHERE Namespace=?1 AND KeyName=?2");
		keyed(statement, ns, key);
		sqlite3_bind_int(statement, 3, increment);
		sqlite3_step(statement);
		sqlite3_finalize(statement);
	}
	int rank(const char *ns, const char *key, bool descending)
	{
		if (!exists(ns, key))
			return -1;
		sqlite3_stmt *statement = prepare(std::string("SELECT COUNT(*) from Zandronum WHERE Namespace=?1 AND CAST(Value AS INTEGER)") + (descending ? ">" : "<") + "(SELECT CAST(Value AS INTEGER) FROM Zandronum WHERE Namespace=?2 AND KeyName=?3)");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 2, ns, -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 3, key, -1, SQLITE_STATIC);
		sqlite3_step(statement);
		const int result = sqlite3_column_int(statement, 0) + 1;
		sqlite3_finalize(statement);
		return result;
	}
	zx::DatabaseEntries rows(sqlite3_stmt *statement)
	{
		zx::DatabaseEntries entries;
		while (sqlite3_step(statement) == SQLITE_ROW)
			entries.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(statement, 1)), reinterpret_cast<const char *>(sqlite3_column_text(statement, 2)));
		sqlite3_finalize(statement);
		return entries;
	}
	zx::DatabaseEntries sorted(const char *ns, int count, int offset, bool descending)
	{
		sqlite3_stmt *statement = prepare(std::string("SELECT * from Zandronum WHERE Namespace=?1 ORDER BY CAST(Value AS INTEGER) ") + (descending ? "DESC" : "ASC") + " LIMIT ?2 OFFSET ?3");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		sqlite3_bind_int(statement, 2, count);
		sqlite3_bind_int(statement, 3, offset);
		return rows(statement);
	}
	zx::DatabaseEntries entries(const char *ns)
	{
		sqlite3_stmt *statement = prepare("SELECT * from Zandronum WHERE Namespace=?1");
		sqlite3_bind_text(statement, 1, ns, -1, SQLITE_STATIC);
		return rows(statement);
	}
};

// [Doruk-Aksoy] A file path of its own in the temp folder, unique across test processes started at the same moment.
inline std::filesystem::path tempDatabasePath(const char *name)
{
	static std::atomic<unsigned> counter{0};
	static const unsigned long long process = (static_cast<unsigned long long>(std::random_device()()) << 32) ^ std::random_device()();
	return std::filesystem::temp_directory_path() / (std::string("zx_db_") + name + "_" + std::to_string(process) + "_" + std::to_string(++counter) + ".db");
}

// [Doruk-Aksoy] Removes a database file with its -wal, -shm and outbox files.
inline void removeDatabaseFiles(const std::filesystem::path &path)
{
	std::error_code ignored;
	for (const char *suffix : { "", "-wal", "-shm", "-journal" })
	{
		std::filesystem::remove(path.string() + suffix, ignored);
		for (int slot = 0; slot < 4; ++slot)
			std::filesystem::remove(zx::ComputeDatabaseOutboxPath(path.string(), slot) + suffix, ignored);
	}
}

// [Doruk-Aksoy] Whether this platform's file system gives SQLite the WAL and file locks the file tests rely on; the browser build's in-memory one may not.
inline bool fileDatabasesSupported()
{
	static const bool supported = []() {
		const std::filesystem::path path = tempDatabasePath("probe");
		sqlite3 *a = nullptr, *b = nullptr;
		sqlite3_open(path.string().c_str(), &a);
		sqlite3_open(path.string().c_str(), &b);
		sqlite3_stmt *mode = nullptr;
		sqlite3_prepare_v2(a, "PRAGMA journal_mode=WAL", -1, &mode, nullptr);
		const bool wal = sqlite3_step(mode) == SQLITE_ROW && std::string(reinterpret_cast<const char *>(sqlite3_column_text(mode, 0))) == "wal";
		sqlite3_finalize(mode);
		sqlite3_exec(a, "PRAGMA locking_mode=EXCLUSIVE; BEGIN EXCLUSIVE; CREATE TABLE t(x); COMMIT", nullptr, nullptr, nullptr);
		const bool locks = sqlite3_exec(b, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr) == SQLITE_BUSY;
		sqlite3_close(b);
		sqlite3_close(a);
		removeDatabaseFiles(path);
		return wal && locks;
	}();
	return supported;
}

#define ZX_REQUIRE_FILE_DATABASES() 	if (!zxdbtest::fileDatabasesSupported()) 		GTEST_SKIP() << "this platform's file system gives SQLite no WAL or file locks"

// [Doruk-Aksoy] A WAL database file of its own in the temp folder with the engine's schema, removed with everything beside it.
struct FileDb
{
	std::filesystem::path path;
	sqlite3 *db = nullptr;
	explicit FileDb(const char *name)
	{
		path = tempDatabasePath(name);
		sqlite3_open(path.string().c_str(), &db);
		createSchema(db);
		for (const std::string &pragma : zx::ComputeDatabasePragmas(false, true, 2))
			exec(db, pragma.c_str());
		exec(db, "PRAGMA wal_autocheckpoint=0");
	}
	~FileDb()
	{
		sqlite3_close(db);
		removeDatabaseFiles(path);
	}
	// [Doruk-Aksoy] Read through a handle of its own, since Windows can report a stale size for a file another handle holds open.
	std::uintmax_t mainFileSize() const
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		return static_cast<std::uintmax_t>(stream.tellg());
	}
};

// [Doruk-Aksoy] Puts a statement that fails when stepped where a query's prepared one would be.
inline void poison(zx::DatabaseStatements &statements, zx::DatabaseQuery query)
{
	sqlite3_stmt *&slot = statements.prepared[static_cast<int>(query)];
	sqlite3_finalize(slot);
	sqlite3_prepare_v2(statements.db, "SELECT abs(-9223372036854775807 - 1)", -1, &slot, nullptr);
}

inline bool waitFor(const std::function<bool()> &condition)
{
	for (int i = 0; i < 500 && !condition(); ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	return condition();
}

}

#endif
