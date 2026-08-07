// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which IWAD to actually host a catalogue entry on.
//
// An entry names the IWAD it was built for -- duel40b says doom2.wad -- and that is a PREFERENCE,
// not a requirement. Owning doom2.wad always means hosting on doom2.wad. Only when it is absent does
// features/wad-download's substitute table get asked, and it answers freedoom2.wad.
//
// The part that is not obvious: whether that substitution is safe depends on the whole selection,
// not on the IWAD. Freedoom's MAP01 is not Doom II's MAP01, so a selection playing STOCK levels
// under Freedoom loads different geometry and Zandronum's level authentication rejects the join. A
// selection that brings its own maps never touches a stock level, so the IWAD supplies only
// textures, sounds and actor definitions and nobody can tell.
//
// That distinction is already in the slot model, so it is derived rather than declared: if anything
// selected fills `maps`, the levels are its own.

#ifndef ZX_IWADPICK_COMPUTE_H
#define ZX_IWADPICK_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

enum class IwadChoice
{
	Preferred,		// the entry's own IWAD is present; nothing to explain
	Substitute,		// standing in a free IWAD, and the selection supplies its own maps
	SubstituteRisky,// standing in a free IWAD for STOCK levels: different geometry, joins will fail
	None,			// neither is present, so this cannot be hosted
};

struct IwadPick
{
	IwadChoice choice;
	std::string iwad;		// what to hand the server; "" when None
	std::string wanted;		// what the entry asked for, for the message

	IwadPick() : choice(IwadChoice::None) {}
};

// `available` is the bare IWAD filenames the host actually has, matched case-insensitively because
// doom2.WAD and doom2.wad are the same file to everyone except a string compare.
//
// `selectionSuppliesMaps` should be true when any selected addon fills the `maps` slot. Passing it
// in rather than reaching for the selection keeps this unit pure and keeps the slot rule in one
// place.
IwadPick PickIwad(const std::string &preferred,
                  const std::vector<std::string> &available,
                  bool selectionSuppliesMaps);

} // namespace zx

#endif // ZX_IWADPICK_COMPUTE_H
