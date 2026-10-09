// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A packed 0x??RRGGBB colour as three floats, the one way every mesh path reads PalEntry::d.

#ifndef ZX_PACKEDCOLOR_COMPUTE_H
#define ZX_PACKEDCOLOR_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] Red, green and blue in 0..1; the top byte, alpha in a PalEntry, is ignored.
void ComputeUnpackRGB(unsigned int packed, float &r, float &g, float &b);

}} // namespace zx::levelmesh

#endif // ZX_PACKEDCOLOR_COMPUTE_H
