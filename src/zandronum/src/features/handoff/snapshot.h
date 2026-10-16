// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The main thread's state a tic reads, taken in the handoff before it, so a tic on its own thread never reads the screen the main thread may be replacing.

#ifndef ZX_HANDOFF_SNAPSHOT_H
#define ZX_HANDOFF_SNAPSHOT_H

namespace zx { namespace handoff {

// [rc4l] Takes the snapshot; the handoff's, before each tic.
void TakeSnapshot();

// [rc4l] The screen's size as the snapshot took it, which ACS's GetScreenWidth and GetScreenHeight read.
int TicScreenWidth();
int TicScreenHeight();

}} // namespace zx::handoff

#endif // ZX_HANDOFF_SNAPSHOT_H
