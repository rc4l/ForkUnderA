// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The registry behind telemetry.h: counters and regions, the flight recorder (frames and seconds), hitches, captures, snapshots and soak marks, and the console commands that read them.

#include "features/telemetry/telemetry.h"
#include "features/telemetry/osprobe.h"
#include "features/json/computation/json_compute.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "doomtype.h"
#include "stats.h"
#include "zstring.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>

CUSTOM_CVAR(Bool, fua_telemetry, true, CVAR_ARCHIVE)
{
	zx::telemetry::g_enabled = self;
}
CVAR(Float, fua_telemetry_hitch_ms, 8.f, CVAR_ARCHIVE)

namespace zx { namespace telemetry {

bool g_enabled = true;

namespace {

const int kMaxCounters = 256;
const int kFrameRows = 2048;
const int kSecondRows = 600;
const size_t kHitchKeep = 64;
const size_t kTraceEventCap = 2000000;
const size_t kCaptureFrameCap = 200000;
const int kStatsFrames = 240;

struct Slot
{
	std::string name, unit;
	CounterKind kind = CounterKind::Gauge;
	int intervalMs = 0;
	bool soaked = true;
	double (*read)() = nullptr;
	// [rc4l] Timings in nanoseconds and totals in thousandths, added from any thread.
	std::atomic<int64_t> accum{ 0 };
	double setValue = 0.0;
	bool hasSet = false;
	double level = 0.0;
	int64_t lastReadMs = -1000000;
	bool available = true;
};

struct RawEvent { int counter; int64_t start, end; };

struct ThreadBuffer
{
	int id;
	std::string name;
	std::mutex lock;
	std::vector<RawEvent> events;
};

struct Hitch { int64_t frame; double ms; std::vector<std::pair<int, float>> top; };

struct Registry
{
	Slot slots[kMaxCounters];
	std::atomic<int> count{ 0 };
	std::mutex registerLock;
	std::vector<std::pair<std::string, void (*)(std::vector<Reading> &)>> breakdowns;
	FrameRing frames{ kFrameRows, kMaxCounters };
	FrameRing seconds{ kSecondRows, kMaxCounters * 2 };
	double secondSum[kMaxCounters] = {}, secondMax[kMaxCounters] = {};
	int secondFrames = 0;
	int64_t secondStartNs = 0;
	int64_t frameIndex = 0, lastMarkNs = 0;
	float row[kMaxCounters] = {};
	Counter frameMs = -1, frameTics = -1;
	double medianMs = 0.0;
	std::vector<Hitch> hitches;
	std::vector<int64_t> hitchTimesNs;
	std::mutex threadLock;
	std::vector<std::unique_ptr<ThreadBuffer>> threads;
	bool mainNamed = false;
	std::atomic<bool> tracing{ false };
	bool capturing = false;
	std::string captureName;
	int64_t captureStartNs = 0;
	std::vector<float> captureRows;
	std::vector<int> captureWidths;
	std::vector<int64_t> captureFrames;
	std::vector<double> captureTimes;
	std::vector<TraceEvent> traceEvents;
	std::map<std::string, std::vector<Reading>> snaps;
	std::vector<std::vector<Reading>> soakMarks;
	std::string watchPrefix;
	std::string crashSummary = "telemetry: no frames yet\n";
	void (*offer)(const char *) = nullptr;
	void (*summaryListener)(const char *) = nullptr;
	OsReadings os;
	int64_t osReadMs = -1000000;
};

Counter RegisterLocked(Registry &r, const std::string &name, const char *unit, CounterKind kind, int intervalMs, double (*read)(), bool soaked = true)
{
	const int n = r.count.load(std::memory_order_relaxed);
	for (int i = 0; i < n; i++) if (r.slots[i].name == name) return i;
	if (n >= kMaxCounters) return -1;
	Slot &s = r.slots[n];
	s.name = name;
	s.unit = unit != nullptr ? unit : "";
	s.kind = kind;
	s.intervalMs = intervalMs;
	s.soaked = soaked;
	s.read = read;
	r.count.store(n + 1, std::memory_order_release);
	return n;
}

Registry *MakeRegistry()
{
	Registry *r = new Registry;
	r->frameMs = RegisterLocked(*r, "frame/ms", "ms", CounterKind::Time, 0, nullptr);
	r->frameTics = RegisterLocked(*r, "frame/tics", "tics", CounterKind::Total, 0, nullptr);
	return r;
}

// [rc4l] The registry, made on first use and never destroyed, since regions run during static construction and destruction.
Registry &R()
{
	static Registry *r = MakeRegistry();
	return *r;
}

ThreadBuffer *CurrentBuffer()
{
	thread_local ThreadBuffer *buffer = nullptr;
	if (buffer != nullptr) return buffer;
	Registry &r = R();
	std::lock_guard<std::mutex> g(r.threadLock);
	r.threads.emplace_back(new ThreadBuffer);
	buffer = r.threads.back().get();
	buffer->id = (int)r.threads.size() - 1;
	buffer->name = "thread " + std::to_string(buffer->id);
	return buffer;
}

double Sample(Slot &s, int64_t nowMs)
{
	double v = 0.0;
	switch (s.kind)
	{
	case CounterKind::Time:
		v = s.accum.exchange(0, std::memory_order_relaxed) / 1e6;
		if (s.hasSet) v = s.setValue;
		break;
	case CounterKind::Total:
		v = s.accum.exchange(0, std::memory_order_relaxed) / 1000.0;
		if (s.hasSet) v = s.setValue;
		break;
	case CounterKind::Gauge:
		if (s.read != nullptr && (s.intervalMs <= 0 || nowMs - s.lastReadMs >= s.intervalMs))
		{
			const double x = s.read();
			s.available = x >= 0.0;
			s.level = s.available ? x : 0.0;
			s.lastReadMs = nowMs;
		}
		v = s.level;
		break;
	}
	s.hasSet = false;
	return v;
}

// [rc4l] Every available gauge and every breakdown reading, what a snapshot holds, or for a soak mark only the gauges a soak judges.
std::vector<Reading> Snapshot(bool soak = false)
{
	Registry &r = R();
	std::vector<Reading> out;
	const int n = r.count.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (r.slots[i].kind == CounterKind::Gauge && r.slots[i].available && (!soak || r.slots[i].soaked)) out.push_back(Reading{ r.slots[i].name, r.slots[i].unit, r.slots[i].level });
	for (auto &b : r.breakdowns) b.second(out);
	return out;
}

// [rc4l] Counters a soak judges: levels of memory and of things held, not rates.
bool SoakUnit(const std::string &unit)
{
	return unit == "bytes" || unit == "objects" || unit == "handles" || unit == "threads";
}

std::string Stamp()
{
	char buf[32];
	const std::time_t now = std::time(nullptr);
	std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&now));
	return buf;
}

std::string FilePath(const std::string &name)
{
	const FString dir = progdir + "telemetry/";
	CreatePath(dir.GetChars());
	return std::string(dir.GetChars()) + name;
}

void WriteFile(const std::string &path, const std::string &text)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) { Printf("telemetry: cannot write %s\n", path.c_str()); return; }
	std::fwrite(text.data(), 1, text.size(), f);
	std::fclose(f);
	Registry &r = R();
	if (r.offer != nullptr) r.offer(path.c_str());
	else Printf("telemetry: wrote %s\n", path.c_str());
}

