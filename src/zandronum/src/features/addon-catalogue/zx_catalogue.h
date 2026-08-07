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
	AddonEntry addon;		// what addon.json said
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

// The full path to an entry's server.cfg, or "" when it has none. Only meaningful for an entry that
// came out of CatalogueLoad.
std::string CatalogueServerCfgPath( const CatalogueEntry &entry );

// The two places entries are read from, in order. Exposed so a player can be TOLD where to put one
// rather than having to guess.
std::string CatalogueShippedDir( void );
std::string CatalogueUserDir( void );

} // namespace zx

#endif // ZX_CATALOGUE_H
