// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a console command may write a file and whether a menu item's command is trusted, kept engine-free so it is unit-tested.

#ifndef ZX_COMMANDSAFETY_COMPUTE_H
#define ZX_COMMANDSAFETY_COMPUTE_H

#include <initializer_list>
#include <string>

namespace zx {

// [rc4l] `name` when it ends in one of `exts` (such as ".log", in any case), else with the first appended, so a command writes only its own kind of file.
std::string ComputeOwnExtension(const std::string &name, std::initializer_list<const char *> exts);

// [rc4l] `name` as one plain file name: each character a path, a drive, an extension or Windows would read (`.` `/` `\` `:` `*` `?` `"` `<` `>` `|` and control characters) becomes `-`.
std::string ComputePlainFileName(const std::string &name);

// [rc4l] Whether `name` names a file alone, with no folder or drive in it (`/`, `\` or `:`), which save and load then look for in the save directory.
bool ComputeBareFileName(const std::string &name);

// [rc4l] The file `name` with extension `ext` in the folder holding `anchor`, `name` made plain first (a trailing `ext` dropped); empty when nothing of the name is left.
std::string ComputeFileBeside(const std::string &anchor, const std::string &name, const std::string &ext);

// [rc4l] Whether a menu item's command runs in an unsafe context: always, unless the item asks for confirmation and comes from the engine's own pk3, which loads before the IWAD.
bool ComputeMenuCommandUnsafe(bool confirmed, int lumpFile, int iwadFile);

} // namespace zx

#endif
