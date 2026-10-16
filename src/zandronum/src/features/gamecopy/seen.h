// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the walks have seen (ML_MAPPED, SECF_DRAWN, SSECF_DRAWN), kept by the renderer and merged into the live map between tics, since the automap, savegames and bots read those bits and a tic may be changing the flags beside them.

#ifndef ZX_GAMECOPY_SEEN_H
#define ZX_GAMECOPY_SEEN_H

namespace zx { namespace gamecopy {

enum SeenKind { kSeenSubsector, kSeenSector, kSeenLine, kSeenKinds };

// [rc4l] Whether a subsector, sector or line has been seen, as the live map had it at the last merge and the walks have marked it since.
bool Seen(SeenKind kind, int index);
// [rc4l] Marks one seen, for the next merge to set on the live map.
void MarkSeen(SeenKind kind, int index);

// [rc4l] Before a tic: what the walks marked since set on the live map, which bots read.
void ApplySeen();
// [rc4l] Between tics: ApplySeen, then the live map's bits taken back, which a savegame load or a reset may have changed.
void MergeSeen();
// [rc4l] A new level's bits, taken from the live map.
void InitSeen();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_SEEN_H
