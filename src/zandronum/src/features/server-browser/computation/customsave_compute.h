// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] A server configuration the player built and named, written as a catalogue entry of their
// own.
//
// SAME SHAPE AS THE CATALOGUE, deliberately. A saved preset is a folder holding an addon.json and a
// server.cfg, exactly as catalogue/doomware does, so:
//
//   * the entry can be read by the code that already reads the catalogue rather than by a second
//     reader written here;
//   * a missing file resolves through the SAME download path a shipped preset uses, because the
//     files are named the way that path expects -- a name and an md5;
//   * one player can hand another a folder and it works, art included. A custom catalogue is what
//     this is, so it is stored as one.
//
// STRIPPED DOWN, because most of the catalogue's schema is about curation this has no use for:
// no variants, no remixes, no ordering, no summary written by somebody who was not there. What is
// left is what a server needs and what a list needs to draw a row.
//
// There is no master index. The folder listing IS the index: a preset dropped in by hand appears,
// and one deleted disappears, without a second file to keep in step.
//
// THE NAME QUESTION is a state machine because it has a memory: the first Confirm on a taken name
// is a question and the second is the answer, and changing the name forgets the question because it
// is no longer the same question.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_CUSTOMSAVE_COMPUTE_H
#define ZX_CUSTOMSAVE_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx
{

// One file a preset needs, named the way the download path names them.
struct CustomFile
{
	std::string name;		// bare filename, as the server will be told
	std::string md5;		// lower-case hex, empty when it could not be worked out

	CustomFile() {}
	CustomFile(const std::string &n, const std::string &hash) : name(n), md5(hash) {}
};

// One saved configuration.
struct CustomEntry
{
	std::string name;						// what the player called it, and the folder name
	std::string iwad;						// bare name, as a catalogue entry states it
	std::vector<CustomFile> files;			// load order
	std::vector<std::string> maps;			// the rotation, in order, only what is in play
	std::string gameMode;					// the mode's cvar name, empty for the engine default

	// [rc4l] pve or pvp, which the catalogue REQUIRES of every entry: an unlabelled experience is
	// what that field exists to stop, and a preset built by hand is not exempt. Derived from the
	// chosen gamemode by the caller, which is the only place that knows.
	bool bPvP;

	// Everything the settings boxes decided, as name and value: flag fields as their numbers, the
	// limits, the skill. These become server.cfg lines.
	std::vector<std::pair<std::string, std::string> > cvars;

	CustomEntry() : bPvP(false) {}
};

// The two files a preset is. Text in, text out, so both are testable without a filesystem.
std::string CustomAddonJson(const CustomEntry &entry);
std::string CustomServerCfg(const CustomEntry &entry);

// [rc4l] The cvars back out of a server.cfg. Comments and blank lines are skipped, and addmap lines
// are returned separately because a rotation is not a setting.
void ParseCustomCfg(const std::string &text,
	std::vector<std::pair<std::string, std::string> > &cvars, std::vector<std::string> &maps);

// [rc4l] Whether a name may be saved at all.
//
// The name IS the folder name, so anything a path could read as structure is refused rather than
// escaped: a preset called "../../boot" would be a save that writes where it was not invited.
bool IsCustomName(const std::string &name);

// [rc4l] What the save box is currently asking.
enum class SaveState
{
	Fresh,			// nothing said yet
	Empty,			// the box is empty, so there is nothing to save
	Bad,			// a name that cannot be a folder
	NoFiles,		// nothing to save: a preset is the files, and there are none
	Asking,			// the name is taken and the player has been asked whether to replace
	Ready,			// pressing Confirm now saves
	Replace,		// pressing Confirm now REPLACES what is there
};

// [rc4l] What Confirm should do next, given the name, the names already saved, whether the player
// has already been asked once about this name, and how many files the configuration loads.
//
// The file count is here because a preset with none cannot be written as a catalogue entry at all:
// that schema requires a file with a hash, which is exactly what lets a missing one be fetched
// later. Refusing at the box says so where it can be read, rather than writing a folder that the
// catalogue reader then quietly skips.
SaveState NextSaveState(const std::string &name, const std::vector<std::string> &taken,
	bool bAlreadyAsked, size_t fileCount);

// The line under the box for a state. Empty where there is nothing to say.
const char *SaveStatusText(SaveState state);

// Whether that state is one the player should read as a refusal rather than as progress. The box
// colours it accordingly, which is why this is not left to the caller's judgement.
bool SaveStatusIsWarning(SaveState state);

} // namespace zx

#endif // ZX_CUSTOMSAVE_COMPUTE_H
