// [rc4l] The sky name as it crosses the wire.
//
// SERVERCOMMANDS_SetMapSky sends the sky BY NAME (sv_commands.cpp). uzdoom@65e8563cf removed the
// name from FLevelLocals entirely -- once the texture is resolved, the id IS the level's sky -- so
// we do the same and derive the name at the wire boundary from the texture itself. That keeps our
// struct identical to upstream's (nothing to re-merge later) and leaves ONE source of truth.
//
// The 8-character bound is not something we maintain: FTexture::Name is itself char[9], so a name
// derived from the resolved texture is already bounded exactly as it has always been. This unit
// exists to state that bound explicitly and pin it with tests, because it IS the observable
// protocol behaviour -- clients have received at most eight characters for this field since the
// command existed.
//
// What over-running it would and would NOT do, since it is easy to overstate: the field is written
// with WriteString and read with ReadString, which are NUL-terminated and self-delimiting, and
// ReadString deliberately keeps consuming an over-long string so the packet stays aligned to the
// next field (see the [BB] comment in networkshared.cpp). A longer name would therefore ARRIVE
// INTACT and parse fine -- it would not corrupt the rest of the packet. The real consequence is a
// compatibility one: clients would receive a name they may not resolve to a texture. Worth pinning,
// not worth dramatising.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SKY_WIRE_COMPUTE_H
#define ZX_SKY_WIRE_COMPUTE_H

#include <cstddef>

namespace zx
{

// [rc4l] Bytes of a sky name on the wire: 8 characters + NUL. Mirrors FTexture::Name, which is what
// the name is derived from; static_asserted at the call site so the two cannot drift.
enum { ZX_SKY_NAME_SIZE = 9 };

// [rc4l] Copy a sky name into a wire-facing buffer, bounded and always NUL-terminated. Used where a
// name must be materialised from a resolved texture for the wire or for a comparison.
void CopySkyNameForWire(const char *src, char *out, size_t outSize);

// [rc4l] True when `name` survives the wire unchanged. Callers can use this to warn a mapper that a
// long sky texture name will reach clients truncated, now that level_info_t itself can hold one.
bool SkyNameFitsOnWire(const char *name);

} // namespace zx

#endif // ZX_SKY_WIRE_COMPUTE_H
