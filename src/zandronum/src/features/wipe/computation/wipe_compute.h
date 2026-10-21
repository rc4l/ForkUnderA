// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The screen wipes as GL's wipers run them: the melt's falling columns, the crossfade's opacity and the burn's mask, worked out once for GL and the backend.

#ifndef ZX_WIPE_COMPUTE_H
#define ZX_WIPE_COMPUTE_H

namespace zx { namespace wipe {

// [rc4l] The melt runs on a 320 by 200 grid whatever the screen's size.
enum { kMeltColumns = 320, kMeltHeight = 200 };

// [rc4l] Each melt column's start from one random byte apiece: the first up to 15 tics late, each next within a tic of its neighbour and never early.
void ComputeMeltStart(const unsigned char *randoms, int *y);

// [rc4l] One tic of the melt: a column waits out its delay, then falls, a row more each tic until it falls eight at a time; true once every column has fallen.
bool ComputeMeltTic(int *y);

// [rc4l] Where melt column `column`, fallen `fall` grid rows, shows the old screen on a w by h screen: the strip (x, y, w, h) and the old screen's part in it (u1 to u2 across, 0 to v2 down), false once it is gone.
struct MeltStrip { float x, y, w, h, u1, u2, v2; };
bool ComputeMeltStrip(int column, int fall, int w, int h, MeltStrip &out);

// [rc4l] The crossfade's opacity of the new screen after `clock` tics: done at 32.
float ComputeCrossfadeAlpha(int clock);

// [rc4l] The burn's mask from its fire: each byte doubled into an opacity, clamped.
void ComputeBurnMask(const unsigned char *fire, int count, unsigned char *alpha);

}} // namespace zx::wipe

#endif // ZX_WIPE_COMPUTE_H