std::vector<std::string> Names(int n)
{
	std::vector<std::string> names;
	for (int i = 0; i < n; i++) names.push_back(R().slots[i].name);
	return names;
}

void RefreshCrashSummary(Registry &r, int64_t nowNs)
{
	static const char *const kGauges[] = { "mem/private_bytes", "mem/heap_bytes", "mem/engine_heap_bytes", "gpu/process_bytes", "gpu/mem/total_bytes", "census/actors", "os/handles" };
	std::vector<SummaryGauge> gauges;
	for (const char *name : kGauges)
	{
		const Counter c = Find(name);
		if (c >= 0 && r.slots[c].available) gauges.push_back(SummaryGauge{ name, r.slots[c].unit, c });
	}
	int recent = 0;
	for (const int64_t t : r.hitchTimesNs) if (nowNs - t < 60000000000LL) recent++;
	r.crashSummary = ComputeFlightSummary(r.seconds, r.frameMs, gauges, 30, recent);
	if (r.summaryListener != nullptr) r.summaryListener(r.crashSummary.c_str());
}

void DetectHitch(Registry &r, int n, int64_t nowNs)
{
	if (r.frameIndex % 32 == 0 && r.frames.Rows() >= 32) r.medianMs = ComputeColumnStats(r.frames, r.frameMs, 128).p50;
	const double ms = r.row[r.frameMs];
	if (r.medianMs <= 0.0 || !ComputeIsHitch(ms, r.medianMs, fua_telemetry_hitch_ms)) return;
	Hitch h{ r.frameIndex, ms, {} };
	for (int i = 0; i < n; i++)
		if (i != r.frameMs && r.slots[i].kind == CounterKind::Time && r.row[i] >= 0.05f) h.top.push_back({ i, r.row[i] });
	std::sort(h.top.begin(), h.top.end(), [](const std::pair<int, float> &a, const std::pair<int, float> &b) { return a.second > b.second; });
	if (h.top.size() > 8) h.top.resize(8);
	r.hitches.push_back(h);
	r.hitchTimesNs.push_back(nowNs);
	if (r.hitches.size() > kHitchKeep) r.hitches.erase(r.hitches.begin());
	if (r.hitchTimesNs.size() > 4096) r.hitchTimesNs.erase(r.hitchTimesNs.begin());
}

