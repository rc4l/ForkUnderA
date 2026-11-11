#include "features/database/computation/database_writer_compute.h"

#include <sqlite3.h>

namespace zx
{

namespace
{

bool isBusy(int code)
{
	const int primary = code & 0xff;
	return primary == SQLITE_BUSY || primary == SQLITE_LOCKED;
}

// [Doruk-Aksoy] Runs a cached query to its first row or its end, handing a row to `read`; false (with the code) on any failure, preparing included.
template <typename Bind>
bool runQuery(DatabaseStatements &statements, DatabaseQuery query, int &code, Bind bind, const std::function<void(sqlite3_stmt *)> &read = nullptr)
{
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, query));
	if (use.statement == nullptr)
	{
		code = SQLITE_ERROR;
		return false;
	}
	bind(use.statement);
	const int result = sqlite3_step(use.statement);
	if (result == SQLITE_ROW && read)
		read(use.statement);
	if (result == SQLITE_ROW || result == SQLITE_DONE)
		return true;
	code = result;
	return false;
}

// [Doruk-Aksoy] Runs a write keyed by the op's namespace and key, with the value, increment or stamp its query takes.
bool keyed(DatabaseStatements &statements, DatabaseQuery query, const DatabaseOp &op, double stamp, int &code)
{
	return runQuery(statements, query, code, [&](sqlite3_stmt *statement) {
		sqlite3_bind_text(statement, 1, op.ns.c_str(), -1, SQLITE_STATIC);
		sqlite3_bind_text(statement, 2, op.key.c_str(), -1, SQLITE_STATIC);
		const int stampAt = query == DatabaseQuery::SetIfNewer || query == DatabaseQuery::IncrementStamped ? 4 : 3;
		if (query == DatabaseQuery::SetIfNewer)
			sqlite3_bind_text(statement, 3, op.value.c_str(), -1, SQLITE_STATIC);
		if (query == DatabaseQuery::IncrementStamped)
			sqlite3_bind_int64(statement, 3, op.delta);
		sqlite3_bind_double(statement, stampAt, stamp);
	});
}

bool applyOp(DatabaseStatements &statements, const DatabaseOp &op, double stamp, int &code)
{
	if (op.kind == DatabaseOpKind::Delete)
		return keyed(statements, DatabaseQuery::DeleteIfOlder, op, stamp, code) && keyed(statements, DatabaseQuery::MarkDeleted, op, stamp, code);
	// [Doruk-Aksoy] A delete newer than this write already stands on the key, so the write must not bring it back.
	double marker = -1;
	if (!runQuery(statements, DatabaseQuery::MarkerStamp, code, [&](sqlite3_stmt *statement) {
			sqlite3_bind_text(statement, 1, op.ns.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(statement, 2, op.key.c_str(), -1, SQLITE_STATIC);
		}, [&](sqlite3_stmt *statement) { marker = sqlite3_column_double(statement, 0); }))
		return false;
	if (marker > stamp)
		return true;
	const DatabaseQuery write = op.kind == DatabaseOpKind::Set ? DatabaseQuery::SetIfNewer : DatabaseQuery::IncrementStamped;
	return keyed(statements, write, op, stamp, code) && keyed(statements, DatabaseQuery::ClearMarker, op, stamp, code);
}

// [Doruk-Aksoy] SQLite's own message for what failed, such as the missing table, rather than its generic code text.
std::string failure(sqlite3 *db, int code)
{
	return db != nullptr && sqlite3_errcode(db) != SQLITE_OK ? sqlite3_errmsg(db) : sqlite3_errstr(code);
}

DatabaseApplyResult rollback(DatabaseStatements &statements, int code, std::string &error)
{
	error = failure(statements.db, code);
	sqlite3_exec(statements.db, "ROLLBACK", nullptr, nullptr, nullptr);
	return isBusy(code) ? DatabaseApplyResult::Busy : DatabaseApplyResult::Failed;
}

// [Doruk-Aksoy] Waits out a retry, waking early to stop; false once the writer should give up.
bool waitRetry(DatabaseWriter &writer, std::unique_lock<std::mutex> &lock, int ms)
{
	writer.wake.wait_for(lock, std::chrono::milliseconds(ms), [&writer]() { return writer.stopping; });
	return !writer.stopping || std::chrono::steady_clock::now() < writer.drainUntil;
}

void writerLoop(DatabaseWriter &writer)
{
	DatabaseStatements statements;
	ComputeDatabaseBind(statements, writer.db);
	auto nextMaintenance = std::chrono::steady_clock::now();
	std::unique_lock<std::mutex> lock(writer.mutex);
	for (;;)
	{
		if (writer.maintenance && std::chrono::steady_clock::now() >= nextMaintenance)
		{
			lock.unlock();
			writer.maintenance(statements);
			lock.lock();
			nextMaintenance = std::chrono::steady_clock::now() + std::chrono::milliseconds(writer.maintenanceMs);
		}
		if (writer.queue.empty())
		{
			if (writer.stopping)
				break;
			const auto ready = [&writer]() { return writer.stopping || !writer.queue.empty(); };
			if (writer.maintenance)
				writer.wake.wait_until(lock, nextMaintenance, ready);
			else
				writer.wake.wait(lock, ready);
			continue;
		}
		if (writer.stopping && std::chrono::steady_clock::now() >= writer.drainUntil)
			break;
		const DatabaseBatch batch = writer.queue.front();
		lock.unlock();
		std::string error;
		const DatabaseApplyResult result = ComputeDatabaseApplyBatch(statements, writer.server, batch, error);
		lock.lock();
		if (result == DatabaseApplyResult::Applied || result == DatabaseApplyResult::AlreadyApplied)
		{
			writer.queue.pop_front();
			writer.applied = batch.seq;
			continue;
		}
		const bool busy = result == DatabaseApplyResult::Busy;
		++(busy ? writer.busyRetries : writer.failures);
		if (!busy)
			writer.lastError = error;
		if (!waitRetry(writer, lock, busy ? writer.retryMs : writer.failedRetryMs))
			break;
	}
	lock.unlock();
	ComputeDatabaseBind(statements, nullptr);
}

}

