// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The melt's column starts, drawn from the game's M_Random as every renderer's melt draws them, so a wipe takes the same numbers out of the game's sequence whichever renderer starts it.

#ifndef ZX_WIPERANDOMS_H
#define ZX_WIPERANDOMS_H

namespace zx { namespace wipe {

// [rc4l] count bytes of M_Random into out; a wipe starts only while no tic runs, which the seam holds it to.
void DrawMeltRandoms(unsigned char *out, int count);

}} // namespace zx::wipe

#endif // ZX_WIPERANDOMS_H
