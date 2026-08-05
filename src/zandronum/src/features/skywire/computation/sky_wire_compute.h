// [rc4l] The sky name as it crosses the wire.
//
// SERVERCOMMANDS_SetMapSky sends the sky BY NAME (sv_commands.cpp). uzdoom@65e8563cf removed the
// name from FLevelLocals entirely -- once the texture is resolved, the id IS the level's sky -- so
// we do the same and derive the name at the wire boundary from the texture itself. That keeps our
// struct identical to upstream's (nothing to re-merge later) and leaves ONE source of truth.
//
// THE 8-CHARACTER BOUND IS GONE, DELIBERATELY. It was never a rule we chose: FTexture::Name was
// char[9], so a name taken from the resolved texture arrived pre-bounded and this unit merely
// stated that. uzdoom@59885b856 made that field an FString, and the previous version of this header
// static_asserted sizeof(FTexture::Name) == 9 at the call site precisely so the change could not
// slip through unnoticed. It did not -- the build broke on that assert, which is the whole point.
//
// Sending the WHOLE name is the correct resolution, for three reasons:
//   - Nothing regresses. Before 59885b856 no texture could hold a name longer than eight
//     characters, so no existing content is affected by lifting a limit it could never reach.
//   - The transport already carries it. spec.map.txt declares sky1/sky2 as String -- variable
//     length, NUL-terminated, self-delimiting, read with ReadString straight into TexMan.GetTexture
//     with no fixed buffer anywhere on the client. The packet stays aligned regardless of length.
//   - Truncating is actively worse than not. "skies/night_a" cut to "skies/ni" does not merely fail
//     to resolve; it may resolve to a DIFFERENT texture, and the client then renders the wrong sky
//     with no error anywhere. A name the client cannot find is visible and diagnosable. A name that
//     silently resolves to something else is not.
//
// The same reasoning applies to SERVERCOMMANDS_SetCameraToTexture, bounded by the same char[9] and
// now sent whole for the same reasons (r_utility.cpp).
//
// This unit therefore no longer bounds anything. What it still owns is the null/empty handling at
// the boundary -- an unresolved sky2 is normal and must reach clients as an empty string rather
// than walking WriteString off a null pointer -- and the tests that pin the pass-through contract,
// so a later refactor cannot quietly reintroduce truncation.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SKY_WIRE_COMPUTE_H
#define ZX_SKY_WIRE_COMPUTE_H

namespace zx
{

// [rc4l] The name to put on the wire for a sky texture, whose name may be absent when the sky did
// not resolve. Returns the name unchanged, or "" -- never null, because WriteString walks to a NUL
// and would run off a null pointer. Length is the caller's content, not this function's business.
const char *SkyNameForWire(const char *name);

} // namespace zx

#endif // ZX_SKY_WIRE_COMPUTE_H