void DrainTrace(Registry &r)
{
	std::lock_guard<std::mutex> g(r.threadLock);
	for (auto &t : r.threads)
	{
		std::lock_guard<std::mutex> tl(t->lock);
		for (const RawEvent &e : t->events)
			if (r.traceEvents.size() < kTraceEventCap)
				r.traceEvents.push_back(TraceEvent{ r.slots[e.counter].name, t->id, (e.start - r.captureStartNs) / 1000.0, (e.end - e.start) / 1000.0 });
		t->events.clear();
	}
}

void StopCapture(Registry &r)
{
	r.capturing = false;
	r.tracing = false;
	DrainTrace(r);
	const int n = r.count.load(std::memory_order_acquire);
	std::string csv = ComputeCsvHeader(Names(n));
	std::vector<float> row((size_t)n, 0.f);
	size_t at = 0;
	for (size_t f = 0; f < r.captureFrames.size(); f++)
	{
		std::fill(row.begin(), row.end(), 0.f);
		for (int c = 0; c < r.captureWidths[f]; c++) row[(size_t)c] = r.captureRows[at + (size_t)c];
		at += (size_t)r.captureWidths[f];
		csv += ComputeCsvRow(r.captureFrames[f], r.captureTimes[f], row.data(), n);
	}
	std::vector<std::string> threadNames;
	{
		std::lock_guard<std::mutex> g(r.threadLock);
		for (auto &t : r.threads) threadNames.push_back(t->name);
	}
	WriteFile(FilePath(r.captureName + ".csv"), csv);
	WriteFile(FilePath(r.captureName + "-trace.json"), ComputeChromeTrace(r.traceEvents, threadNames));
	Printf("telemetry: captured %d frames (%.1f s), %d timed regions\n", (int)r.captureFrames.size(),
		r.captureTimes.empty() ? 0.0 : r.captureTimes.back() / 1000.0, (int)r.traceEvents.size());
	r.captureRows.clear(); r.captureWidths.clear(); r.captureFrames.clear(); r.captureTimes.clear(); r.traceEvents.clear();
}

