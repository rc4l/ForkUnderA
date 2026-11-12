// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] When a new game clears ACS's world and global variables.

#ifndef ZX_ACSVARS_COMPUTE_H
#define ZX_ACSVARS_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@70735560bb12edf79bdc6b530fdb0ead65aabd02: a client changing levels for its server keeps what its own scripts stored, and clears only before it holds the full snapshot.
bool ComputeNewGameClearsAcsVars(bool inClientMode, bool clientActive);

} // namespace zx

#endif
