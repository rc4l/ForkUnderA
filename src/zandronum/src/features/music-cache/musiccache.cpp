// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] features/music-cache glue: which songs the level names, the bytes kept for them, and the reader a song plays them through.
#include "features/music-cache/musiccache.h"
#include "features/music-cache/computation/musiccache_compute.h"
#include "features/handoff/effects.h"
#include "features/net-session/netrole.h"
#include "features/telemetry/telemetry.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "files.h"
#include "g_level.h"
#include "gstrings.h"
#include "p_acs.h"
#include "s_sound.h"
#include "w_wad.h"
#include "sound/i_music.h"
#include <atomic>
#include <map>

EXTERN_CVAR(Float, snd_musicvolume)
EXTERN_CVAR(Bool, snd_fua_musiccache)
EXTERN_CVAR(Int, snd_fua_musiccache_mb)

namespace zx { namespace musiccache {

// [Doruk-Aksoy] The songs kept for the level, by lump.
static std::map<int, std::shared_ptr<const Bytes>> cached;
// [Doruk-Aksoy] Every song inflated and still playing, so one already loaded is never inflated twice.
static std::map<int, std::weak_ptr<const Bytes>> live;
static std::atomic<long long> cachedBytes(0);
// [Doruk-Aksoy] Music changes since the level's precache that played bytes already in memory, and those that inflated a song.
static unsigned changesFromMemory, changesInflated;

static void Recount()
{
	long long total = 0;
	for (const auto &entry : cached)
		total += (long long)entry.second->size();
	cachedBytes = total;
}

static double CachedBytes() { return (double)cachedBytes.load(); }
static const zx::telemetry::Counter s_cachedBytes = zx::telemetry::Register(zx::telemetry::CounterSpec{ "audio/music_cache_bytes", "bytes", zx::telemetry::CounterKind::Gauge, 500 }, CachedBytes);

// [Doruk-Aksoy] Holds the bytes ahead of the MemoryReader over them, as bases are made in order and freed in reverse.
struct KeepAlive
{
	std::shared_ptr<const Bytes> bytes;
};

class SharedMusicReader : private KeepAlive, public MemoryReader
{
public:
	explicit SharedMusicReader(std::shared_ptr<const Bytes> b)
		: KeepAlive{ std::move(b) }, MemoryReader((const char *)bytes->data(), (long)bytes->size())
	{
	}
};

static std::shared_ptr<const Bytes> Inflate(int lump)
{
	const int length = Wads.LumpLength(lump);
	if (length <= 0)
		return nullptr;
	ZX_ZONE("audio/music_read");   // [Doruk-Aksoy] the whole compressed lump, allocated and inflated
	auto bytes = std::make_shared<Bytes>(length);
	Wads.ReadLump(lump, bytes->data());
	return bytes;
}

// [Doruk-Aksoy] Load without the counting, which the precache uses too; inflated says whether it read the lump.
static std::shared_ptr<const Bytes> Fetch(int lump, bool &inflated)
{
	inflated = false;
	const auto kept = cached.find(lump);
	if (kept != cached.end())
		return kept->second;
	const auto playing = live.find(lump);
	if (playing != live.end())
		if (std::shared_ptr<const Bytes> bytes = playing->second.lock())
			return bytes;
	std::shared_ptr<const Bytes> bytes = Inflate(lump);
	if (bytes == nullptr)
		return nullptr;
	inflated = true;
	for (auto it = live.begin(); it != live.end(); )
		it = it->second.expired() ? live.erase(it) : std::next(it);
	live[lump] = bytes;
	return bytes;
}

std::shared_ptr<const Bytes> Load(int lump)
{
	bool inflated;
	std::shared_ptr<const Bytes> bytes = Fetch(lump, inflated);
	if (bytes != nullptr)
		++(inflated ? changesInflated : changesFromMemory);
	return bytes;
}

FileReader *Reader(std::shared_ptr<const Bytes> bytes)
{
	return new SharedMusicReader(std::move(bytes));
}

void Clear()
{
	cached.clear();
	live.clear();
	Recount();
}

// [Doruk-Aksoy] The lump S_ChangeMusic would play for a name, or -1, by its own steps: LANGUAGE's D_ name, the alias, then the lump unless a file on disk wins.
static int MusicLump(const char *name)
{
	FString dehacked;
	if (name != NULL && name[0] == '$')
	{
		const char *song = GStrings[name + 1];
		if (song != NULL)
		{
			dehacked << "D_" << song;
			name = dehacked;
		}
	}
	name = ComputeMusicLumpName(name);
	if (name == NULL)
		return -1;
	// [Doruk-Aksoy] A name the table lacks is no alias's key, and looking it up so creates none.
	const FName asName(name, true);
	const FName *alias = asName != NAME_None ? MusicAliases.CheckKey(asName) : NULL;
	if (alias != NULL)
		name = *alias != NAME_None ? ComputeMusicLumpName(alias->GetChars()) : NULL;
	if (name == NULL)
		return -1;
	const int lump = Wads.CheckNumForFullName(name, true, ns_music);
	return lump >= 0 && !FileExists(name) ? lump : -1;
}

static void AddCandidate(std::vector<ZxMusicCandidate> &candidates, const char *name)
{
	const int lump = MusicLump(name);
	if (lump >= 0)
		candidates.push_back(ZxMusicCandidate{ lump, (long long)Wads.LumpLength(lump), !Wads.IsUncompressedFile(lump) });
}

void PrecacheLevel()
{
	// [Doruk-Aksoy] Reading lumps is the main thread's, so a level loaded by a tic on its own thread is precached in the handoff after it.
	if (zx::handoff::QueuedForHandoff({ [](intptr_t, intptr_t) { PrecacheLevel(); }, 0, 0 }))
		return;
	ZX_ZONE("load/music_precache");
	const bool plays = !zx::net::IsServer() && !nomusic && snd_musicvolume > 0;
	const long long budget = ComputeMusicCacheBudget(snd_fua_musiccache && plays, snd_fua_musiccache_mb);
	std::vector<ZxMusicCandidate> candidates;
	if (budget > 0)
	{
		AddCandidate(candidates, level.Music);
		for (int lib = 0; FBehavior *module = FBehavior::StaticGetModule(lib); ++lib)
			for (DWORD i = 0; const char *text = module->LookupString(i); ++i)
				AddCandidate(candidates, text);
	}
	std::map<int, std::shared_ptr<const Bytes>> kept;
	bool inflated;
	for (int lump : ComputeMusicCachePlan(candidates, budget))
		if (std::shared_ptr<const Bytes> bytes = Fetch(lump, inflated))
			kept[lump] = std::move(bytes);
	cached.swap(kept);
	Recount();
	changesFromMemory = changesInflated = 0;
	DPrintf("Music cache: %u songs, %lld KB\n", (unsigned)cached.size(), cachedBytes.load() / 1024);
}

} }

