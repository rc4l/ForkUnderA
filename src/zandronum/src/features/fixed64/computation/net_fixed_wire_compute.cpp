// [rc4l] Implementation of the pure network fixed_t wire model. See net_fixed_wire_compute.h.
// The byte-level behaviour is copied verbatim from BYTESTREAM_s (networkshared.cpp) so the model
// cannot silently diverge from the real serializer.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "net_fixed_wire_compute.h"

namespace zx
{

namespace
{

// [rc4l] BYTESTREAM_s::WriteLong emits the low 4 bytes little-endian; ReadLong rebuilds a signed int
// as (b0) + (b1<<8) + (b2<<16) + (b3<<24). Modelled on a 4-byte buffer to keep the byte math honest.
int32_t ByteRoundtripLong(int32_t v)
{
	unsigned char b[4];
	b[0] = static_cast<unsigned char>(v & 0xFF);
	b[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
	b[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
	b[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
	// Exactly ReadLong(): the high byte becomes the sign bit. Assemble in uint32_t (shifting a set
	// high bit into a signed int is UB) and reinterpret to int32_t.
	const uint32_t u = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
	return static_cast<int32_t>(u);
}

// [rc4l] WriteShort emits the low 2 bytes; ReadShort returns (short)(b0 + (b1<<8)) -- the `(short)`
// cast is what sign-extends a value like 0xFF9C back to -100.
int16_t ByteRoundtripShort(int v)
{
	unsigned char b[2];
	b[0] = static_cast<unsigned char>(v & 0xFF);
	b[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
	return static_cast<int16_t>(int(b[0]) + (int(b[1]) << 8));
}

} // namespace

int64_t WireRoundtripLong(int64_t rawFixed)
{
	// Sender: (SDWORD)v truncates the 64-bit raw to 32 bits.
	const int32_t sent = static_cast<int32_t>(rawFixed);
	// Wire + ReadLong reconstruct the same signed 32-bit value.
	const int32_t got = ByteRoundtripLong(sent);
	// command.field = got; -- fixed_t(int) sign-extends back to 64 bits.
	return static_cast<int64_t>(got);
}

int64_t WireRoundtripShort(int64_t rawFixed)
{
	// Sender: (int)(v >> FRACBITS) -- arithmetic shift keeps the sign, then truncate to int.
	const int sentInt = static_cast<int>(rawFixed >> ZX_WIRE_FRACBITS);
	// Wire + ReadShort() sign-extend the low 16 bits back to a signed short.
	const int16_t got = ByteRoundtripShort(sentInt);
	// command.field = ReadShort() << FRACBITS. Multiply by 1<<FRACBITS rather than shift: `got` is a
	// signed short and left-shifting a negative value is UB (UBSan halt_on_error fails CI on it).
	return static_cast<int64_t>(got) * (int64_t(1) << ZX_WIRE_FRACBITS);
}

} // namespace zx
