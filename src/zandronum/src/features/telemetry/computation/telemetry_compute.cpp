// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/telemetry/computation/telemetry_compute.h"
#include "features/json/computation/json_compute.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace zx { namespace telemetry {

namespace {

// [rc4l] A number as a table shows it: bytes as their unit, large values whole, the rest to two places.
std::string Number(double v, const std::string &unit)
{
	if (unit == "bytes") return ComputeBytesText(v);
	char buf[64];
	std::snprintf(buf, sizeof(buf), std::fabs(v) >= 1000.0 ? "%.0f" : "%.2f", v);
	return buf;
}

// [rc4l] A number as JSON carries it.
std::string JsonNumber(double v)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.6g", std::isfinite(v) ? v : 0.0);
	return buf;
}

// [rc4l] A CSV field, quoted when it holds a comma or a quote.
std::string CsvField(const std::string &s)
{
	if (s.find_first_of(",\"") == std::string::npos) return s;
	std::string out = "\"";
	for (const char c : s) { out += c; if (c == '"') out += '"'; }
	return out + "\"";
}

// [rc4l] text padded with spaces to width.
std::string Pad(const std::string &text, size_t width)
{
	return text.size() >= width ? text : text + std::string(width - text.size(), ' ');
}

} // namespace

FrameRing::FrameRing(int rows, int columns)
	: m_values((size_t)std::max(1, rows) * (size_t)std::max(1, columns), 0.f), m_frames((size_t)std::max(1, rows), 0),
	  m_capacity(std::max(1, rows)), m_columns(std::max(1, columns)), m_head(0), m_size(0)
{
}

void FrameRing::Push(int64_t frame, const float *values, int count)
{
	float *row = &m_values[(size_t)m_head * (size_t)m_columns];
	const int n = std::min(std::max(count, 0), m_columns);
	for (int i = 0; i < m_columns; i++) row[i] = i < n ? values[i] : 0.f;
	m_frames[(size_t)m_head] = frame;
	m_head = (m_head + 1) % m_capacity;
	if (m_size < m_capacity) m_size++;
}

int FrameRing::Index(int ago) const
{
	return ((m_head - 1 - ago) % m_capacity + m_capacity) % m_capacity;
}

const float *FrameRing::Row(int ago) const
{
	return &m_values[(size_t)Index(ago) * (size_t)m_columns];
}

int64_t FrameRing::Frame(int ago) const
{
	return m_frames[(size_t)Index(ago)];
}

void FrameRing::Clear()
{
	m_head = m_size = 0;
}

ColumnStats ComputeColumnStats(const FrameRing &ring, int column, int rows)
{
	ColumnStats s = {};
	const int n = std::min(rows, ring.Rows());
	if (n <= 0 || column < 0 || column >= ring.Columns()) return s;
	std::vector<double> v((size_t)n);
	double sum = 0.0;
	for (int i = 0; i < n; i++) { v[(size_t)i] = ring.Row(i)[column]; sum += v[(size_t)i]; }
	s.n = n;
	s.last = v[0];
	s.mean = sum / n;
	std::sort(v.begin(), v.end());
	s.min = v.front();
	s.max = v.back();
	const auto pct = [&](double p) { return v[(size_t)(p / 100.0 * (n - 1) + 0.5)]; };
	s.p50 = pct(50);
	s.p95 = pct(95);
	s.p99 = pct(99);
	return s;
}

std::string ComputeStatsTable(const std::vector<StatsRow> &rows)
{
	size_t width = 7, unitWidth = 4;
	for (const StatsRow &r : rows) { width = std::max(width, r.name.size()); unitWidth = std::max(unitWidth, r.unit.size()); }
	std::string out = Pad("counter", width) + "  " + Pad("unit", unitWidth) + "  " + Pad("last", 10) + "  " + Pad("mean", 10) + "  " + Pad("p99", 10) + "  max\n";
	for (const StatsRow &r : rows)
	{
		out += Pad(r.name, width) + "  " + Pad(r.unit, unitWidth) + "  ";
		if (!r.available) { out += "n/a\n"; continue; }
		out += Pad(Number(r.stats.last, r.unit), 10) + "  " + Pad(Number(r.stats.mean, r.unit), 10) + "  " +
			Pad(Number(r.stats.p99, r.unit), 10) + "  " + Number(r.stats.max, r.unit) + "\n";
	}
	return out;
}

