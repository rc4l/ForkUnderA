// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The registry behind telemetry.h: counters and regions, the flight recorder (frames and seconds), hitches, captures, snapshots and soak marks, and the console commands that read them.

#include "features/telemetry/telemetry.h"
#include "features/telemetry/osprobe.h"

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
	// [rc4l] How often a gauge's read runs, in milliseconds.
	Throttle every;
	bool soaked = true;
	double (*read)() = nullptr;
	// [rc4l] Timings in nanoseconds and totals in thousandths, added from any thread and never reset, a frame's value being what was added since the last sample.
	std::atomic<int64_t> accum{ 0 };
	int64_t sampled = 0;
	double setValue = 0.0;
	bool hasSet = false;
	double level = 0.0;
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

struct Registry
{
	Slot slots[kMaxCounters];
	std::atomic<int> count{ 0 };
	std::mutex registerLock;
	struct Breakdown { std::string name; void (*read)(std::vector<Reading> &); bool soaked; };
	std::vector<Breakdown> breakdowns;
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
	bool soaking = false;   // [rc4l] from fua_soak begin to its verdict, while the heaps are walked
	std::string watchPrefix;
	std::string crashSummary = "telemetry: no frames yet\n";
	void (*offer)(const char *) = nullptr;
	void (*summaryListener)(const char *) = nullptr;
	OsReadings os;
	// [rc4l] The OS probe runs at most a few times a second however many gauges ask.
	Throttle osEvery{ 400 };
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
	s.every = Throttle(intervalMs);
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

// [rc4l] What was added to a timing or a total since its last sample.
int64_t TakeAdded(Slot &s)
{
	const int64_t now = s.accum.load(std::memory_order_relaxed);
	const int64_t added = now - s.sampled;
	s.sampled = now;
	return added;
}

// [rc4l] A gauge's read taken into its level, unavailable when it reads below zero.
void ReadGauge(Slot &s)
{
	const double x = s.read();
	s.available = x >= 0.0;
	s.level = s.available ? x : 0.0;
}

double Sample(Slot &s, int64_t nowMs)
{
	double v = 0.0;
	switch (s.kind)
	{
	case CounterKind::Time:
		v = TakeAdded(s) / 1e6;
		if (s.hasSet) v = s.setValue;
		break;
	case CounterKind::Total:
		v = TakeAdded(s) / 1000.0;
		if (s.hasSet) v = s.setValue;
		break;
	case CounterKind::Gauge:
		if (s.read != nullptr && s.every.Due(nowMs)) ReadGauge(s);
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
	for (auto &b : r.breakdowns) if (!soak || b.soaked) b.read(out);
	return out;
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
	std::vector<bool> timed((size_t)n);
	for (int i = 0; i < n; i++) timed[(size_t)i] = r.slots[i].kind == CounterKind::Time;
	r.hitches.push_back(Hitch{ r.frameIndex, ms, ComputeHitchTop(r.row, timed, r.frameMs, 0.05f, 8) });
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
	const std::string csv = ComputeCaptureCsv(Names(r.count.load(std::memory_order_acquire)), r.captureFrames, r.captureTimes, r.captureWidths, r.captureRows);
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
		if (!ComputeStartsWith(s.name, prefix)) continue;
		// [rc4l] A level reads as its last value, but a gauge in ms is a time measured late (a GPU pass), whose spread over the frames is what matters.
		const bool level = s.kind == CounterKind::Gauge && s.unit != "ms";
		rows.push_back(StatsRow{ s.name, s.unit, ComputeColumnStats(r.frames, i, level ? 1 : kStatsFrames), s.available });
	}
	// [rc4l] A breakdown is read when its readings can fall under the prefix, whichever of the two names the other starts with.
	std::vector<Reading> extra;
	for (auto &b : r.breakdowns) if (ComputeStartsWith(b.name, prefix) || ComputeStartsWith(prefix, b.name)) b.read(extra);
	const std::vector<StatsRow> readings = ComputeReadingRows(extra, prefix);
	rows.insert(rows.end(), readings.begin(), readings.end());
	return rows;
}

std::string HitchText(Registry &r, size_t limit)
{
	return ComputeHitchText(r.hitches, Names(r.count.load(std::memory_order_acquire)), r.frameIndex, limit);
}

// [rc4l] Every heap walked for the heap gauges, which stalls a frame for tens of milliseconds twice a second in a big level, so only this or a soak asks for it.
CVAR(Bool, fua_telemetry_heaps, false, 0)

// [rc4l] The OS readings, at once with fresh, else no more than every osEvery; the heaps with them only when a soak, a snapshot or fua_telemetry_heaps wants them.
const OsReadings &Os(bool fresh = false)
{
	Registry &r = R();
	if (r.osEvery.Due(NowNs() / 1000000) || fresh)
	{
		r.os = OsReadings();
		ReadOs(r.os, fresh || r.soaking || fua_telemetry_heaps);
	}
	return r.os;
}

// [rc4l] The OS readings with the heaps and every gauge read now, for a snapshot or a soak mark that has to hold what this moment holds.
void ReadEverythingNow()
{
	Os(true);
	Registry &r = R();
	const int n = r.count.load(std::memory_order_acquire);
	for (int i = 0; i < n; i++)
		if (r.slots[i].kind == CounterKind::Gauge && r.slots[i].read != nullptr) ReadGauge(r.slots[i]);
}

double OsPrivate() { return Os().privateBytes; }
double OsResident() { return Os().residentBytes; }
double OsPeak() { return Os().peakBytes; }
double OsHeap() { return Os().heapBytes; }
double OsAllHeaps() { return Os().allHeapsBytes; }
double OsHeapsCommitted() { return Os().heapsCommittedBytes; }
double OsHandles() { return Os().handles; }
double OsThreads() { return Os().threads; }
double OsGpu() { return Os().gpuProcessBytes; }
double OsJsHeap() { return Os().jsHeapBytes; }

// [rc4l] The probe's gauges, each read twice a second; a soak leaves out the working set, its peak and the heaps' committed bytes, which settle after a restart rather than leak.
const bool s_osRegistered = (
	Register(CounterSpec{ "mem/private_bytes", "bytes", CounterKind::Gauge, 500 }, OsPrivate),
	Register(CounterSpec{ "mem/resident_bytes", "bytes", CounterKind::Gauge, 500, false }, OsResident),
	Register(CounterSpec{ "mem/peak_bytes", "bytes", CounterKind::Gauge, 500, false }, OsPeak),
	Register(CounterSpec{ "mem/heap_bytes", "bytes", CounterKind::Gauge, 500 }, OsHeap),
	Register(CounterSpec{ "mem/all_heaps_bytes", "bytes", CounterKind::Gauge, 500 }, OsAllHeaps),
	Register(CounterSpec{ "mem/heaps_committed_bytes", "bytes", CounterKind::Gauge, 500, false }, OsHeapsCommitted),
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

int64_t TimedNs(Counter c)
{
	return c < 0 ? 0 : R().slots[c].accum.load(std::memory_order_relaxed);
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

void RegisterBreakdown(const char *name, void (*read)(std::vector<Reading> &out), bool soaked)
{
	Registry &r = R();
	std::lock_guard<std::mutex> g(r.registerLock);
	r.breakdowns.push_back(Registry::Breakdown{ name, read, soaked });
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
	{
		// [rc4l] The gauges' reads timed, since a slow one stalls every frame it falls due in and no other region would name it.
		ZX_ZONE("telemetry/gauges");
		for (int i = 0; i < n; i++) r.row[i] = (float)Sample(r.slots[i], nowMs);
	}
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
	const std::vector<std::string> names = Names(r.count.load(std::memory_order_acquire));
	WriteFile(FilePath(name + "-frames.csv"), ComputeRingCsv(r.frames, names));
	WriteFile(FilePath(name + "-seconds.csv"), ComputeRingCsv(r.seconds, ComputeSecondNames(names)));
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
	ReadEverythingNow();
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
	if (b.empty()) ReadEverythingNow();
	const std::vector<DiffRow> rows = ComputeSnapshotDiff(r.snaps[a], b.empty() ? Snapshot() : r.snaps[b]);
	Printf("%s", ComputeDiffTable(rows, 40).c_str());
}

// [rc4l] A soak: begin, a mark after each repetition of an action, and the verdict on what kept growing.
CCMD(fua_soak)
{
	Registry &r = R();
	const std::string what = argv.argc() > 1 ? argv[1] : "";
	if (what == "begin") { r.soakMarks.clear(); r.soaking = true; Printf("telemetry: soak begun\n"); return; }
	if (what == "mark") { ReadEverythingNow(); r.soakMarks.push_back(Snapshot(true)); Printf("telemetry: soak mark %d\n", (int)r.soakMarks.size()); return; }
	if (what != "verdict") { Printf("usage: fua_soak begin | mark | verdict [warmup] [json] [-prefix ...]\n"); return; }
	std::vector<std::string> args;
	for (int i = 2; i < argv.argc(); i++) args.push_back(argv[i]);
	const SoakVerdictArgs asked = ComputeSoakVerdictArgs(args);
	const SoakVerdict verdict = ComputeSoakVerdict(r.soakMarks, asked.warmup, asked.ignore);
	r.soaking = false;
	Printf("%s", asked.json ? (ComputeSoakJson(verdict) + "\n").c_str() : ComputeSoakText(verdict).c_str());
}

// [rc4l] The counters fua_stats watch picked, in the stat overlay's corner.
ADD_STAT(telemetry)
{
	return FString(ComputeWatchText(StatsRows(R().watchPrefix), 16).c_str());
}
