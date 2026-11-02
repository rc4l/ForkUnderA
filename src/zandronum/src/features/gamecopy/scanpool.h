// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [Doruk-Aksoy] The pool a scan of the game for the render copy splits its reads across, by the thread it runs on.

#ifndef ZX_GAMECOPY_SCANPOOL_H
#define ZX_GAMECOPY_SCANPOOL_H

#include "features/handoff/effects.h"
#include "features/jobs/jobs.h"

namespace zx { namespace gamecopy {

// [Doruk-Aksoy] The game thread's own pool at a tic's end beside the frames, which leaves the frames' pool to them, else the main thread's.
inline zx::jobs::JobPool &ScanPool() { return zx::handoff::Deferring() ? zx::jobs::TicPool() : zx::jobs::Pool(); }

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_SCANPOOL_H
