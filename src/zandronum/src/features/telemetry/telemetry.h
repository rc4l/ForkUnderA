// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one registry every feature reports its performance through: counters read or fed each frame, timed regions on any thread, a flight recorder of the last minutes, and the console commands, files and bridge views that read them (README.md).

#ifndef ZX_TELEMETRY_H
#define ZX_TELEMETRY_H

#include "features/telemetry/computation/telemetry_compute.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zx { namespace telemetry {

// [rc4l] A registered counter, its column in the frame ring, or -1 when the registry is full.
typedef int Counter;

// [rc4l] A counter's name ("group/name"), unit, kind, for a gauge how often its read runs (0 being every frame), and whether a soak judges its growth, which a peak or the OS's working set says nothing about.
struct CounterSpec { const char *name; const char *unit; CounterKind kind; int intervalMs; bool soaked = true; };

// [rc4l] Registers a counter, read through read (a gauge) or fed with Add and Set; a name registered twice returns the first counter, and a read returning a negative value marks the counter unavailable on this platform.
Counter Register(const CounterSpec &spec, double (*read)() = nullptr);
// [rc4l] The counter registered under name, or -1.
Counter Find(const char *name);
// [rc4l] The nanoseconds a timing counter has gathered since it was registered, for a reader keeping its own intervals (the bridge's perf.ticprof); 0 for no counter.
int64_t TimedNs(Counter c);

// [rc4l] Adds amount to this frame's value of a total or a timing, from any thread.
void Add(Counter c, double amount);
// [rc4l] Sets this frame's value of a total or a timing, or a gauge's level until the next Set, from the main thread.
void Set(Counter c, double value);

// [rc4l] A breakdown snapshots and fua_stats list beside the counters: readings a feature makes on request, such as memory by tag; soaked false keeps a diagnostic that restates another reading out of a soak's verdict.
void RegisterBreakdown(const char *name, void (*read)(std::vector<Reading> &out), bool soaked = true);

// [rc4l] Nanoseconds on the clock regions are timed with.
int64_t NowNs();

// [rc4l] Whether regions are timed (fua_telemetry), which a disabled region costs one load of.
extern bool g_enabled;

// [rc4l] The fixed part of a timed region, made the first time its ZX_ZONE runs.
struct ZoneSite
{
	explicit ZoneSite(const char *name);
	Counter counter;
	const char *name;
};

// [rc4l] Times its scope into its site's counter, and into the trace while one is captured.
class ZoneScope
{
public:
	explicit ZoneScope(const ZoneSite &site) : m_site(&site), m_start(g_enabled ? NowNs() : 0) {}
	~ZoneScope() { if (m_start != 0) Close(); }

private:
	void Close();
	const ZoneSite *m_site;
	int64_t m_start;
};

#define ZX_TELEMETRY_CAT2(a, b) a##b
#define ZX_TELEMETRY_CAT(a, b) ZX_TELEMETRY_CAT2(a, b)
// [rc4l] Times the rest of the scope as the timing counter name, a "group/name" string literal (tools/zone-tripwire.sh).
#define ZX_ZONE(name) \
	static const zx::telemetry::ZoneSite ZX_TELEMETRY_CAT(zxZoneSite, __LINE__)(name); \
	const zx::telemetry::ZoneScope ZX_TELEMETRY_CAT(zxZone, __LINE__)(ZX_TELEMETRY_CAT(zxZoneSite, __LINE__))

// [rc4l] Adds amount to the total counter name ("group/name", a string literal) in unit, registering it the first time it runs.
#define ZX_COUNT(name, unit, amount) do { \
	static const zx::telemetry::Counter ZX_TELEMETRY_CAT(zxCount, __LINE__) = zx::telemetry::Register(zx::telemetry::CounterSpec{ name, unit, zx::telemetry::CounterKind::Total, 0 }); \
	zx::telemetry::Add(ZX_TELEMETRY_CAT(zxCount, __LINE__), (double)(amount)); \
} while (0)

// [rc4l] A region timed by hand from startNs to endNs on NowNs's clock into a timing counter, for work whose start and end are not one scope (a job lane, a wait).
void Region(Counter c, int64_t startNs, int64_t endNs);

// [rc4l] Ends a frame: samples the gauges, rolls the frame into the flight recorder and any capture; features/framephase's FrameBoundary calls it once a frame.
void FrameMark();
// [rc4l] One game tic ran.
void TicMark();
// [rc4l] Names the calling thread in traces, such as "job 3".
void NameThisThread(const char *name);

// [rc4l] Who hands a file the registry wrote to the player, which a browser's host does by downloading it; NULL prints its path.
void SetFileOfferer(void (*offer)(const char *path));

// [rc4l] Whether the game's own state may be read now, which a gauge counting thinkers asks; the handoff installs it, since a tic in flight owns that state, and none installed means always.
void SetGameReadable(bool (*readable)());
bool GameReadable();

// [rc4l] Whether a soak mark may be taken now, which d_main installs: not while a wipe holds both screens' copies, so a mark asked for then is taken once it ends; none installed means always.
void SetSoakReady(bool (*ready)());

// [rc4l] The counters whose names start with prefix as fua_stats json prints them, for the bridge's perf.counters.
std::string StatsJson(const char *prefix);

// [rc4l] Who hears the last minute in a few lines each second, which a crash report keeps to send with a crash; NULL hears nothing.
void SetSummaryListener(void (*listen)(const char *summary));

}} // namespace zx::telemetry

#endif // ZX_TELEMETRY_H
