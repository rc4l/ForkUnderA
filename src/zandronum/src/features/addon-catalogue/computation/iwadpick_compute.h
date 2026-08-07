// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which IWAD to actually host a catalogue entry on.
//
// An entry names the IWAD it was built for -- duel40b says doom2.wad -- and that is a PREFERENCE,
// not a requirement. Owning doom2.wad always means hosting on doom2.wad. Only when it is absent does
// features/wad-download's substitute table get asked, and it answers freedoom2.wad.
//
// Nothing about this breaks a join. The join-side warning in zx_joinserver.cpp exists because the
// CLIENT substitutes while the SERVER runs the real thing, so the two disagree about the level. A
// host that substitutes makes no such mistake: the server runs freedoom2, advertises freedoom2, and
// every client loads freedoom2.
//
// What is left is a content surprise, and only when the selection has no maps of its own: the host
// asked for Doom II's levels and will get Freedoom's, which are different levels. A selection that
// brings its own maps never touches a stock level and nobody can tell. That distinction is already
// in the slot model, so it is derived rather than declared.

#ifndef ZX_IWADPICK_COMPUTE_H
#define ZX_IWADPICK_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

enum class IwadChoice
{
	Preferred,			// the entry's own IWAD is present; nothing to explain
	Substitute,			// standing in a free IWAD, and the selection supplies its own maps
	SubstituteOwnMaps,	// standing in for STOCK levels: it works, but they are Freedoom's levels
	None,				// neither is present, so this cannot be hosted
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