std::string ComputeStatsJson(const std::vector<StatsRow> &rows)
{
	std::string out = "[";
	for (size_t i = 0; i < rows.size(); i++)
	{
		const StatsRow &r = rows[i];
		const ColumnStats &s = r.stats;
		if (i) out += ",";
		out += "{\"name\":" + zx::json::ComputeJsonQuoted(r.name) + ",\"unit\":" + zx::json::ComputeJsonQuoted(r.unit) +
			",\"available\":" + (r.available ? "true" : "false") + ",\"n\":" + std::to_string(s.n) +
			",\"last\":" + JsonNumber(s.last) + ",\"mean\":" + JsonNumber(s.mean) + ",\"min\":" + JsonNumber(s.min) +
			",\"max\":" + JsonNumber(s.max) + ",\"p50\":" + JsonNumber(s.p50) + ",\"p95\":" + JsonNumber(s.p95) +
			",\"p99\":" + JsonNumber(s.p99) + "}";
	}
	return out + "]";
}

bool ComputeIsHitch(double frameMs, double medianMs, double floorMs)
{
	return frameMs > floorMs && frameMs > 2.0 * medianMs;
}

std::string ComputeCsvHeader(const std::vector<std::string> &names)
{
	std::string out = "frame,time_ms";
	for (const std::string &n : names) out += "," + CsvField(n);
	return out + "\n";
}

std::string ComputeCsvRow(int64_t frame, double timeMs, const float *values, int count)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%lld,%.3f", (long long)frame, timeMs);
	std::string out = buf;
	for (int i = 0; i < count; i++) out += "," + JsonNumber(values[i]);
	return out + "\n";
}

std::string ComputeChromeTrace(const std::vector<TraceEvent> &events, const std::vector<std::string> &threadNames)
{
	std::string out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
	bool first = true;
	for (size_t t = 0; t < threadNames.size(); t++)
	{
		out += std::string(first ? "" : ",") + "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" + std::to_string(t) +
			",\"args\":{\"name\":" + zx::json::ComputeJsonQuoted(threadNames[t]) + "}}";
		first = false;
	}
	for (const TraceEvent &e : events)
	{
		out += std::string(first ? "" : ",") + "{\"name\":" + zx::json::ComputeJsonQuoted(e.name) + ",\"ph\":\"X\",\"pid\":1,\"tid\":" +
			std::to_string(e.thread) + ",\"ts\":" + JsonNumber(e.startUs) + ",\"dur\":" + JsonNumber(e.durationUs) + "}";
		first = false;
	}
	return out + "]}\n";
}

std::vector<DiffRow> ComputeSnapshotDiff(const std::vector<Reading> &before, const std::vector<Reading> &after)
{
	std::map<std::string, DiffRow> rows;
	for (const Reading &r : before) { DiffRow &d = rows[r.name]; d.name = r.name; d.unit = r.unit; d.before = r.value; }
	for (const Reading &r : after) { DiffRow &d = rows[r.name]; d.name = r.name; d.unit = r.unit; d.after = r.value; }
	std::vector<DiffRow> out;
	for (auto &kv : rows) { kv.second.delta = kv.second.after - kv.second.before; out.push_back(kv.second); }
	std::stable_sort(out.begin(), out.end(), [](const DiffRow &a, const DiffRow &b) { return std::fabs(a.delta) > std::fabs(b.delta); });
	return out;
}

std::string ComputeDiffTable(const std::vector<DiffRow> &rows, size_t limit)
{
	size_t width = 7;
	for (const DiffRow &r : rows) width = std::max(width, r.name.size());
	std::string out = Pad("counter", width) + "  " + Pad("before", 10) + "  " + Pad("after", 10) + "  change\n";
	size_t shown = 0;
	for (const DiffRow &r : rows)
	{
		if (r.delta == 0.0 || shown == limit) continue;
		shown++;
		out += Pad(r.name, width) + "  " + Pad(Number(r.before, r.unit), 10) + "  " + Pad(Number(r.after, r.unit), 10) + "  " +
			(r.delta > 0 ? "+" : "") + Number(r.delta, r.unit) + "\n";
	}
	if (shown == 0) out += "(no change)\n";
	return out;
}