std::vector<StatsRow> StatsRows(const std::string &prefix)
{
	Registry &r = R();
	std::vector<StatsRow> rows;
	const int n = r.count.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++)
	{
		const Slot &s = r.slots[i];
		if (s.name.compare(0, prefix.size(), prefix) != 0) continue;
		// [rc4l] A level reads as its last value, but a gauge in ms is a time measured late (a GPU pass), whose spread over the frames is what matters.
		const bool level = s.kind == CounterKind::Gauge && s.unit != "ms";
		rows.push_back(StatsRow{ s.name, s.unit, ComputeColumnStats(r.frames, i, level ? 1 : kStatsFrames), s.available });
	}
	std::vector<Reading> extra;
	for (auto &b : r.breakdowns) if (b.first.compare(0, prefix.size(), prefix) == 0 || prefix.compare(0, b.first.size(), b.first) == 0) b.second(extra);
	for (const Reading &x : extra)
	{
		if (x.name.compare(0, prefix.size(), prefix) != 0) continue;
		ColumnStats st = {};
		st.n = 1; st.last = st.mean = st.min = st.max = st.p50 = st.p95 = st.p99 = x.value;
		rows.push_back(StatsRow{ x.name, x.unit, st, true });
	}
	return rows;
}

std::string HitchText(Registry &r, size_t limit)
{
	std::string out;
	const size_t from = r.hitches.size() > limit ? r.hitches.size() - limit : 0;
	char buf[96];
	for (size_t i = from; i < r.hitches.size(); i++)
	{
		const Hitch &h = r.hitches[i];
		std::snprintf(buf, sizeof(buf), "frame %lld: %.2f ms (%lld frames ago):", (long long)h.frame, h.ms, (long long)(r.frameIndex - h.frame));
		out += buf;
		for (const auto &t : h.top)
		{
			std::snprintf(buf, sizeof(buf), " %s %.2f,", r.slots[t.first].name.c_str(), t.second);
			out += buf;
		}
		if (!h.top.empty()) out.pop_back();
		out += "\n";
	}
	return out.empty() ? std::string("no hitches yet\n") : out;
}

// [rc4l] The OS probe's readings, taken at most a few times a second however many gauges ask.
const OsReadings &Os()
{
	Registry &r = R();
	const int64_t nowMs = NowNs() / 1000000;
	if (nowMs - r.osReadMs >= 400)
	{
		r.os = OsReadings();
		ReadOs(r.os);
		r.osReadMs = nowMs;
	}
	return r.os;
}

double OsPrivate() { return Os().privateBytes; }
double OsResident() { return Os().residentBytes; }
double OsPeak() { return Os().peakBytes; }
double OsHeap() { return Os().heapBytes; }
double OsHandles() { return Os().handles; }
double OsThreads() { return Os().threads; }
double OsGpu() { return Os().gpuProcessBytes; }
double OsJsHeap() { return Os().jsHeapBytes; }

// [rc4l] The probe's gauges, each read twice a second.
const bool s_osRegistered = (
	Register(CounterSpec{ "mem/private_bytes", "bytes", CounterKind::Gauge, 500 }, OsPrivate),
	Register(CounterSpec{ "mem/resident_bytes", "bytes", CounterKind::Gauge, 500, false }, OsResident),
	Register(CounterSpec{ "mem/peak_bytes", "bytes", CounterKind::Gauge, 500, false }, OsPeak),
	Register(CounterSpec{ "mem/heap_bytes", "bytes", CounterKind::Gauge, 500 }, OsHeap),
	Register(CounterSpec{ "os/handles", "handles", CounterKind::Gauge, 500 }, OsHandles),
	Register(CounterSpec{ "os/threads", "threads", CounterKind::Gauge, 500 }, OsThreads),
	Register(CounterSpec{ "gpu/process_bytes", "bytes", CounterKind::Gauge, 500 }, OsGpu),
	Register(CounterSpec{ "mem/js_heap_bytes", "bytes", CounterKind::Gauge, 500 }, OsJsHeap),
	true);

} // namespace

int64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Counter Register(const CounterSpec &spec, double (*read)())
{
	Registry &r = R();
	std::lock_guard<std::mutex> g(r.registerLock);
	return RegisterLocked(r, spec.name, spec.unit, spec.kind, spec.intervalMs, read, spec.soaked);
}

Counter Find(const char *name)
{
	Registry &r = R();
	const int n = r.count.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++) if (r.slots[i].name == name) return i;
	return -1;
}

void Add(Counter c, double amount)
{
	if (c < 0) return;
	Slot &s = R().slots[c];
	if (s.kind == CounterKind::Time) s.accum.fetch_add((int64_t)(amount * 1e6), std::memory_order_relaxed);
	else if (s.kind == CounterKind::Total) s.accum.fetch_add((int64_t)(amount * 1000.0), std::memory_order_relaxed);
	else s.level += amount;
}

