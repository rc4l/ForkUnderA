#include "features/database/computation/database_queue_compute.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sqlite3.h>

namespace zx
{

namespace
{

bool isSqliteSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::string text(const char *s)
{
	return s != nullptr ? s : "";
}

// [Doruk-Aksoy] The overlay entries of one namespace, as a range of the ordered map.
std::pair<DatabaseOverlay::const_iterator, DatabaseOverlay::const_iterator> namespaceRange(const DatabaseOverlay &overlay, const std::string &ns)
{
	auto first = overlay.lower_bound(std::make_pair(ns, std::string()));
	auto last = first;
	while (last != overlay.end() && last->first.first == ns)
		++last;
	return { first, last };
}

void sealOpen(DatabaseBatcher &batcher)
{
	if (batcher.open.empty())
		return;
	DatabaseBatch batch;
	batch.seq = batcher.openSeq;
	batch.ops = std::move(batcher.open);
	batcher.open.clear();
	batcher.sealed.push_back(std::move(batch));
}

void rebuildOverlay(DatabaseQueue &queue)
{
	queue.overlay.clear();
	for (const DatabaseBatch &batch : queue.pending)
		for (const DatabaseOp &op : batch.ops)
			ComputeDatabaseOverlayAdd(queue.overlay, batch.seq, op);
	for (const DatabaseBatch &batch : queue.batcher.sealed)
		for (const DatabaseOp &op : batch.ops)
			ComputeDatabaseOverlayAdd(queue.overlay, batch.seq, op);
	for (const DatabaseOp &op : queue.batcher.open)
		ComputeDatabaseOverlayAdd(queue.overlay, queue.batcher.openSeq, op);
}

// [Doruk-Aksoy] A key's value with this server's pending ops on top, reading the file only when those ops are increments alone.
bool resolveKey(DatabaseStatements &statements, const DatabaseOverlayOps &ops, uint64_t applied, const std::string &ns, const std::string &key, std::string &value, bool &found, double *seenStamp)
{
	std::string fileValue;
	bool fileFound = false;
	if (ComputeDatabaseOverlayReadsFile(ops) && !ComputeDatabaseGet(statements, ns.c_str(), key.c_str(), fileValue, fileFound, seenStamp))
		return false;
	found = ComputeDatabaseOverlayResolve(ops, applied, fileFound, fileValue, value);
	return true;
}

struct SortRow
{
	int64_t order;
	std::string key;
	std::string value;
};

}

int64_t ComputeDatabaseCastInteger(const std::string &value)
{
	const size_t size = value.size();
	size_t i = 0;
	while (i < size && isSqliteSpace(value[i]))
		++i;
	bool negative = false;
	if (i < size && (value[i] == '-' || value[i] == '+'))
	{
		negative = value[i] == '-';
		++i;
	}
	const uint64_t limit = negative ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1 : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	uint64_t magnitude = 0;
	for (; i < size && value[i] >= '0' && value[i] <= '9'; ++i)
	{
		const uint64_t digit = static_cast<uint64_t>(value[i] - '0');
		magnitude = magnitude > (limit - digit) / 10 ? limit : magnitude * 10 + digit;
	}
	return negative ? static_cast<int64_t>(0 - magnitude) : static_cast<int64_t>(magnitude);
}

std::string ComputeDatabaseAddText(const std::string &value, int64_t delta)
{
	const int64_t base = ComputeDatabaseCastInteger(value);
	const bool overflows = delta > 0 ? base > std::numeric_limits<int64_t>::max() - delta : base < std::numeric_limits<int64_t>::min() - delta;
	if (!overflows)
		return std::to_string(base + delta);
	// [Doruk-Aksoy] SQLite adds in floating point once the integers overflow, and stores the REAL as its own %!.15g text.
	char *real = sqlite3_mprintf("%!.15g", static_cast<double>(base) + static_cast<double>(delta));
	const std::string result = real;
	sqlite3_free(real);
	return result;
}

bool operator==(const DatabaseOp &a, const DatabaseOp &b)
{
	return a.kind == b.kind && a.ns == b.ns && a.key == b.key && a.value == b.value && a.delta == b.delta;
}

DatabaseOp ComputeDatabaseSetOp(const char *ns, const char *key, const char *value)
{
	DatabaseOp op;
	op.ns = text(ns);
	op.key = text(key);
	if (value == nullptr || *value == '\0')
		op.kind = DatabaseOpKind::Delete;
	else
		op.value = value;
	return op;
}

DatabaseOp ComputeDatabaseIncrementOp(const char *ns, const char *key, int64_t delta)
{
	DatabaseOp op;
	op.kind = DatabaseOpKind::Increment;
	op.ns = text(ns);
	op.key = text(key);
	op.delta = delta;
	return op;
}

double ComputeDatabaseClockStamp(DatabaseClock &clock, double wallSeconds)
{
	// [Doruk-Aksoy] Whole steps of 0.1 ms keep 14 digits, which the Timestamp column's text holds exactly, so equal stamps still compare equal once stored.
	const double stepsPerSecond = 1 / kDatabaseClockStep;
	const int64_t wall = std::llround(wallSeconds * stepsPerSecond);
	const int64_t next = std::llround(clock.last * stepsPerSecond) + 1;
	clock.last = static_cast<double>(wall > next ? wall : next) / stepsPerSecond;
	return clock.last;
}

bool ComputeDatabaseClockObserve(DatabaseClock &clock, double seenStamp, double wallSeconds)
{
	if (seenStamp > clock.last)
		clock.last = seenStamp;
	return seenStamp > wallSeconds + kDatabaseClockAheadWarning;
}

DatabaseTxnResult ComputeDatabaseBatcherBegin(DatabaseBatcher &batcher)
{
	if (batcher.modOpen)
		return DatabaseTxnResult::AlreadyOpen;
	sealOpen(batcher);
	batcher.modOpen = true;
	batcher.modTics = 0;
	return DatabaseTxnResult::Ok;
}

DatabaseTxnResult ComputeDatabaseBatcherEnd(DatabaseBatcher &batcher)
{
	if (!batcher.modOpen)
		return DatabaseTxnResult::NotOpen;
	sealOpen(batcher);
	batcher.modOpen = false;
	return DatabaseTxnResult::Ok;
}

void ComputeDatabaseOverlayAdd(DatabaseOverlay &overlay, uint64_t seq, const DatabaseOp &op)
{
	DatabaseOverlayOp entry;
	entry.seq = seq;
	entry.kind = op.kind;
	entry.value = op.value;
	entry.delta = op.delta;
	overlay[std::make_pair(op.ns, op.key)].push_back(std::move(entry));
}

bool ComputeDatabaseOverlayReadsFile(const DatabaseOverlayOps &ops)
{
	for (const DatabaseOverlayOp &op : ops)
		if (op.kind != DatabaseOpKind::Increment)
			return false;
	return true;
}

bool ComputeDatabaseOverlayReadsFile(const DatabaseOverlay &overlay, const std::string &ns, const std::string *key)
{
	if (key != nullptr)
	{
		const auto entry = overlay.find(std::make_pair(ns, *key));
		return entry != overlay.end() && ComputeDatabaseOverlayReadsFile(entry->second);
	}
	const auto range = namespaceRange(overlay, ns);
	for (auto it = range.first; it != range.second; ++it)
		if (ComputeDatabaseOverlayReadsFile(it->second))
			return true;
	return false;
}

bool ComputeDatabaseOverlayResolve(const DatabaseOverlayOps &ops, uint64_t applied, bool fileFound, const std::string &fileValue, std::string &value)
{
	// [Doruk-Aksoy] A set or delete decides the key whatever the file holds, so only the increments after the last one are folded on; with none, the file's value takes the increments it does not hold yet.
	size_t start = 0;
	bool found = fileFound;
	std::string current = fileFound ? fileValue : std::string();
	bool decided = false;
	for (size_t i = ops.size(); i-- > 0;)
		if (ops[i].kind != DatabaseOpKind::Increment)
		{
			found = ops[i].kind == DatabaseOpKind::Set;
			current = found ? ops[i].value : std::string();
			start = i + 1;
			decided = true;
			break;
		}
	for (size_t i = start; i < ops.size(); ++i)
		if (decided || ops[i].seq > applied)
		{
			current = ComputeDatabaseAddText(found ? current : std::string(), ops[i].delta);
			found = true;
		}
	value = found ? current : std::string();
	return found;
}

void ComputeDatabaseOverlayTrim(DatabaseOverlay &overlay, const DatabaseBatch &batch, uint64_t applied)
{
	for (const DatabaseOp &op : batch.ops)
	{
		const auto entry = overlay.find(std::make_pair(op.ns, op.key));
		if (entry == overlay.end())
			continue;
		DatabaseOverlayOps &ops = entry->second;
		ops.erase(ops.begin(), std::find_if(ops.begin(), ops.end(), [applied](const DatabaseOverlayOp &kept) { return kept.seq > applied; }));
		if (ops.empty())
			overlay.erase(entry);
	}
}

void ComputeDatabaseQueueWrite(DatabaseQueue &queue, const DatabaseOp &op)
{
	if (queue.batcher.open.empty())
		queue.batcher.openSeq = queue.nextSeq++;
	queue.batcher.open.push_back(op);
	ComputeDatabaseOverlayAdd(queue.overlay, queue.batcher.openSeq, op);
}

std::vector<DatabaseBatch> ComputeDatabaseQueueSeal(DatabaseQueue &queue, double wallSeconds, bool *modExpired)
{
	bool expired = false;
	if (queue.batcher.modOpen && ++queue.batcher.modTics >= kDatabaseModTransactionTics)
	{
		queue.batcher.modOpen = false;
		expired = true;
	}
	if (modExpired != nullptr)
		*modExpired = expired;
	if (!queue.batcher.modOpen)
		sealOpen(queue.batcher);
	std::vector<DatabaseBatch> batches;
	for (DatabaseBatch &batch : queue.batcher.sealed)
	{
		batch.stamp = ComputeDatabaseClockStamp(queue.clock, wallSeconds);
		queue.pending.push_back(batch);
		batches.push_back(std::move(batch));
	}
	queue.batcher.sealed.clear();
	return batches;
}

void ComputeDatabaseQueueRestore(DatabaseQueue &queue, const std::vector<DatabaseBatch> &batches, uint64_t nextSeq)
{
	queue.pending.insert(queue.pending.begin(), batches.begin(), batches.end());
	for (const DatabaseBatch &batch : batches)
		if (batch.stamp > queue.clock.last)
			queue.clock.last = batch.stamp;
	queue.nextSeq = std::max(queue.nextSeq, nextSeq);
	rebuildOverlay(queue);
}

bool ComputeDatabaseQueueAcknowledge(DatabaseQueue &queue, uint64_t appliedSeq)
{
	queue.applied = std::max(queue.applied, appliedSeq);
	bool dropped = false;
	while (!queue.pending.empty() && queue.pending.front().seq <= queue.applied)
	{
		ComputeDatabaseOverlayTrim(queue.overlay, queue.pending.front(), queue.applied);
		queue.pending.pop_front();
		dropped = true;
	}
	return dropped;
}

bool ComputeDatabaseMergedGet(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, const char *key, std::string &value, bool &found, double *seenStamp)
{
	const std::string space = text(ns), name = text(key);
	const auto entry = overlay.find(std::make_pair(space, name));
	if (entry == overlay.end())
		return ComputeDatabaseGet(statements, space.c_str(), name.c_str(), value, found, seenStamp);
	return resolveKey(statements, entry->second, applied, space, name, value, found, seenStamp);
}

bool ComputeDatabaseMergedEntries(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, DatabaseEntries &entries, double *seenStamp)
{
	const std::string space = text(ns);
	if (!ComputeDatabaseEntries(statements, space.c_str(), entries, seenStamp))
		return false;
	const auto range = namespaceRange(overlay, space);
	if (range.first == range.second)
		return true;
	std::map<std::string, std::string> rows(entries.begin(), entries.end());
	for (auto it = range.first; it != range.second; ++it)
	{
		const auto row = rows.find(it->first.second);
		std::string value;
		if (ComputeDatabaseOverlayResolve(it->second, applied, row != rows.end(), row != rows.end() ? row->second : std::string(), value))
			rows[it->first.second] = value;
		else if (row != rows.end())
			rows.erase(row);
	}
	entries.assign(rows.begin(), rows.end());
	return true;
}

bool ComputeDatabaseMergedSorted(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double *seenStamp)
{
	const std::string space = text(ns);
	const auto range = namespaceRange(overlay, space);
	const int64_t touched = std::distance(range.first, range.second);
	if (touched == 0)
		return ComputeDatabaseSorted(statements, space.c_str(), count, offset, descending, entries, seenStamp);
	// [Doruk-Aksoy] The file's first offset+count+touched rows hold every row that can make the result, since at most `touched` of them change.
	const int64_t skip = std::max<int64_t>(offset, 0);
	const int64_t window = count < 0 ? -1 : skip + count + touched;
	DatabaseEntries fileRows;
	if (!ComputeDatabaseSorted(statements, space.c_str(), window, 0, descending, fileRows, seenStamp))
		return false;
	std::vector<SortRow> rows;
	for (const auto &row : fileRows)
		if (overlay.find(std::make_pair(space, row.first)) == overlay.end())
			rows.push_back({ ComputeDatabaseCastInteger(row.second), row.first, row.second });
	for (auto it = range.first; it != range.second; ++it)
	{
		std::string value;
		bool found = false;
		if (!resolveKey(statements, it->second, applied, it->first.first, it->first.second, value, found, seenStamp))
			return false;
		if (found)
			rows.push_back({ ComputeDatabaseCastInteger(value), it->first.second, value });
	}
	std::sort(rows.begin(), rows.end(), [descending](const SortRow &a, const SortRow &b) {
		if (a.order != b.order)
			return descending ? a.order > b.order : a.order < b.order;
		return a.key < b.key;
	});
	entries.clear();
	for (int64_t i = skip; i < static_cast<int64_t>(rows.size()) && (count < 0 || i < skip + count); ++i)
		entries.emplace_back(rows[static_cast<size_t>(i)].key, rows[static_cast<size_t>(i)].value);
	return true;
}

bool ComputeDatabaseMergedRank(DatabaseStatements &statements, const DatabaseOverlay &overlay, uint64_t applied, const char *ns, const char *key, bool descending, int &rank)
{
	const std::string space = text(ns), name = text(key);
	const auto range = namespaceRange(overlay, space);
	if (range.first == range.second)
		return ComputeDatabaseRank(statements, space.c_str(), name.c_str(), descending, rank);
	rank = -1;
	std::string value;
	bool found = false;
	if (!ComputeDatabaseMergedGet(statements, overlay, applied, space.c_str(), name.c_str(), value, found))
		return false;
	if (!found)
		return true;
	const int64_t order = ComputeDatabaseCastInteger(value);
	const auto beyond = [descending, order](const std::string &other) {
		const int64_t otherOrder = ComputeDatabaseCastInteger(other);
		return descending ? otherOrder > order : otherOrder < order;
	};
	int64_t count = 0;
	if (!ComputeDatabaseCountBeyond(statements, space.c_str(), order, descending, count))
		return false;
	// [Doruk-Aksoy] Each key this server changed swaps its counted file value for its merged one.
	for (auto it = range.first; it != range.second; ++it)
	{
		std::string fileValue, merged;
		bool fileFound = false;
		if (!ComputeDatabaseGet(statements, space.c_str(), it->first.second.c_str(), fileValue, fileFound))
			return false;
		if (fileFound && beyond(fileValue))
			--count;
		if (ComputeDatabaseOverlayResolve(it->second, applied, fileFound, fileValue, merged) && beyond(merged))
			++count;
	}
	rank = static_cast<int>(std::min<int64_t>(count + 1, std::numeric_limits<int>::max()));
	return true;
}

}
