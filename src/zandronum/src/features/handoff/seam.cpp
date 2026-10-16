// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/seam.h"
#include "features/handoff/computation/lock_compute.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomtype.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace zx { namespace handoff {

namespace {

SeamRegistry &Registry()
{
	static SeamRegistry registry;
	return registry;
}

std::atomic<std::thread::id> g_mainThread;
std::atomic<std::thread::id> g_gameThread;
std::atomic<bool> g_gameRunning(false);
// [rc4l] True until the loop runs its handoff, so seams placed before it raise nothing.
std::atomic<bool> g_mainInHandoff(true);

std::atomic<const char *> g_current[2];
std::atomic<uint64_t> g_beats[2];

std::atomic<int> g_stressMaxUs(0);
std::atomic<int> g_stressSeed(1);
std::atomic<uint64_t> g_stressCount(0);

std::atomic<int> g_conflicts(0);
std::atomic<const char *> g_firstViolation(nullptr);
std::atomic<const char *> g_firstViolationThread(nullptr);
std::atomic<int> g_nestedLocks(0);
std::atomic<const char *> g_firstNested(nullptr);

// [rc4l] 0 for the main thread (and an inline tic), 1 for the game thread, -1 for any other.
int Slot()
{
	if (OnMainThread()) return 0;
	if (OnGameThread()) return 1;
	return -1;
}

void Nested(const char *taking, const char *held)
{
	(void)held;
	g_nestedLocks.fetch_add(1, std::memory_order_relaxed);
	const char *none = nullptr;
	g_firstNested.compare_exchange_strong(none, taking);
}

// [rc4l] Waits out a stress delay, spinning below two milliseconds where a sleep would oversleep.
void Delay(uint32_t us)
{
	if (us >= 2000) { std::this_thread::sleep_for(std::chrono::microseconds(us)); return; }
	const int64_t end = zx::telemetry::NowNs() + (int64_t)us * 1000;
	while (zx::telemetry::NowNs() < end) {}
}

double ReadViolations()
{
	return (double)SeamViolations();
}

} // namespace

void NoteMainThread()
{
	g_mainThread.store(std::this_thread::get_id());
	if (g_gameThread.load() == std::thread::id()) g_gameThread.store(std::this_thread::get_id());
	zx::SetLockNestedListener(Nested);
	static const zx::telemetry::Counter violations = zx::telemetry::Register(
		zx::telemetry::CounterSpec{ "handoff/seam_violations", "entries", zx::telemetry::CounterKind::Gauge, 500 }, ReadViolations);
	(void)violations;
}

void NoteGameThread()
{
	g_gameThread.store(std::this_thread::get_id());
}

bool OnMainThread()
{
	return std::this_thread::get_id() == g_mainThread.load(std::memory_order_relaxed);
}

bool OnGameThread()
{
	return std::this_thread::get_id() == g_gameThread.load(std::memory_order_relaxed);
}

void SetGameRunning(bool running)
{
	g_gameRunning.store(running);
}

void SetMainInHandoff(bool inHandoff)
{
	g_mainInHandoff.store(inHandoff);
}

Phase CurrentPhase()
{
	return Phase{ g_gameRunning.load(std::memory_order_relaxed), g_mainInHandoff.load(std::memory_order_relaxed) };
}

SeamSite::SeamSite(const char *n, Owner o, bool timed)
	: index(Registry().Declare(n, o)), name(n), owner(o),
	  counter(timed ? zx::telemetry::Register(zx::telemetry::CounterSpec{ n, "ms", zx::telemetry::CounterKind::Time, 0 }) : -1)
{
	if (index < 0) g_conflicts.fetch_add(1);
}

SeamScope::SeamScope(const SeamSite &site) : m_outer(nullptr), m_start(0), m_site(&site)
{
	const bool onMain = OnMainThread(), onGame = OnGameThread();
	const bool allowed = ComputeOwnerAllows(site.owner, onMain, onGame, CurrentPhase());
	if (site.index >= 0) Registry().Enter(site.index, allowed);
	if (!allowed)
	{
		const char *none = nullptr;
		if (g_firstViolation.compare_exchange_strong(none, site.name))
			g_firstViolationThread.store(onMain ? "main" : onGame ? "game" : "another");
	}
	const int slot = Slot();
	if (slot >= 0)
	{
		m_outer = g_current[slot].exchange(site.name, std::memory_order_relaxed);
		g_beats[slot].fetch_add(1, std::memory_order_relaxed);
	}
	const int maxUs = g_stressMaxUs.load(std::memory_order_relaxed);
	if (maxUs > 0)
	{
		const uint32_t us = ComputeStressDelayUs((uint64_t)g_stressSeed.load(), (uint32_t)(site.index < 0 ? 0 : site.index),
			g_stressCount.fetch_add(1, std::memory_order_relaxed), (uint32_t)maxUs);
		if (us > 0) Delay(us);
	}
	if (site.counter >= 0 && zx::telemetry::g_enabled) m_start = zx::telemetry::NowNs();
}

SeamScope::~SeamScope()
{
	if (m_start != 0) zx::telemetry::Region(m_site->counter, m_start, zx::telemetry::NowNs());
	const int slot = Slot();
	if (slot >= 0) g_current[slot].store(m_outer, std::memory_order_relaxed);
}

const char *CurrentSeam(bool game)
{
	return g_current[game ? 1 : 0].load(std::memory_order_relaxed);
}

uint64_t Heartbeat(bool game)
{
	return g_beats[game ? 1 : 0].load(std::memory_order_relaxed);
}

uint64_t SeamViolations()
{
	return Registry().Violations() + (uint64_t)g_nestedLocks.load() + (uint64_t)g_conflicts.load();
}

}} // namespace zx::handoff

// [rc4l] Delays about one seam entry in eight by up to this many microseconds, to shake out what only breaks when the two threads interleave differently.
CUSTOM_CVAR(Int, fua_gamethread_stress, 0, 0)
{
	zx::handoff::g_stressMaxUs.store(self < 0 ? 0 : (int)self);
}

// [rc4l] The stress mode's seed, so a run that broke can be repeated.
CUSTOM_CVAR(Int, fua_gamethread_stress_seed, 1, 0)
{
	zx::handoff::g_stressSeed.store((int)self);
}

// [rc4l] Lists every seam with its owner, entries and the entries a thread it does not allow made.
CCMD(fua_seams)
{
	Printf("%s", zx::handoff::ComputeSeamTable(zx::handoff::Registry()).c_str());
	const char *first = zx::handoff::g_firstViolation.load();
	Printf("%llu violations", (unsigned long long)zx::handoff::SeamViolations());
	if (first != nullptr) Printf(", the first in %s on the %s thread", first, zx::handoff::g_firstViolationThread.load());
	Printf("; %d locks taken while holding another", zx::handoff::g_nestedLocks.load());
	if (zx::handoff::g_firstNested.load() != nullptr) Printf(" (first %s)", zx::handoff::g_firstNested.load());
	Printf("; %d seams declared with a second owner\n", zx::handoff::g_conflicts.load());
}
