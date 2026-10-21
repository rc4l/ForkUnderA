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

// [rc4l] Every remix on disk, read once and cached beside the entries.
//
// One pool rather than a copy per entry, because the same few apply to many: "three lives" means the
// same thing to every invasion pack. Entries name the ones they can take and this holds what they
// actually do.
const std::vector<AddonRemix> &CatalogueRemixes( bool bForceReload = false );

// The full path to a remix's cfg, or "" when it has none to exec. Remixes live in their own folders
// under remix/, so this is not the entry's directory.
std::string CatalogueRemixCfgPath( const std::string &remixId );

// [rc4l] The picture that stands in for a name, or "" when there is none to draw.
//
// Optional everywhere and absent for most things: a pack only has one if its own files carried
// something worth showing. "" is the ordinary answer and means "use the text", not "something went
// wrong".
//
// Resolved against the roots in the same order everything else is, so a player who drops their own
// copy of an experience beside the shipped one gets their own picture with it.
std::string CatalogueArtPath( const CatalogueEntry &entry, const std::string &variantId = std::string( ));
std::string CatalogueRemixArtPath( const std::string &remixId );

// [rc4l] Read the catalogue during startup and say, loudly, if anything in it could not be used.
//
// Called once from D_DoomMain. Without it the first read happens when somebody opens the host screen,
// which is both too late to be called a startup error and too far from the console to be seen.
void CatalogueCheckAtStartup( void );

// [rc4l] Every IWAD the host could run an entry on, for PickIwad.
//
// `preferred` is what the entry asked for, and it is always probed: a fixed list of the classics
// cannot name a game that ships its own iwad, and an entry declaring one was told there was nothing
// to run on while the file sat beside the executable.
std::vector<std::string> AvailableIwads( const std::string &preferred );

// The two places entries are read from, in order. Exposed so a player can be TOLD where to put one
// rather than having to guess.
std::string CatalogueShippedDir( void );
std::string CatalogueUserDir( void );

} // namespace zx

#endif // ZX_CATALOGUE_H