GrowthVerdict ComputeGrowthVerdict(const std::vector<double> &samples, int warmup, double floor, double share)
{
	GrowthVerdict g = { false, 0.0, 0.0, 0 };
	const int start = std::max(0, warmup);
	const int n = (int)samples.size() - start;
	g.cycles = std::max(0, n);
	if (n < 3) return g;
	double mx = 0.0, my = 0.0;
	for (int i = 0; i < n; i++) { mx += i; my += samples[(size_t)(start + i)]; }
	mx /= n;
	my /= n;
	double sxy = 0.0, sxx = 0.0;
	for (int i = 0; i < n; i++)
	{
		const double dx = i - mx;
		sxy += dx * (samples[(size_t)(start + i)] - my);
		sxx += dx * dx;
	}
	g.perCycle = sxy / sxx;
	g.total = samples.back() - samples[(size_t)start];
	const double limit = std::max(floor, share * std::fabs(my));
	g.growing = g.perCycle > limit && g.total > limit;
	return g;
}

TagLedger::TagLedger(size_t maxTags) : m_maxTags(std::max<size_t>(1, maxTags))
{
}

size_t TagLedger::Add(const std::string &tag, double bytes)
{
	size_t slot = std::find(m_names.begin(), m_names.end(), tag) - m_names.begin();
	if (slot == m_names.size() && m_names.size() + 1 >= m_maxTags)
		slot = std::find(m_names.begin(), m_names.end(), std::string("other")) - m_names.begin();
	if (slot == m_names.size())
	{
		m_names.push_back(m_names.size() + 1 >= m_maxTags ? std::string("other") : tag);
		m_bytes.push_back(0.0);
		m_objects.push_back(0.0);
	}
	m_bytes[slot] += bytes;
	m_objects[slot] += 1.0;
	return slot;
}

void TagLedger::Remove(size_t slot, double bytes)
{
	m_bytes[slot] -= bytes;
	m_objects[slot] -= 1.0;
}

double TagLedger::TotalBytes() const
{
	double t = 0.0;
	for (const double b : m_bytes) t += b;
	return t;
}

double TagLedger::TotalObjects() const
{
	double t = 0.0;
	for (const double o : m_objects) t += o;
	return t;
}

std::vector<Reading> TagLedger::Readings(const std::string &prefix) const
{
	std::vector<Reading> out;
	for (size_t i = 0; i < m_names.size(); i++)
		if (m_objects[i] != 0.0 || m_bytes[i] != 0.0) out.push_back(Reading{ prefix + m_names[i], "bytes", m_bytes[i] });
	std::stable_sort(out.begin(), out.end(), [](const Reading &a, const Reading &b) { return a.value > b.value; });
	return out;
}

void KeyedTimer::Add(const void *key, const char *name, int64_t ns)
{
	if (m_last >= m_entries.size() || m_entries[m_last].key != key)
	{
		m_last = 0;
		while (m_last < m_entries.size() && m_entries[m_last].key != key) m_last++;
		if (m_last == m_entries.size()) m_entries.push_back(Entry{ key, name != nullptr ? name : "unnamed", 0, 0 });
	}
	m_entries[m_last].ns += ns;
	m_entries[m_last].calls++;
}

std::vector<Reading> KeyedTimer::Take(const std::string &prefix, size_t top)
{
	std::vector<Reading> out;
	for (Entry &e : m_entries)
	{
		if (e.calls > 0) out.push_back(Reading{ prefix + e.name, "ms", e.ns / 1e6 });
		e.ns = 0;
		e.calls = 0;
	}
	std::stable_sort(out.begin(), out.end(), [](const Reading &a, const Reading &b) { return a.value > b.value; });
	if (out.size() > top) out.resize(top);
	return out;
}

