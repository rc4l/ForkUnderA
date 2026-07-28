// [rc4l] Convert 48.16 fixed-point world coordinates (raw 64-bit fixed_t values) to render-space
// floats for the hardware backend.
//
// The scene renderer is float-based; the sim is 64-bit fixed_t (48.16 since the widening). Passing a
// coordinate through the old 32-bit path -- `(float)(int)fixed` or `fixed / 65536.0f` -- truncates
// the high 32 bits, so anything beyond ~32k map units in fixed (a normal full-size map: two points
// ~46k units apart exceed INT32_MAX in raw fixed) converts to garbage. Convert through int64 ->
// double so the whole 48.16 range survives, then narrow to float only at the very end for the GPU.
//
// The conversion is one-way (sim -> pixels); nothing here feeds back into the fixed-point sim, so
// netplay/demo determinism is untouched. See the fixed64-widening skill.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_VERTEXCONVERT_COMPUTE_H
#define ZX_VERTEXCONVERT_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] 65536 raw fixed units == 1.0 map unit (FRACBITS == 16), as a double so the divide is exact
// across the full fixed range rather than a lossy float reciprocal.
constexpr double kFixedToUnits = 1.0 / 65536.0;

// [rc4l] Raw fixed_t value -> map units as double. Lossless for every value fixed_t can hold: a
// 64-bit integer has at most 63 significant bits and IEEE double has 52, but real map coordinates
// stay far under 2^52 raw, so the coordinates we actually convert round-trip exactly.
double FixedRawToUnits(int64_t rawFixed);

// [rc4l] Same, narrowed to float for vertex buffers. The double intermediate is what keeps the high
// bits; the float narrowing at the end costs precision only in the low mantissa, not the integer
// part of a large coordinate.
float FixedRawToUnitsF(int64_t rawFixed);

// [rc4l] A render-space vertex. Doom world space is X east, Y north, Z up; the GL backend wants
// X right, Y up, Z into the screen -- so the axes map (x, y, z)_doom -> (x, z, y)_gl. This mirrors
// the legacy FStateVec3::Set(x,y,z) which stored vec = {x, z, y}.
struct RenderVertexF
{
	float x;
	float y;
	float z;
};

// [rc4l] Convert a fixed-point Doom vertex to a render vertex, applying the axis swap and the
// fixed->float conversion in one place so no call site re-derives (and re-breaks) either.
RenderVertexF FixedVertexToRender(int64_t doomXRaw, int64_t doomYRaw, int64_t doomZRaw);

} // namespace zx

#endif // ZX_VERTEXCONVERT_COMPUTE_H
