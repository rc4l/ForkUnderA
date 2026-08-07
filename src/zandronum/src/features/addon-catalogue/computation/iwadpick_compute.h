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
// Naming the substitute is therefore the whole story. If a selection has no maps of its own the
// levels come from the IWAD, so "hosting on freedoom2.wad" already says they will be Freedoom's;
// a caller wanting to spell that out knows whether it picked any maps without being told.

#ifndef ZX_IWADPICK_COMPUTE_H
#define ZX_IWADPICK_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

enum class IwadChoice
{
	Preferred,	// the entry's own IWAD is present; nothing to explain
	Substitute,	// a free IWAD stands in, and `iwad` says which
	None,		// neither is present, so this cannot be hosted
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
IwadPick PickIwad(const std::string &preferred, const std::vector<std::string> &available);

} // namespace zx

#endif // ZX_IWADPICK_COMPUTE_H
