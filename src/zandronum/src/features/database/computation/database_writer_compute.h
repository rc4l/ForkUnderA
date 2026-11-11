#ifndef ZX_DATABASE_WRITER_COMPUTE_H
#define ZX_DATABASE_WRITER_COMPUTE_H

#include "features/database/computation/database_compute.h"
#include "features/database/computation/database_queue_compute.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct sqlite3;

namespace zx
{

enum class DatabaseApplyResult
{
	Applied,
	AlreadyApplied,
	Busy,
	Failed,
};

// [Doruk-Aksoy] Applies one server's batch to the shared file in one transaction: newer sets win, deletes leave a marker, increments add, and the batch is recorded as applied.
DatabaseApplyResult ComputeDatabaseApplyBatch(DatabaseStatements &statements, const std::string &server, const DatabaseBatch &batch, std::string &error, int *failureCode = nullptr);

// [Doruk-Aksoy] Forgets delete markers older than `before`; a write queued longer than that could bring its key back.
bool ComputeDatabasePruneMarkers(DatabaseStatements &statements, double before);

// [Doruk-Aksoy] The thread that owns this server's writing connection to the shared file; a batch leaves its queue only once it is in the file.
struct DatabaseWriter
{
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<DatabaseBatch> queue;
	bool stopping = false;
	std::chrono::steady_clock::time_point drainUntil;
	sqlite3 *db = nullptr;
	std::string server;
	std::string lastError;
	int retryMs = 5;
	int failedRetryMs = 2000;
	int maintenanceMs = 60000;
	// [Doruk-Aksoy] Run on the writer's connection at start and then every maintenanceMs, as the store's orphan replay and marker pruning.
	std::function<void(DatabaseStatements &)> maintenance;
	std::atomic<uint64_t> applied{0};
	std::atomic<int> busyRetries{0};
	std::atomic<int> failures{0};
	// [Doruk-Aksoy] SQLite's code for the last failed attempt, 0 once a batch lands; with SQLITE_FULL, the file's page use as the writer saw it.
	std::atomic<int> lastFailureCode{0};
	std::atomic<int64_t> filePages{0};
	std::atomic<int64_t> fileMaxPages{0};
	std::atomic<int64_t> filePageSize{0};
	// [Doruk-Aksoy] How many times maintenance has finished since the writer started.
	std::atomic<int> maintenanceRuns{0};

	DatabaseWriter() = default;
	DatabaseWriter(const DatabaseWriter &) = delete;
	DatabaseWriter &operator=(const DatabaseWriter &) = delete;
	~DatabaseWriter();
};

// [Doruk-Aksoy] Opens the shared file for writing with `pragmas` and starts the thread; false when the file cannot be opened.
bool ComputeDatabaseWriterStart(DatabaseWriter &writer, const char *path, const std::string &server, const std::vector<std::string> &pragmas, int busyTimeoutMs);

void ComputeDatabaseWriterPush(DatabaseWriter &writer, const std::vector<DatabaseBatch> &batches);

size_t ComputeDatabaseWriterQueued(DatabaseWriter &writer);

std::string ComputeDatabaseWriterLastError(DatabaseWriter &writer);

// [Doruk-Aksoy] Keeps applying for up to drainMs, then stops; true when nothing was left, since what is left stays in the outbox.
bool ComputeDatabaseWriterStop(DatabaseWriter &writer, int drainMs);

}

#endif
