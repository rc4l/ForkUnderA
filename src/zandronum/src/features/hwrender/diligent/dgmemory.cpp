// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Every texture and buffer made through CreateDeviceTexture and CreateDeviceBuffer, held weakly so counting changes no lifetime and swept as each is released, as features/telemetry's gpu/mem counters and tags.

#include "features/hwrender/diligent/dgmemory.h"
#include "features/hwrender/diligent/dgdevice.h"   // [Doruk-Aksoy] EngineMemoryAllocator, AllocateAlignedNew
#include "features/telemetry/telemetry.h"

#include "GraphicsAccessories.hpp"
#include "RefCntAutoPtr.hpp"
#include "doomtype.h"

#include "features/hwrender/computation/alignedblock_compute.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <vector>

namespace zx { namespace hwrender {

namespace {

// [Doruk-Aksoy] What sits before each aligned block: the larger block it was cut from, and its size for a reallocation's copy.
struct AlignedHeader { void *base; size_t size; };

// [Doruk-Aksoy] Diligent's default allocator calls malloc, which the engine's counters never see; this one calls operator new, aligned blocks included.
struct EngineAllocator final : Diligent::IMemoryAllocator
{
	void *Allocate(size_t size, const Diligent::Char *, const char *, const Diligent::Int32) override { return ::operator new(size); }
	void Free(void *ptr) override { ::operator delete(ptr); }
	void *AllocateAligned(size_t size, size_t alignment, const Diligent::Char *, const char *, const Diligent::Int32) override { return AllocateAlignedNew(size, alignment); }
	void FreeAligned(void *ptr) override { FreeAlignedNew(ptr); }
};

EngineAllocator g_engineAllocator;

} // namespace

Diligent::IMemoryAllocator *EngineMemoryAllocator() { return &g_engineAllocator; }

void *AllocateAlignedNew(size_t size, size_t alignment)
{
	const size_t align = std::max(alignment, alignof(AlignedHeader));
	char *base = static_cast<char *>(::operator new(ComputeAlignedBlockBytes(size, align, sizeof(AlignedHeader)), std::nothrow));
	if (base == nullptr) return nullptr;
	AlignedHeader *block = reinterpret_cast<AlignedHeader *>(ComputeAlignedBlockStart(reinterpret_cast<uintptr_t>(base), align, sizeof(AlignedHeader)));
	block[-1] = AlignedHeader{ base, size };
	return block;
}

void FreeAlignedNew(void *ptr)
{
	if (ptr != nullptr) ::operator delete(static_cast<AlignedHeader *>(ptr)[-1].base);
}

size_t AlignedNewSize(void *ptr)
{
	return static_cast<AlignedHeader *>(ptr)[-1].size;
}

namespace {

struct Held
{
	Diligent::RefCntWeakPtr<Diligent::IDeviceObject> object;
	size_t slot;
	double bytes;
};

std::mutex g_lock;
std::vector<Held> g_held;
zx::telemetry::TagLedger g_ledger(48);

// [rc4l] Forgets every object nothing holds any more; called with g_lock held.
void SweepLocked()
{
	size_t keep = 0;
	for (size_t i = 0; i < g_held.size(); i++)
	{
		if (!g_held[i].object.IsValid()) { g_ledger.Remove(g_held[i].slot, g_held[i].bytes); continue; }
		if (keep != i) g_held[keep] = std::move(g_held[i]);
		keep++;
	}
	g_held.resize(keep);
}

double TotalBytes()
{
	std::lock_guard<std::mutex> lock(g_lock);
	SweepLocked();
	return g_ledger.TotalBytes();
}

double Objects()
{
	std::lock_guard<std::mutex> lock(g_lock);
	SweepLocked();
	return g_ledger.TotalObjects();
}

void Tags(std::vector<zx::telemetry::Reading> &out)
{
	std::lock_guard<std::mutex> lock(g_lock);
	SweepLocked();
	const std::vector<zx::telemetry::Reading> tags = g_ledger.Readings("gpu/mem/tag/");
	out.insert(out.end(), tags.begin(), tags.end());
}

const bool s_registered = (
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/mem/total_bytes", "bytes", zx::telemetry::CounterKind::Gauge, 500 }, TotalBytes),
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/mem/objects", "objects", zx::telemetry::CounterKind::Gauge, 500 }, Objects),
	zx::telemetry::RegisterBreakdown("gpu/mem/tag/", Tags),
	true);

void Track(Diligent::IDeviceObject *object, const char *name, double bytes)
{
	if (object == nullptr) return;
	std::lock_guard<std::mutex> lock(g_lock);
	const size_t slot = g_ledger.Add(zx::telemetry::ComputeMemoryTag(name != nullptr ? name : ""), bytes);
	g_held.push_back(Held{ Diligent::RefCntWeakPtr<Diligent::IDeviceObject>(object), slot, bytes });
}

// [rc4l] A texture's bytes from its description, its format's texel or block size from Diligent's table.
double TextureBytes(const Diligent::TextureDesc &d)
{
	const Diligent::TextureFormatAttribs &fa = Diligent::GetTextureFormatAttribs(d.Format);
	const bool volume = d.Type == Diligent::RESOURCE_DIM_TEX_3D;
	zx::telemetry::TextureShape s = {};
	s.width = (int)d.Width;
	s.height = (int)d.Height;
	s.depth = volume ? (int)d.Depth : 1;
	s.layers = volume ? 1 : (int)d.ArraySize;
	s.mips = (int)d.MipLevels;
	s.samples = (int)d.SampleCount;
	if (fa.ComponentType == Diligent::COMPONENT_TYPE_COMPRESSED)
	{
		s.blockWidth = fa.BlockWidth;
		s.blockHeight = fa.BlockHeight;
		s.blockBytes = fa.ComponentSize;
	}
	else s.bytesPerTexel = (double)fa.ComponentSize * fa.NumComponents;
	return zx::telemetry::ComputeTextureBytes(s);
}

} // namespace

void TrackDeviceTexture(Diligent::ITexture *texture, const Diligent::TextureDesc &desc)
{
	Track(texture, desc.Name, TextureBytes(desc));
}

void TrackDeviceBuffer(Diligent::IBuffer *buffer, const Diligent::BufferDesc &desc)
{
	Track(buffer, desc.Name, (double)desc.Size);
}

void ReportHeldDeviceObjects(const char *when)
{
	std::lock_guard<std::mutex> lock(g_lock);
	SweepLocked();
	Printf("telemetry: %.0f GPU objects (%s) held %s\n", g_ledger.TotalObjects(), zx::telemetry::ComputeBytesText(g_ledger.TotalBytes()).c_str(), when);
	for (const zx::telemetry::Reading &r : g_ledger.Readings(""))
		Printf("  %s %s\n", r.name.c_str(), zx::telemetry::ComputeBytesText(r.value).c_str());
}

}} // namespace zx::hwrender
