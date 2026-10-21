// [rc4l] Which savegame versions a build reads: from the oldest it supports up to its own, since a newer save may hold classes and fields it cannot know and an older build stopped dead on them.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SAVEVERSION_COMPUTE_H
#define ZX_SAVEVERSION_COMPUTE_H

// [rc4l] A save version's verdict for a build.
enum SaveVersionVerdict { kSaveVersionReadable, kSaveVersionTooOld, kSaveVersionTooNew };

// [rc4l] Whether a save of version is readable by a build reading oldest to newest, or too old or too new for it.
int ComputeSaveVersionVerdict(int version, int oldest, int newest);

#endif // ZX_SAVEVERSION_COMPUTE_H
