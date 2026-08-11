// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Loading the catalogue off disk. The only part of this feature that touches the filesystem;
// everything it decides is settled in computation/.

#ifndef ZX_CATALOGUE_H
#define ZX_CATALOGUE_H

#include "features/addon-catalogue/computation/addonfile_compute.h"

#include <string>
#include <vector>

namespace zx
{

struct CatalogueEntry
{
	AddonEntry addon;		// what addon.json said, variants included
	std::string dir;		// the folder it came from, so server.cfg can be found beside it
	bool hasServerCfg;
	bool shipped;			// ours, next to the exe, rather than the player's

	CatalogueEntry() : hasServerCfg(false), shipped(false) {}
};

// Reads the catalogue, shipped first and then the player's, and caches the result. A second call
// returns the cache; pass true to re-read after someone has added a folder without restarting.
//
// A folder that fails to parse is skipped with a message rather than taking the whole catalogue down
// with it: one bad entry a player dropped in must not cost them the rest of the list.
const std::vector<CatalogueEntry> &CatalogueLoad( bool bForceReload = false );

// The full path to the cfg an entry should be hosted with, or "" when it has none. `variantId` is
// the way of playing the player chose; empty means "no preference", which lands on the entry's
// default. An entry with no variants answers with its server.cfg whatever is passed.
std::string CatalogueServerCfgPath( const CatalogueEntry &entry, const std::string &variantId = std::string( ));

// [rc4l] Read the catalogue during startup and say, loudly, if anything in it could not be used.
//
// Called once from D_DoomMain. Without it the first read happens when somebody opens the host screen,
// which is both too late to be called a startup error and too far from the console to be seen.
void CatalogueCheckAtStartup( void );

// The two places entries are read from, in order. Exposed so a player can be TOLD where to put one
// rather than having to guess.
std::string CatalogueShippedDir( void );
std::string CatalogueUserDir( void );

} // namespace zx

#endif // ZX_CATALOGUE_H
