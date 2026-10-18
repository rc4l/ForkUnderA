// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The global allocation counter: the replaceable operator new and delete, still malloc and free underneath, counting each call, so allocations made and not freed show as a level and allocations a frame as a rate; fua_allocsites also records where each was made.

#include "features/telemetry/telemetry.h"
#include "features/telemetry/osprobe.h"
#include "features/telemetry/computation/allocsites_compute.h"
#include "features/sanitizer/sanitizer.h"

#include "c_dispatch.h"
#include "doomtype.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>

using zx::telemetry::AllocTable;

namespace {

// [rc4l] Constant-initialised, so a static constructor's new before any other code runs counts too.
std::atomic<long long> s_news{ 0 };
std::atomic<long long> s_deletes{ 0 };

// [rc4l] The table fua_allocsites records into while it runs, and the lock around it.
std::atomic<AllocTable *> s_sites{ nullptr };
std::atomic_flag s_sitesLock = ATOMIC_FLAG_INIT;
// [rc4l] Set while this thread holds the table, so the table's own allocations and frees never reach it.
thread_local bool t_holding = false;

struct HoldSites
{
	HoldSites() { t_holding = true; while (s_sitesLock.test_and_set(std::memory_order_acquire)) {} }
	~HoldSites() { s_sitesLock.clear(std::memory_order_release); t_holding = false; }
};

double LiveAllocations()
{
	return (double)(s_news.load(std::memory_order_relaxed) - s_deletes.load(std::memory_order_relaxed));
}

// [rc4l] The news since the last read, read every frame.
double AllocationsThisFrame()
{
	static long long last = 0;
	const long long now = s_news.load(std::memory_order_relaxed);
	const double made = (double)(now - last);
	last = now;
	return made;
}

const bool s_registered = (
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "mem/allocations_live", "objects", zx::telemetry::CounterKind::Gauge, 250 }, LiveAllocations),
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "mem/allocations_per_frame", "allocations", zx::telemetry::CounterKind::Gauge, 0 }, AllocationsThisFrame),
	true);

// [rc4l] A ThreadSanitizer build keeps the sanitizer's own operator new and delete, so a library's allocations are judged by its suppressions rather than as the engine's.
#if !defined(ZX_THREAD_SANITIZER)
void Record(void *p, std::size_t size)
{
	void *frames[AllocTable::kFrames];
	t_holding = true;
	const int depth = zx::telemetry::CaptureStack(frames, AllocTable::kFrames);
	HoldSites hold;
	if (AllocTable *t = s_sites.load(std::memory_order_relaxed)) t->Insert(p, size, frames, depth);
}

void *Allocate(std::size_t size)
{
	void *p = std::malloc(size != 0 ? size : 1);
	if (p == nullptr) return p;
	s_news.fetch_add(1, std::memory_order_relaxed);
	if (s_sites.load(std::memory_order_acquire) != nullptr && !t_holding) Record(p, size);
	return p;
}

void Free(void *p)
{
	if (p == nullptr) return;
	s_deletes.fetch_add(1, std::memory_order_relaxed);
	if (s_sites.load(std::memory_order_acquire) != nullptr && !t_holding)
	{
		HoldSites hold;
		if (AllocTable *t = s_sites.load(std::memory_order_relaxed)) t->Erase(p);
	}
	std::free(p);
}
#endif

// [rc4l] Puts a new table in place of the old one (none to stop) and frees the old one once nobody can be using it.
void SwapSites(AllocTable *next)
{
	AllocTable *old;
	{
		HoldSites hold;
		old = s_sites.exchange(next);
	}
	delete old;
}

} // namespace

#if !defined(ZX_THREAD_SANITIZER)
void *operator new(std::size_t size)
{
	void *p = Allocate(size);
	if (p == nullptr) throw std::bad_alloc();
	return p;
}

void *operator new[](std::size_t size)
{
	void *p = Allocate(size);
	if (p == nullptr) throw std::bad_alloc();
	return p;
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept { return Allocate(size); }
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept { return Allocate(size); }
void operator delete(void *p) noexcept { Free(p); }
void operator delete[](void *p) noexcept { Free(p); }
void operator delete(void *p, std::size_t) noexcept { Free(p); }
void operator delete[](void *p, std::size_t) noexcept { Free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { Free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { Free(p); }
#endif

// [rc4l] Where the allocations still live were made: start records each new allocation's stack from then on, report lists the stacks holding the most, and stop ends it.
CCMD(fua_allocsites)
{
	const FString what = argv.argc() > 1 ? argv[1] : "report";
	if (what.CompareNoCase("start") == 0)
	{
		SwapSites(new AllocTable(1 << 20, 1 << 16));
		Printf("telemetry: recording where every allocation is made (fua_allocsites report | stop)\n");
		return;
	}
	if (what.CompareNoCase("stop") == 0)
	{
		SwapSites(nullptr);
		Printf("telemetry: allocation sites no longer recorded\n");
		return;
	}
	const size_t top = argv.argc() > 2 ? (size_t)std::max(1, atoi(argv[2])) : 12;
	std::vector<AllocTable::Site> sites;
	double bytes = 0.0;
	size_t live = 0, dropped = 0;
	{
		HoldSites hold;
		if (AllocTable *t = s_sites.load(std::memory_order_relaxed))
		{
			sites = t->TopSites(top, bytes);
			live = t->Live();
			dropped = t->Dropped();
		}
	}
	if (sites.empty() && live == 0)
	{
		Printf("telemetry: no allocation made since fua_allocsites start is live (or it was never started)\n");
		return;
	}
	Printf("telemetry: %u allocations (%s) made since fua_allocsites start are live%s; the stacks holding the most:\n", (unsigned)live,
		zx::telemetry::ComputeBytesText(bytes).c_str(), dropped != 0 ? ", some not recorded (a table was full)" : "");
	std::vector<void *> frames;
	for (const AllocTable::Site &s : sites) frames.insert(frames.end(), s.frames.begin(), s.frames.end());
	const std::vector<std::string> names = zx::telemetry::SymbolNames(frames);
	size_t at = 0;
	for (const AllocTable::Site &s : sites)
	{
		const std::vector<std::string> stack(names.begin() + at, names.begin() + at + s.frames.size());
		at += s.frames.size();
		Printf("  %u (%s)  %s\n", (unsigned)s.count, zx::telemetry::ComputeBytesText(s.bytes).c_str(), zx::telemetry::ComputeAllocStackText(stack, 6).c_str());
	}
}