void Set(Counter c, double value)
{
	if (c < 0) return;
	Slot &s = R().slots[c];
	s.setValue = value;
	s.hasSet = true;
	if (s.kind == CounterKind::Gauge)
	{
		s.available = value >= 0.0;
		s.level = s.available ? value : 0.0;
	}
}

void RegisterBreakdown(const char *name, void (*read)(std::vector<Reading> &out))
{
	Registry &r = R();
	std::lock_guard<std::mutex> g(r.registerLock);
	r.breakdowns.push_back({ name, read });
}

ZoneSite::ZoneSite(const char *n) : counter(Register(CounterSpec{ n, "ms", CounterKind::Time, 0 })), name(n)
{
}

void Region(Counter c, int64_t startNs, int64_t endNs)
{
	if (c < 0 || !g_enabled) return;
	Registry &r = R();
	r.slots[c].accum.fetch_add(endNs - startNs, std::memory_order_relaxed);
	if (!r.tracing.load(std::memory_order_relaxed)) return;
	ThreadBuffer *b = CurrentBuffer();
	std::lock_guard<std::mutex> g(b->lock);
	b->events.push_back(RawEvent{ c, startNs, endNs });
}

void ZoneScope::Close()
{
	Region(m_site->counter, m_start, NowNs());
}

void NameThisThread(const char *name)
{
	ThreadBuffer *b = CurrentBuffer();
	std::lock_guard<std::mutex> g(R().threadLock);
	b->name = name;
}

void TicMark()
{
	Add(R().frameTics, 1.0);
}

void FrameMark()
{
	Registry &r = R();
	if (!r.mainNamed) { NameThisThread("main"); r.mainNamed = true; }
	const int64_t now = NowNs();
	const int64_t nowMs = now / 1000000;
	if (r.lastMarkNs != 0) Set(r.frameMs, (now - r.lastMarkNs) / 1e6);
	if (r.secondStartNs == 0) r.secondStartNs = now;
	const int n = r.count.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++) r.row[i] = (float)Sample(r.slots[i], nowMs);
	r.frames.Push(r.frameIndex, r.row, n);
	for (int i = 0; i < n; i++)
	{
		r.secondSum[i] += r.row[i];
		r.secondMax[i] = r.secondFrames == 0 ? r.row[i] : std::max(r.secondMax[i], (double)r.row[i]);
	}
	r.secondFrames++;
	if (now - r.secondStartNs >= 1000000000LL)
	{
		float pairs[kMaxCounters * 2];
		for (int i = 0; i < n; i++)
		{
			pairs[i * 2] = (float)(r.secondSum[i] / r.secondFrames);
			pairs[i * 2 + 1] = (float)r.secondMax[i];
			r.secondSum[i] = 0.0;
		}
		r.seconds.Push(r.frameIndex, pairs, n * 2);
		r.secondFrames = 0;
		r.secondStartNs = now;
		RefreshCrashSummary(r, now);
	}
	DetectHitch(r, n, now);
	if (r.capturing)
	{
		r.captureFrames.push_back(r.frameIndex);
		r.captureTimes.push_back((now - r.captureStartNs) / 1e6);
		r.captureWidths.push_back(n);
		r.captureRows.insert(r.captureRows.end(), r.row, r.row + n);
		DrainTrace(r);
		if (r.captureFrames.size() >= kCaptureFrameCap) StopCapture(r);
	}
	r.frameIndex++;
	r.lastMarkNs = now;
}

void SetFileOfferer(void (*offer)(const char *path))
{
	R().offer = offer;
}

std::string StatsJson(const char *prefix)
{
	return ComputeStatsJson(StatsRows(prefix != nullptr ? prefix : ""));
}

const char *CrashSummary()
{
	return R().crashSummary.c_str();
}

void SetSummaryListener(void (*listen)(const char *summary))
{
	R().summaryListener = listen;
}

}} // namespace zx::telemetry

using namespace zx::telemetry;

