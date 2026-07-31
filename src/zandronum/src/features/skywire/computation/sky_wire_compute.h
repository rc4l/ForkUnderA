// [rc4l] The sky name as it crosses the wire.
//
// SERVERCOMMANDS_SetMapSky sends the sky BY NAME (sv_commands.cpp), reading FLevelLocals'
// skypic1/skypic2. Upstream deleted those fields in uzdoom@65e8563cf -- once the texture is
// resolved they never need the name again -- but our netcode does, so we kept them, and kept them
// as char[9] rather than FString ON PURPOSE: nine bytes is eight characters plus a NUL, which is
// exactly the truncation the wire has always carried. An FString would happily put a twenty-
// character name into a packet that every existing client decodes expecting the old bound.
//
// That truncation is therefore a PROTOCOL CONSTANT, not an implementation detail, and this unit
// exists so it is pinned by a test rather than by the incidental size of a struct member. A future
// refactor that "modernises" those fields to FString has to delete these tests to do it, which is
// the point -- the failure is loud instead of a silent desync in the field.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SKY_WIRE_COMPUTE_H
#define ZX_SKY_WIRE_COMPUTE_H

#include <cstddef>

namespace zx
{

// [rc4l] Bytes of the wire-facing sky-name buffer: 8 characters + NUL. Mirrors the char[9] fields
// in FLevelLocals; static_asserted against them at the call site so the two cannot drift.
enum { ZX_SKY_NAME_SIZE = 9 };

// [rc4l] Copy a sky name (from a level_info FString, or a resolved FTexture's name) into the
// wire-facing buffer, applying the historic 8-character truncation and always NUL-terminating.
// `outSize` is the destination size; passing anything other than ZX_SKY_NAME_SIZE is what a
// widening refactor would do, and the tests cover that shape too.
void CopySkyNameForWire(const char *src, char *out, size_t outSize);

// [rc4l] True when `name` survives the wire unchanged -- i.e. it is short enough that the client
// receives exactly what the server had. Callers can use this to warn a mapper that a long sky
// texture name will reach clients truncated, now that level_info_t itself can hold one.
bool SkyNameFitsOnWire(const char *name);

} // namespace zx

#endif // ZX_SKY_WIRE_COMPUTE_H
