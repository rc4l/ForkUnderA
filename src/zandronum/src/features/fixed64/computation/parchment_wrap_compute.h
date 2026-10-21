// [rc4l] Automap parchment (AUTOPAGE) scroll-offset wrap.
//
// AM_ScrollParchment keeps the tiled automap background offset inside one tile by normalising
// it into (-period, 0]. Upstream did that with subtract-until-in-range loops:
//
//     while (mapxstart >  0)       mapxstart -= pwidth;
//     while (mapxstart <= -pwidth) mapxstart += pwidth;
//
// That was survivable while fixed_t was 32-bit: the offset could never be more than ~2^31 away
// from the range, so the worst case was a few thousand iterations. After the fixed_t widening it
// is a hang. The first scroll after the automap opens is fed a delta against the FIXED_MAX
// sentinel in f_oldloc (AM_doFollowPlayer), which at 64-bit width is ~9.2e18 -- so the loop runs
// on the order of 10^13 times and the engine never returns from AM_Ticker. It reproduces on any
// wad that supplies an AUTOPAGE automap background (Heretic/Hexen/Strife, and Doom mods that
// ship one); plain Doom has no AUTOPAGE, so mapback is invalid and the loops never run.
//
// The wrap is just a modulo, so compute it as one. O(1), and immune to however far out of range
// the offset starts.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_PARCHMENT_WRAP_COMPUTE_H
#define ZX_PARCHMENT_WRAP_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] Normalise a tiled-background scroll offset into (-period, 0] -- the same half-open range
// the old loops produced, so the drawn tiling is unchanged. `period` is one tile's size in map
// units (texture size << MAPBITS) and must be positive; a non-positive period means the texture
// has no size to wrap against, so the offset is returned untouched (the caller then draws nothing
// useful, exactly as before, instead of dividing by zero or spinning).
inline int64_t WrapParchmentOffset(int64_t offset, int64_t period)
{
	if (period <= 0)
		return offset;

	// C++11 truncates toward zero, so the remainder keeps the sign of `offset`: the result is in
	// (-period, period). One conditional subtract folds the positive half down into (-period, 0].
	int64_t wrapped = offset % period;
	if (wrapped > 0)
		wrapped -= period;
	return wrapped;
}

} // namespace zx

#endif // ZX_PARCHMENT_WRAP_COMPUTE_H