DatabaseApplyResult ComputeDatabaseApplyBatch(DatabaseStatements &statements, const std::string &server, const DatabaseBatch &batch, std::string &error)
{
	error.clear();
	const int begin = sqlite3_exec(statements.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
	if (begin != SQLITE_OK)
	{
		error = failure(statements.db, begin);
		return isBusy(begin) ? DatabaseApplyResult::Busy : DatabaseApplyResult::Failed;
	}
	int code = SQLITE_OK;
	// [Doruk-Aksoy] Marks the transaction as the queue's, so the stock-delete trigger leaves this batch's own stamped markers alone.
	if (!runQuery(statements, DatabaseQuery::WritingBegin, code, [](sqlite3_stmt *) {}))
		return rollback(statements, code, error);
	int64_t done = 0;
	const auto bindServer = [&server](sqlite3_stmt *statement) { sqlite3_bind_text(statement, 1, server.c_str(), -1, SQLITE_STATIC); };
	if (!runQuery(statements, DatabaseQuery::AppliedSeq, code, bindServer, [&done](sqlite3_stmt *statement) { done = sqlite3_column_int64(statement, 0); }))
		return rollback(statements, code, error);
	const bool already = done >= static_cast<int64_t>(batch.seq);
	if (!already)
	{
		for (const DatabaseOp &op : batch.ops)
			if (!applyOp(statements, op, batch.stamp, code))
				return rollback(statements, code, error);
		const auto bindApplied = [&](sqlite3_stmt *statement) {
			bindServer(statement);
			sqlite3_bind_int64(statement, 2, static_cast<int64_t>(batch.seq));
		};
		if (!runQuery(statements, DatabaseQuery::SetApplied, code, bindApplied))
			return rollback(statements, code, error);
	}
	if (!runQuery(statements, DatabaseQuery::WritingEnd, code, [](sqlite3_stmt *) {}))
		return rollback(statements, code, error);
	const int commit = sqlite3_exec(statements.db, "COMMIT", nullptr, nullptr, nullptr);
	if (commit != SQLITE_OK)
		return rollback(statements, commit, error);
	return already ? DatabaseApplyResult::AlreadyApplied : DatabaseApplyResult::Applied;
}

bool ComputeDatabasePruneMarkers(DatabaseStatements &statements, double before)
{
	DatabaseStatementUse use(ComputeDatabaseStatement(statements, DatabaseQuery::PruneMarkers));
	if (use.statement == nullptr)
		return false;
	sqlite3_bind_double(use.statement, 1, before);
	return sqlite3_step(use.statement) == SQLITE_DONE;
}

DatabaseWriter::~DatabaseWriter()
{
	ComputeDatabaseWriterStop(*this, 0);
}

bool ComputeDatabaseWriterStart(DatabaseWriter &writer, const char *path, const std::string &server, const std::vector<std::string> &pragmas, int busyTimeoutMs)
{
	ComputeDatabaseWriterStop(writer, 0);
	if (sqlite3_open_v2(path, &writer.db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK)
	{
		sqlite3_close(writer.db);
		writer.db = nullptr;
		return false;
	}
	sqlite3_busy_timeout(writer.db, busyTimeoutMs);
	for (const std::string &pragma : pragmas)
		sqlite3_exec(writer.db, pragma.c_str(), nullptr, nullptr, nullptr);
	writer.server = server;
	writer.stopping = false;
	writer.lastError.clear();
	writer.applied = 0;
	writer.busyRetries = 0;
	writer.failures = 0;
	writer.thread = std::thread(writerLoop, std::ref(writer));
	return true;
}

void ComputeDatabaseWriterPush(DatabaseWriter &writer, const std::vector<DatabaseBatch> &batches)
{
	if (batches.empty())
		return;
	{
		std::lock_guard<std::mutex> lock(writer.mutex);
		writer.queue.insert(writer.queue.end(), batches.begin(), batches.end());
	}
	writer.wake.notify_all();
}

size_t ComputeDatabaseWriterQueued(DatabaseWriter &writer)
{
	std::lock_guard<std::mutex> lock(writer.mutex);
	return writer.queue.size();
}

std::string ComputeDatabaseWriterLastError(DatabaseWriter &writer)
{
	std::lock_guard<std::mutex> lock(writer.mutex);
	return writer.lastError;
}

bool ComputeDatabaseWriterStop(DatabaseWriter &writer, int drainMs)
{
	if (writer.thread.joinable())
	{
		{
			std::lock_guard<std::mutex> lock(writer.mutex);
			writer.stopping = true;
			writer.drainUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(drainMs > 0 ? drainMs : 0);
		}
		writer.wake.notify_all();
		writer.thread.join();
	}
	sqlite3_close(writer.db);
	writer.db = nullptr;
	std::lock_guard<std::mutex> lock(writer.mutex);
	const bool drained = writer.queue.empty();
	writer.queue.clear();
	return drained;
}

}