// [rc4l] The counters whose names start with a prefix (all of them without one), as a table, as JSON, or on screen.
CCMD(fua_stats)
{
	const bool json = argv.argc() > 1 && !strcmp(argv[1], "json");
	const bool watch = argv.argc() > 1 && !strcmp(argv[1], "watch");
	const int first = (json || watch) ? 2 : 1;
	const std::string prefix = argv.argc() > first ? argv[first] : "";
	if (watch)
	{
		FStat *stat = FStat::FindStat("telemetry");
		const bool off = prefix == "off";
		R().watchPrefix = off ? "" : prefix;
		if (stat != nullptr && stat->isActive() == off) stat->ToggleStat();
		return;
	}
	const std::vector<StatsRow> rows = StatsRows(prefix);
	if (json) Printf("%s\n", ComputeStatsJson(rows).c_str());
	else Printf("telemetry (%s probe), last %d frames:\n%s", OsProbeName(), std::min(R().frames.Rows(), kStatsFrames), ComputeStatsTable(rows).c_str());
}

// [rc4l] Records every counter each frame and every timed region on every thread, until stopped, as <name>.csv and <name>-trace.json under telemetry/.
CCMD(fua_capture)
{
	Registry &r = R();
	const bool stop = argv.argc() > 1 && !strcmp(argv[1], "stop");
	if (stop)
	{
		if (!r.capturing) Printf("telemetry: no capture running\n");
		else StopCapture(r);
		return;
	}
	if (r.capturing) { Printf("telemetry: a capture is already running (fua_capture stop)\n"); return; }
	r.captureName = argv.argc() > 2 ? argv[2] : "capture-" + Stamp();
	r.captureStartNs = NowNs();
	r.capturing = true;
	r.tracing = true;
	Printf("telemetry: capturing to telemetry/%s.csv until fua_capture stop\n", r.captureName.c_str());
}

// [rc4l] Writes the flight recorder, the last frames, seconds and hitches, as <name>-frames.csv, <name>-seconds.csv and <name>-hitches.txt, and prints its summary.
CCMD(fua_flightdump)
{
	Registry &r = R();
	const std::string name = argv.argc() > 1 ? argv[1] : "flight-" + Stamp();
	const int n = r.count.load(std::memory_order_acquire);
	std::string frames = ComputeCsvHeader(Names(n));
	for (int ago = r.frames.Rows() - 1; ago >= 0; ago--) frames += ComputeCsvRow(r.frames.Frame(ago), 0.0, r.frames.Row(ago), n);
	std::vector<std::string> pairNames;
	for (const std::string &s : Names(n)) { pairNames.push_back(s + " mean"); pairNames.push_back(s + " max"); }
	std::string seconds = ComputeCsvHeader(pairNames);
	for (int ago = r.seconds.Rows() - 1; ago >= 0; ago--) seconds += ComputeCsvRow(r.seconds.Frame(ago), 0.0, r.seconds.Row(ago), n * 2);
	WriteFile(FilePath(name + "-frames.csv"), frames);
	WriteFile(FilePath(name + "-seconds.csv"), seconds);
	WriteFile(FilePath(name + "-hitches.txt"), HitchText(r, kHitchKeep));
	Printf("%s", r.crashSummary.c_str());
}

// [rc4l] The last slow frames, each with the timed regions it spent most in.
CCMD(fua_hitches)
{
	const int n = argv.argc() > 1 ? atoi(argv[1]) : 10;
	Printf("%s", HitchText(R(), (size_t)std::max(1, n)).c_str());
}

// [rc4l] Keeps every gauge and breakdown under a name ("last" without one) for fua_memdiff.
CCMD(fua_memsnap)
{
	const std::string name = argv.argc() > 1 ? argv[1] : "last";
	std::vector<Reading> &snap = R().snaps[name] = Snapshot();
	Printf("telemetry: snapshot %s holds %d readings\n", name.c_str(), (int)snap.size());
}

