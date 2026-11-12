// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The rules behind the ACS functions that change the map rotation, kept apart from the rotation they change.

#ifndef ZX_MAPROTATION_COMPUTE_H
#define ZX_MAPROTATION_COMPUTE_H

namespace zx { namespace maprotation {

// [rc4l] Ported from zandronum@f522876fe386925f508425afceb41474914d4c24: whether SetMapUsedStatus changes the entry at position, never on a client, past the rotation's end, or to the status it has.
bool ComputeSetsMapUsedStatus(bool inClientMode, unsigned position, unsigned numEntries, bool used, bool currentlyUsed);

}} // namespace zx::maprotation

#endif
