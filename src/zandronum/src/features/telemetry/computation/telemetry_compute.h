// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The arithmetic of features/telemetry, with no engine in it: the frame ring, its statistics, the hitch rule, the capture and trace formats, snapshot diffs, the growth verdict soak tests judge by, and the ledger memory is attributed with.

#ifndef ZX_TELEMETRY_COMPUTE_H
#define ZX_TELEMETRY_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zx { namespace telemetry {

// [rc4l] What a counter's value for a frame means: a level read when sampled, an amount summed over the frame, or milliseconds timed over it.
enum class CounterKind { Gauge, Total, Time };

// [rc4l] The newest frames' readings, a row of float columns per frame, the oldest overwritten first.
class FrameRing
{
public:
	FrameRing(int rows, int columns);
	// [rc4l] Adds frame's row from the first count of values, columns past count reading zero.
	void Push(int64_t frame, const float *values, int count);
	int Rows() const { return m_size; }
	int Columns() const { return m_columns; }
	int Capacity() const { return m_capacity; }
	// [rc4l] The row ago frames before the newest, 0 being the newest; ago must be below Rows().
	const float *Row(int ago) const;
	int64_t Frame(int ago) const;
	void Clear();

private:
	int Index(int ago) const;
	std::vector<float> m_values;
	std::vector<int64_t> m_frames;
	int m_capacity, m_columns, m_head, m_size;
};

// [rc4l] One column's statistics over the newest rows of a ring, all zero for none.
struct ColumnStats { int n; double last, mean, min, max, p50, p95, p99; };
ColumnStats ComputeColumnStats(const FrameRing &ring, int column, int rows);

// [rc4l] A counter as fua_stats lists it; an unavailable one is a reading this platform cannot take.
struct StatsRow { std::string name, unit; ColumnStats stats; bool available; };
std::string ComputeStatsTable(const std::vector<StatsRow> &rows);
std::string ComputeStatsJson(const std::vector<StatsRow> &rows);

// [rc4l] A frame of frameMs is a hitch when it is over twice the recent median and over floorMs, so a steadily heavy scene is not one.
bool ComputeIsHitch(double frameMs, double medianMs, double floorMs);

// [rc4l] A capture's CSV: the frame, its time and each counter, a header and then a row per frame.
std::string ComputeCsvHeader(const std::vector<std::string> &names);
std::string ComputeCsvRow(int64_t frame, double timeMs, const float *values, int count);

// [rc4l] One timed region in Chrome's trace event format, which Perfetto and chrome://tracing open.
struct TraceEvent { std::string name; int thread; double startUs, durationUs; };
std::string ComputeChromeTrace(const std::vector<TraceEvent> &events, const std::vector<std::string> &threadNames);

// [rc4l] One named value of a snapshot, and what changed between two.
struct Reading { std::string name, unit; double value; };
struct DiffRow { std::string name, unit; double before, after, delta; };
// [rc4l] Every name in either snapshot with its change, the largest change first and an absent side read as zero.
std::vector<DiffRow> ComputeSnapshotDiff(const std::vector<Reading> &before, const std::vector<Reading> &after);
// [rc4l] The first limit rows with a change, as a text table.
std::string ComputeDiffTable(const std::vector<DiffRow> &rows, size_t limit);

// [rc4l] Whether a value sampled once a cycle keeps growing after its warmup cycles: the least-squares slope per cycle against floor (an amount) or share (of the mean), whichever is larger.
struct GrowthVerdict { bool growing; double perCycle; double total; int cycles; };
GrowthVerdict ComputeGrowthVerdict(const std::vector<double> &samples, int warmup, double floor, double share);

// [rc4l] Bytes and objects held per tag, for memory an owner can name; tags past maxTags share one called "other".
class TagLedger
{
public:
	explicit TagLedger(size_t maxTags);
	// [rc4l] Counts an object of bytes under tag, returning the slot it went to for its Remove.
	size_t Add(const std::string &tag, double bytes);
	void Remove(size_t slot, double bytes);
	size_t Size() const { return m_names.size(); }
	const std::string &Name(size_t slot) const { return m_names[slot]; }
	double Bytes(size_t slot) const { return m_bytes[slot]; }
	double Objects(size_t slot) const { return m_objects[slot]; }
	double TotalBytes() const;
	double TotalObjects() const;
	// [rc4l] Each tag that holds something, as readings named prefix + tag in bytes.
	std::vector<Reading> Readings(const std::string &prefix) const;

private:
	size_t m_maxTags;
	std::vector<std::string> m_names;
	std::vector<double> m_bytes, m_objects;
};

// [rc4l] Time spent per key (a thinker's class, say), named when first seen, taken as readings of the milliseconds since the last take.
class KeyedTimer
{
public:
	void Add(const void *key, const char *name, int64_t ns);
	// [rc4l] The top keys by time since the last take, as readings named prefix + name in ms, and the clock started again.
	std::vector<Reading> Take(const std::string &prefix, size_t top);

private:
	struct Entry { const void *key; std::string name; int64_t ns; int64_t calls; };
	std::vector<Entry> m_entries;
	size_t m_last = 0;
};

// [rc4l] A texture's size: its width, height, depth or array layers, mips and samples, and per texel either bytesPerTexel or, for a compressed format, blockBytes per blockWidth by blockHeight.
struct TextureShape { int width, height, depth, layers, mips, samples; double bytesPerTexel; int blockWidth, blockHeight; double blockBytes; };
double ComputeTextureBytes(const TextureShape &shape);

// [rc4l] An object's name as its memory tag: anything from a digit, '#' or '[' on dropped and trailing spaces trimmed, so numbered copies share a tag; unnamed ones are "unnamed".
std::string ComputeMemoryTag(const std::string &name);

// [rc4l] Bytes in the unit a human reads them in, as "12.3 MB", "512 KB" or "40 B".
std::string ComputeBytesText(double bytes);

// [rc4l] A field of a "Key:   value [kB]" listing such as Linux's /proc/self/status, in bytes when its unit is kB, else as written; -1 when the key is missing.
double ComputeStatusField(const std::string &text, const char *key);

// [rc4l] Readings from lines of a name, a tab and a value, each named prefix + name in unit; a line without both is skipped.
std::vector<Reading> ComputeTabbedReadings(const std::string &text, const std::string &prefix, const std::string &unit);

// [rc4l] A gauge named in a flight summary, by its column in a per-second ring of (mean, max) pairs.
struct SummaryGauge { std::string name, unit; int column; };
// [rc4l] The last seconds of a per-second ring as a few lines: the frame time's mean and max each second (frameColumn), each gauge's first and last value and its change, and the hitch count.
std::string ComputeFlightSummary(const FrameRing &seconds, int frameColumn, const std::vector<SummaryGauge> &gauges, int lastSeconds, int hitches);

}} // namespace zx::telemetry

#endif // ZX_TELEMETRY_COMPUTE_H
