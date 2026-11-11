#include "features/database/computation/database_queue_compute.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace zx
{

namespace
{

bool isSqliteSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// [Doruk-Aksoy] Adds as SQLite's integers wrap, without the undefined behaviour of signed overflow.
int64_t wrappingAdd(int64_t a, int64_t b)
{
	return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
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

void rebuildOverlay(DatabaseQueue &queue)
{
	queue.overlay.clear();
	for (const DatabaseBatch &batch : queue.pending)
		for (const DatabaseOp &op : batch.ops)
			ComputeDatabaseOverlayAdd(queue.overlay, op);
	for (const std::vector<DatabaseOp> &ops : queue.batcher.sealed)
		for (const DatabaseOp &op : ops)
			ComputeDatabaseOverlayAdd(queue.overlay, op);
	for (const DatabaseOp &op : queue.batcher.open)
		ComputeDatabaseOverlayAdd(queue.overlay, op);
}

void sealOpen(DatabaseBatcher &batcher)
{
	if (!batcher.open.empty())
	{
		batcher.sealed.push_back(std::move(batcher.open));
		batcher.open.clear();
	}
}

// [Doruk-Aksoy] A key's value with this server's writes on top, reading the file only when the overlay alone cannot say.
bool resolveKey(DatabaseStatements &statements, const DatabaseOverlayEntry &entry, const std::string &ns, const std::string &key, std::string &value, bool &found, double *seenStamp)
{
	std::string fileValue;
	bool fileFound = false;
	if (!entry.hasBase && !ComputeDatabaseGet(statements, ns.c_str(), key.c_str(), fileValue, fileFound, seenStamp))
		return false;
	found = ComputeDatabaseOverlayResolve(entry, fileFound, fileValue, value);
	return true;
}

struct SortRow
{
	int64_t order;
	std::string key;
	std::string value;
};

}

int64_t ComputeDatabaseCastInteger(const std::string &text)
{
	const size_t size = text.size();
	size_t i = 0;
	while (i < size && isSqliteSpace(text[i]))
		++i;
	bool negative = false;
	if (i < size && (text[i] == '-' || text[i] == '+'))
	{
		negative = text[i] == '-';
		++i;
	}
	const uint64_t limit = negative ? static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1 : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	uint64_t magnitude = 0;
	for (; i < size && text[i] >= '0' && text[i] <= '9'; ++i)
	{
		const uint64_t digit = static_cast<uint64_t>(text[i] - '0');
		magnitude = magnitude > (limit - digit) / 10 ? limit : magnitude * 10 + digit;
	}
	return negative ? static_cast<int64_t>(0 - magnitude) : static_cast<int64_t>(magnitude);
}

bool operator==(const DatabaseOp &a, const DatabaseOp &b)
{
	return a.kind == b.kind && a.ns == b.ns && a.key == b.key && a.value == b.value && a.delta == b.delta;
}

DatabaseOp ComputeDatabaseSetOp(const char *ns, const char *key, const char *value)
{
	DatabaseOp op;
	op.ns = ns != nullptr ? ns : "";
	op.key = key != nullptr ? key : "";
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
	op.ns = ns != nullptr ? ns : "";
	op.key = key != nullptr ? key : "";
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

void ComputeDatabaseOverlayAdd(DatabaseOverlay &overlay, const DatabaseOp &op)
{
	DatabaseOverlayEntry &entry = overlay[std::make_pair(op.ns, op.key)];
	switch (op.kind)
	{
	case DatabaseOpKind::Set:
		entry = DatabaseOverlayEntry();
		entry.hasBase = true;
		entry.base = op.value;
		break;
	case DatabaseOpKind::Delete:
		entry = DatabaseOverlayEntry();
		entry.hasBase = true;
		entry.deleted = true;
		break;
	case DatabaseOpKind::Increment:
		// [Doruk-Aksoy] An increment on a deleted key starts it again from zero, as the SQL insert does.
		entry.deleted = false;
		entry.incremented = true;
		entry.delta = wrappingAdd(entry.delta, op.delta);
		break;
	}
}

bool ComputeDatabaseOverlayResolve(const DatabaseOverlayEntry &entry, bool fileFound, const std::string &fileValue, std::string &value)
{
	value.clear();
	if (entry.hasBase && entry.deleted)
		return false;
	const std::string &base = entry.hasBase ? entry.base : fileValue;
	if (!entry.incremented)
	{
		value = base;
		return entry.hasBase || fileFound;
	}
	value = std::to_string(wrappingAdd(ComputeDatabaseCastInteger(base), entry.delta));
	return true;
}

void ComputeDatabaseQueueWrite(DatabaseQueue &queue, const DatabaseOp &op)
{
	queue.batcher.open.push_back(op);
	ComputeDatabaseOverlayAdd(queue.overlay, op);
}

std::vector<DatabaseBatch> ComputeDatabaseQueueSeal(DatabaseQueue &queue, double wallSeconds)
{
	if (!queue.batcher.modOpen)
		sealOpen(queue.batcher);
	std::vector<DatabaseBatch> batches;
	for (std::vector<DatabaseOp> &ops : queue.batcher.sealed)
	{
		DatabaseBatch batch;
		batch.seq = queue.nextSeq++;
		batch.stamp = ComputeDatabaseClockStamp(queue.clock, wallSeconds);
		batch.ops = std::move(ops);
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
		queue.pending.pop_front();
		dropped = true;
	}
	if (dropped)
		rebuildOverlay(queue);
	return dropped;
}

bool ComputeDatabaseMergedGet(DatabaseStatements &statements, const DatabaseOverlay &overlay, const char *ns, const char *key, std::string &value, bool &found, double *seenStamp)
{
	const auto entry = overlay.find(std::make_pair(std::string(ns), std::string(key)));
	if (entry == overlay.end())
		return ComputeDatabaseGet(statements, ns, key, value, found, seenStamp);
	return resolveKey(statements, entry->second, ns, key, value, found, seenStamp);
}

bool ComputeDatabaseMergedEntries(DatabaseStatements &statements, const DatabaseOverlay &overlay, const char *ns, DatabaseEntries &entries, double *seenStamp)
{
	if (!ComputeDatabaseEntries(statements, ns, entries, seenStamp))
		return false;
	const auto range = namespaceRange(overlay, ns);
	if (range.first == range.second)
		return true;
	std::map<std::string, std::string> rows(entries.begin(), entries.end());
	for (auto it = range.first; it != range.second; ++it)
	{
		const auto row = rows.find(it->first.second);
		std::string value;
		if (ComputeDatabaseOverlayResolve(it->second, row != rows.end(), row != rows.end() ? row->second : std::string(), value))
			rows[it->first.second] = value;
		else if (row != rows.end())
			rows.erase(row);
	}
	entries.assign(rows.begin(), rows.end());
	return true;
}

bool ComputeDatabaseMergedSorted(DatabaseStatements &statements, const DatabaseOverlay &overlay, const char *ns, int64_t count, int64_t offset, bool descending, DatabaseEntries &entries, double *seenStamp)
{
	const auto range = namespaceRange(overlay, ns);
	const int64_t touched = std::distance(range.first, range.second);
	if (touched == 0)
		return ComputeDatabaseSorted(statements, ns, count, offset, descending, entries, seenStamp);
	// [Doruk-Aksoy] The file's first offset+count+touched rows hold every row that can make the result, since at most `touched` of them change.
	const int64_t skip = std::max<int64_t>(offset, 0);
	const int64_t window = count < 0 ? -1 : skip + count + touched;
	DatabaseEntries fileRows;
	if (!ComputeDatabaseSorted(statements, ns, window, 0, descending, fileRows, seenStamp))
		return false;
	std::vector<SortRow> rows;
	for (const auto &row : fileRows)
		if (overlay.find(std::make_pair(std::string(ns), row.first)) == overlay.end())
			rows.push_back({ ComputeDatabaseCastInteger(row.second), row.first, row.second });
	for (auto it = range.first; it != range.second; ++it)
	{
		std::string value;
		bool found = false;
		if (!resolveKey(statements, it->second, it->first.first, it->first.second, value, found, seenStamp))
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

bool ComputeDatabaseMergedRank(DatabaseStatements &statements, const DatabaseOverlay &overlay, const char *ns, const char *key, bool descending, int &rank)
{
	const auto range = namespaceRange(overlay, ns);
	if (range.first == range.second)
		return ComputeDatabaseRank(statements, ns, key, descending, rank);
	rank = -1;
	std::string value;
	bool found = false;
	if (!ComputeDatabaseMergedGet(statements, overlay, ns, key, value, found))
		return false;
	if (!found)
		return true;
	const int64_t order = ComputeDatabaseCastInteger(value);
	const auto beyond = [descending, order](const std::string &other) {
		const int64_t otherOrder = ComputeDatabaseCastInteger(other);
		return descending ? otherOrder > order : otherOrder < order;
	};
	int64_t count = 0;
	if (!ComputeDatabaseCountBeyond(statements, ns, order, descending, count))
		return false;
	// [Doruk-Aksoy] Each key this server changed swaps its counted file value for its merged one.
	for (auto it = range.first; it != range.second; ++it)
	{
		std::string fileValue, merged;
		bool fileFound = false;
		if (!ComputeDatabaseGet(statements, ns, it->first.second.c_str(), fileValue, fileFound))
			return false;
		if (fileFound && beyond(fileValue))
			--count;
		if (ComputeDatabaseOverlayResolve(it->second, fileFound, fileValue, merged) && beyond(merged))
			++count;
	}
	rank = static_cast<int>(std::min<int64_t>(count + 1, std::numeric_limits<int>::max()));
	return true;
}

}
