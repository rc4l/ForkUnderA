// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where a player's own presets live: a catalogue they wrote themselves.
//
//   <user>/custompresets/<name>/addon.json     the entry, in the catalogue's own schema
//   <user>/custompresets/<name>/server.cfg     its settings and rotation
//   <user>/custompresets/<name>/art.png        optional, and read if it is there
//
// Under the same per-user folder as iwads/ and pwads/, so everything this install keeps for the
// player is in one place they can find, back up, or hand to somebody else. A folder IS a preset,
// which is what makes sharing one a matter of copying it.
//
// NO MASTER INDEX. The folder listing is the index: a preset dropped in by hand appears and one
// deleted disappears, with no second file to keep in step and nothing to repair when they disagree.
//
// The last configuration played is kept the same way, under a folder nothing lists. That is what
// makes the NEW screen survive a wad reload -- hosting makes the client reload its files to match
// the server it just started, and anything held only in memory across that is a bet.

#ifndef ZX_CUSTOMSTORE_H
#define ZX_CUSTOMSTORE_H

#include <string>
#include <vector>

#include "features/server-browser/computation/customsave_compute.h"

namespace zx
{

// The folder every preset lives under, with a trailing separator. Made when something is written.
std::string CustomRoot();

// The names already saved, sorted, so the CUSTOM tab never shuffles.
std::vector<std::string> CustomNames();

// One by name. The returned entry has an empty name when there is no such preset or it cannot be
// read -- a preset that fails the catalogue's own rules is not offered rather than half-offered.
CustomEntry CustomLoad(const std::string &name);

// Everything saved, in name order.
std::vector<CustomEntry> CustomAll();

// Where a preset's art would be, whether or not it is there.
std::string CustomArtPath(const std::string &name);

// Writes the folder, replacing what is there. False when the name is refused or a write fails.
bool CustomSave(const CustomEntry &entry);

// Removes the whole folder. True when it is gone, including when it was never there.
bool CustomDelete(const std::string &name);

// [rc4l] The unnamed one: what the NEW screen last had. Written on every play, without asking.
bool CustomSaveLast(const CustomEntry &entry);
CustomEntry CustomLoadLast();

} // namespace zx

#endif // ZX_CUSTOMSTORE_H