// [Doruk-Aksoy] Lists the songs kept for the level, the memory they take, and how the level's music changes since got their bytes.
CCMD(fua_musiccache)
{
	for (const auto &entry : zx::musiccache::cached)
		Printf("%s: %u KB\n", Wads.GetLumpFullName(entry.first), (unsigned)(entry.second->size() / 1024));
	Printf("%u songs kept, %lld KB of %lld KB\n", (unsigned)zx::musiccache::cached.size(), zx::musiccache::cachedBytes.load() / 1024,
		ComputeMusicCacheBudget(snd_fua_musiccache, snd_fua_musiccache_mb) / 1024);
	Printf("music changes since the level's precache: %u from memory, %u inflated\n", zx::musiccache::changesFromMemory, zx::musiccache::changesInflated);
}

// [Doruk-Aksoy] Whether the level's songs are kept in memory; turning it off lets them go at once.
CUSTOM_CVAR(Bool, snd_fua_musiccache, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (!self)
		zx::musiccache::Clear();
}

// [Doruk-Aksoy] How many MB of the level's songs are kept, from the next level on.
CUSTOM_CVAR(Int, snd_fua_musiccache_mb, 128, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	if (self < ZX_MUSICCACHE_MIN_MB)
		self = ZX_MUSICCACHE_MIN_MB;
	else if (self > ZX_MUSICCACHE_MAX_MB)
		self = ZX_MUSICCACHE_MAX_MB;
}
