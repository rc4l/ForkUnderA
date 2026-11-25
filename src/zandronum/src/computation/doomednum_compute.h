// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
//
// [MGOOOOOO] Whether a DECORATE actor's editor number replaces the entry already in DoomEdMap.

#ifndef ZX_DOOMEDNUM_COMPUTE_H
#define ZX_DOOMEDNUM_COMPUTE_H

namespace zx {

// [MGOOOOOO] What RegisterIDs does with a DECORATE editor number.
enum class DecorateEdNumClaim
{
	// [MGOOOOOO] A free number, or a MAPINFO entry that names a class.
	Take,
	// [MGOOOOOO] A number another DECORATE actor already took.
	TakeAndWarnTwice,
	// [MGOOOOOO] A MAPINFO special mapthing (a player or deathmatch start, a polyobject spot, a sound sequence), which Zandronum matched by number before any actor.
	KeepSpecial,
};

// [MGOOOOOO] existingSpecial is the entry's Special: -2 for a DECORATE entry, above 0 for a special mapthing when the entry names no class.
DecorateEdNumClaim ComputeDecorateEdNumClaim(bool hasExisting, bool existingNamesClass, int existingSpecial);

} // namespace zx

#endif