double ComputeTextureBytes(const TextureShape &s)
{
	double total = 0.0;
	int w = std::max(1, s.width), h = std::max(1, s.height), d = std::max(1, s.depth);
	const bool blocks = s.blockWidth > 0 && s.blockHeight > 0;
	for (int m = 0; m < std::max(1, s.mips); m++)
	{
		const double level = blocks ? std::ceil((double)w / s.blockWidth) * std::ceil((double)h / s.blockHeight) * s.blockBytes
			: (double)w * h * s.bytesPerTexel;
		total += level * d;
		w = std::max(1, w / 2);
		h = std::max(1, h / 2);
		d = std::max(1, d / 2);
	}
	return total * std::max(1, s.layers) * std::max(1, s.samples);
}

std::string ComputeMemoryTag(const std::string &name)
{
	size_t end = name.find_first_of("0123456789#[");
	if (end == std::string::npos) end = name.size();
	while (end > 0 && name[end - 1] == ' ') end--;
	return end == 0 ? std::string("unnamed") : name.substr(0, end);
}

std::string ComputeBytesText(double bytes)
{
	const double a = std::fabs(bytes);
	char buf[64];
	if (a >= 1073741824.0) std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / 1073741824.0);
	else if (a >= 1048576.0) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
	else if (a >= 1024.0) std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
	else std::snprintf(buf, sizeof(buf), "%.0f B", bytes);
	return buf;
}

double ComputeStatusField(const std::string &text, const char *key)
{
	const std::string want = std::string(key) + ":";
	for (size_t at = 0; at < text.size();)
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		if (text.compare(at, want.size(), want) == 0)
		{
			const std::string rest = text.substr(at + want.size(), end - at - want.size());
			char *after = nullptr;
			const double value = std::strtod(rest.c_str(), &after);
			if (after == rest.c_str()) return -1.0;
			const size_t unit = rest.find_first_not_of(" \t", (size_t)(after - rest.c_str()));
			return unit != std::string::npos && rest.compare(unit, 2, "kB") == 0 ? value * 1024.0 : value;
		}
		at = end + 1;
	}
	return -1.0;
}

std::vector<Reading> ComputeTabbedReadings(const std::string &text, const std::string &prefix, const std::string &unit)
{
	std::vector<Reading> out;
	for (size_t at = 0; at < text.size();)
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		const size_t tab = text.find('\t', at);
		if (tab != std::string::npos && tab < end && tab > at)
		{
			const std::string value = text.substr(tab + 1, end - tab - 1);
			char *after = nullptr;
			const double v = std::strtod(value.c_str(), &after);
			if (after != value.c_str()) out.push_back(Reading{ prefix + text.substr(at, tab - at), unit, v });
		}
		at = end + 1;
	}
	return out;
}

std::string ComputeFlightSummary(const FrameRing &seconds, int frameColumn, const std::vector<SummaryGauge> &gauges, int lastSeconds, int hitches)
{
	const int n = std::min(std::max(lastSeconds, 0), seconds.Rows());
	char buf[128];
	std::snprintf(buf, sizeof(buf), "telemetry: last %d s, %d hitch%s\n", n, hitches, hitches == 1 ? "" : "es");
	std::string out = buf;
	if (n == 0) return out;
	out += "frame ms each second, mean/max, oldest first:";
	for (int ago = n - 1; ago >= 0; ago--)
	{
		std::snprintf(buf, sizeof(buf), " %.1f/%.1f", seconds.Row(ago)[frameColumn * 2], seconds.Row(ago)[frameColumn * 2 + 1]);
		out += buf;
	}
	out += "\n";
	for (const SummaryGauge &g : gauges)
	{
		const double first = seconds.Row(n - 1)[g.column * 2], last = seconds.Row(0)[g.column * 2];
		out += g.name + ": " + Number(first, g.unit) + " -> " + Number(last, g.unit) + " (" + (last >= first ? "+" : "") + Number(last - first, g.unit) + ")\n";
	}
	return out;
}

}} // namespace zx::telemetry
