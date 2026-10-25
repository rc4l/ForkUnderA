// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] features/music-cache: the level's compressed songs, inflated once at its load and kept, so a script's music change only opens them.
#ifndef ZX_MUSICCACHE_H
#define ZX_MUSICCACHE_H

#include <memory>
#include <vector>

class FileReader;

namespace zx { namespace musiccache {

using Bytes = std::vector<unsigned char>;

// [Doruk-Aksoy] A compressed lump's inflated bytes, from the cache or a song still playing them, else read now; null for an empty lump.
std::shared_ptr<const Bytes> Load(int lump);

// [Doruk-Aksoy] A reader over bytes, holding them for as long as it and the song it is handed to live.
FileReader *Reader(std::shared_ptr<const Bytes> bytes);

// [Doruk-Aksoy] Keeps the songs MAPINFO and the level's scripts name, as many as snd_fua_musiccache_mb holds, and lets the others go.
void PrecacheLevel();

// [Doruk-Aksoy] Lets every cached song go, as the lumps they were read from are about to be renumbered.
void Clear();

} }

#endif // ZX_MUSICCACHE_H
