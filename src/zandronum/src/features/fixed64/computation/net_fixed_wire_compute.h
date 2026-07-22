// [rc4l] Pure model of how a 64-bit fixed_t crosses the (deliberately 32-bit) network wire, so the
// sign/round-trip behaviour can be swept in a unit test without linking the engine or the netcode.
// It mirrors, exactly, the two patterns the generated servercommands.cpp uses:
//   * "long"  fields  -- WriteLong( (SDWORD)v )        <-> command.f = ReadLong();              (full 16.16)
//   * "short" fields  -- WriteShort( (int)(v>>FRACBITS)) <-> command.f = ReadShort() << FRACBITS; (integer units)
// plus the exact BYTESTREAM_s byte (little-endian, sign-through-`(short)`) semantics from
// networkshared.cpp. The point is to confirm that a NEGATIVE fixed_t (a leftward velocity, a
// coordinate below the origin) survives the wire with its sign intact under the widening -- the
// Shape-3 hazard -- rather than zero-extending into a huge positive.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_NET_FIXED_WIRE_COMPUTE_H
#define ZX_NET_FIXED_WIRE_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] FRACBITS mirror (tables.h). The wire shift is fixed at 16 regardless of the fixed_t width.
enum { ZX_WIRE_FRACBITS = 16 };

// [rc4l] Round-trip a raw fixed_t value through a "long" wire field: the sender truncates to a
// signed 32-bit word ((SDWORD)v), the four little-endian bytes travel, and BYTESTREAM_s::ReadLong
// reconstructs a signed int which the command assigns back into the 64-bit fixed_t (sign-extending).
// For any value whose 16.16 magnitude fits in 32 bits this is the identity; larger values document
// the wire's intentional 32-bit ceiling.
int64_t WireRoundtripLong(int64_t rawFixed);

// [rc4l] Round-trip a raw fixed_t value through a "short" wire field: the sender emits the integer
// part ((int)(v>>FRACBITS)) as a 16-bit word, ReadShort sign-extends it back through `(short)`, and
// the command shifts it left by FRACBITS into the fixed_t. Fractional bits are intentionally lost;
// the integer part (in map units within +/-32767) survives with its sign.
int64_t WireRoundtripShort(int64_t rawFixed);

} // namespace zx

#endif // ZX_NET_FIXED_WIRE_COMPUTE_H