// [rc4l] What changed between two named snapshots, or between one ("last" without a name) and now.
CCMD(fua_memdiff)
{
	Registry &r = R();
	const std::string a = argv.argc() > 1 ? argv[1] : "last";
	const std::string b = argv.argc() > 2 ? argv[2] : "";
	for (const std::string &name : { a, b })
		if (!name.empty() && r.snaps.count(name) == 0) { Printf("telemetry: no snapshot %s (fua_memsnap %s)\n", name.c_str(), name.c_str()); return; }
	const std::vector<DiffRow> rows = ComputeSnapshotDiff(r.snaps[a], b.empty() ? Snapshot() : r.snaps[b]);
	Printf("%s", ComputeDiffTable(rows, 40).c_str());
}

// [rc4l] A soak: begin, a mark after each repetition of an action, and the verdict on what kept growing.
CCMD(fua_soak)
{
	Registry &r = R();
	const std::string what = argv.argc() > 1 ? argv[1] : "";
	if (what == "begin") { r.soakMarks.clear(); Printf("telemetry: soak begun\n"); return; }
	if (what == "mark") { r.soakMarks.push_back(Snapshot(true)); Printf("telemetry: soak mark %d\n", (int)r.soakMarks.size()); return; }
	if (what != "verdict") { Printf("usage: fua_soak begin | mark | verdict [warmup] [json]\n"); return; }
	const int warmup = argv.argc() > 2 && strcmp(argv[2], "json") != 0 ? atoi(argv[2]) : 1;
	const bool json = argv.argc() > 2 && !strcmp(argv[argv.argc() - 1], "json");
	std::vector<std::string> grew;
	std::string details;
	int judged = 0;
	if (!r.soakMarks.empty())
	{
		for (const Reading &first : r.soakMarks[0])
		{
			if (!SoakUnit(first.unit)) continue;
			std::vector<double> series;
			for (const std::vector<Reading> &mark : r.soakMarks)
			{
				const auto it = std::find_if(mark.begin(), mark.end(), [&](const Reading &x) { return x.name == first.name; });
				if (it == mark.end()) break;
				series.push_back(it->value);
			}
			if (series.size() != r.soakMarks.size()) continue;
			judged++;
			const double floor = first.unit == "bytes" ? 512.0 * 1024.0 : 4.0;
			const GrowthVerdict v = ComputeGrowthVerdict(series, warmup, floor, 0.01);
			if (!v.growing) continue;
			grew.push_back(first.name);
			char perCycle[32], total[32], buf[256];
			std::snprintf(perCycle, sizeof(perCycle), "%.1f", v.perCycle);
			std::snprintf(total, sizeof(total), "%.1f", v.total);
			std::snprintf(buf, sizeof(buf), "  %s grew %s a cycle (%s over %d cycles)\n", first.name.c_str(),
				first.unit == "bytes" ? ComputeBytesText(v.perCycle).c_str() : perCycle,
				first.unit == "bytes" ? ComputeBytesText(v.total).c_str() : total, v.cycles);
			details += buf;
		}
	}
	const bool pass = grew.empty() && r.soakMarks.size() >= (size_t)warmup + 3;
	if (json)
	{
		std::string list;
		for (size_t i = 0; i < grew.size(); i++) list += (i ? "," : "") + zx::json::ComputeJsonQuoted(grew[i]);
		Printf("{\"pass\":%s,\"marks\":%d,\"judged\":%d,\"grew\":[%s]}\n", pass ? "true" : "false", (int)r.soakMarks.size(), judged, list.c_str());
		return;
	}
	Printf("SOAK %s: %d marks, %d counters judged, %d grew\n%s", pass ? "PASS" : "FAIL", (int)r.soakMarks.size(), judged, (int)grew.size(), details.c_str());
}

// [rc4l] The counters fua_stats watch picked, in the stat overlay's corner.
ADD_STAT(telemetry)
{
	FString out;
	int shown = 0;
	for (const StatsRow &row : StatsRows(R().watchPrefix))
	{
		if (shown++ == 16) break;
		char number[32];
		std::snprintf(number, sizeof(number), "%.2f", row.stats.last);
		const std::string last = row.unit == "bytes" ? ComputeBytesText(row.stats.last) : number;
		out.AppendFormat("%s %s\n", row.name.c_str(), row.available ? last.c_str() : "n/a");
	}
	return out;
}
