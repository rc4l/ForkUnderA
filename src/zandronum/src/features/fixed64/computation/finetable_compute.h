// [rc4l] Wraparound fill for the finesine[] lookup table, extracted so its size can never again
// be tied to the wrong element width.
//
// finesine has FINEANGLES/4 spare entries past FINEANGLES. They mirror finesine[0 .. FINEANGLES/4)
// so that finecosine -- which is just finesine indexed with a +FINEANGLES/4 (PI/2) phase shift --
// can be read across the whole circle without a bounds wrap. R_InitTables builds finesine[0 ..
// FINEANGLES) with trig, then copies that quarter-circle prefix into the tail.
//
// THE TRAP THIS GUARDS: the copy must move FINEANGLES/4 *elements*, sized off the array's own
// element type. The original engine sized it with sizeof(angle_t) (4 bytes) because, at 32-bit
// fixed_t, angle_t and fixed_t were the same width. When fixed_t widened to 64 bits that copy
// moved only half the tail, leaving finecosine zeroed across roughly 315-360 degrees -- so a
// player pressing forward while facing that arc got velx = FixedMul(move, 0) = 0 and slid south
// regardless of facing. See finetable_compute_test.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_FINETABLE_COMPUTE_H
#define ZX_FINETABLE_COMPUTE_H

#include <cstddef>
#include <cstring>

namespace zx
{

// [rc4l] Number of ELEMENTS in the finesine wraparound tail (a quarter circle). Element count,
// never a byte size -- the byte size is derived from the array's own element type at the copy.
inline int ComputeFineSineWrapCount(int fineAngles) { return fineAngles / 4; }

// [rc4l] Copy the quarter-circle prefix into the wraparound tail. sizeof(T) tracks the real
// element width, so this stays correct no matter how wide fixed_t becomes.
template <class T>
void FillFineSineWrap(T *finesine, int fineAngles)
{
	std::memcpy(&finesine[fineAngles], &finesine[0],
				sizeof(T) * static_cast<std::size_t>(ComputeFineSineWrapCount(fineAngles)));
}

} // namespace zx

#endif // ZX_FINETABLE_COMPUTE_H
