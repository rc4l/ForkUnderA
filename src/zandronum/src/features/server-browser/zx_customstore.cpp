// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_customstore.h for the layout and why there is no index.

#include "features/server-browser/zx_customstore.h"

#include <algorithm>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "features/addon-catalogue/computation/addonfile_compute.h"
#include "features/wad-download/zx_waddownload.h"
#include "i_system.h"

#include "cmdlib.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "m_misc.h"
#include "v_text.h"
#include "zstring.h"

namespace zx
{

namespace
{

// [rc4l] The folder the last-played configuration lives in.
//
// A leading dot so it sorts away from the player's own presets on every listing they will ever see,
// and CustomNames skips it by name regardless -- "what you saved" and "what you last played" are
// different lists and only one of them is the CUSTOM tab.
const char *const kLastFolder = ".last";

std::string ReadWholeFile(const std::string &path)
{
	FILE *const fp = fopen(path.c_str(), "rb");
	if (fp == NULL)
		return std::string();

	std::string out;
	char buffer[4096];

	for (;;)
	{
		const size_t read = fread(buffer, 1, sizeof(buffer), fp);
		if (read == 0)
			break;

		out.append(buffer, read);
	}

	fclose(fp);
	return out;
}

bool WriteWholeFile(const std::string &path, const std::string &text)
{
	FILE *const fp = fopen(path.c_str(), "wb");
	if (fp == NULL)
		return false;

	const bool bOk = text.empty() || (fwrite(text.data(), 1, text.size(), fp) == text.size());

	fclose(fp);
	return bOk;
}

// A preset's own folder, with a trailing separator. Empty for a name this refuses.
std::string FolderFor(const std::string &name)
{
	if (!IsCustomName(name))
		return std::string();

	return CustomRoot() + name + "/";
}

// Reads one folder into an entry. `id` is the folder name, which is also the preset's name: the
// file may disagree, and then the folder wins, because renaming the folder is how somebody renames
// a preset.
CustomEntry LoadFolder(const std::string &folder, const std::string &id)
{
	CustomEntry out;

	const std::string json = ReadWholeFile(folder + "addon.json");
	if (json.empty())
		return out;

	// [rc4l] THE CATALOGUE'S OWN PARSER, which is the point of the shape. A preset that does not
	// satisfy it is not offered at all -- half a preset is a row that fails when it is pressed.
	const AddonEntry parsed = ParseAddonFile(id, json);
	if (parsed.name.empty())
		return out;

	out.name = id;
	out.iwad = parsed.iwad;
	out.bPvP = (parsed.kind == VariantKind::PvP);

	for (size_t i = 0; i < parsed.files.size(); ++i)
		out.files.push_back(CustomFile(parsed.files[i].name, parsed.files[i].md5));

	// The settings and the rotation come from the cfg, which is where a server would read them.
	ParseCustomCfg(ReadWholeFile(folder + "server.cfg"), out.cvars, out.maps);

	// The gamemode is a cfg line like any other; lifting it out is what the NEW screen needs to
	// put the mode pill back where it was.
	for (size_t i = 0; i < out.cvars.size(); ++i)
	{
		if (( out.cvars[i].second != "1") && (out.cvars[i].second != "true"))
			continue;

		static const char *const kModes[] = { "cooperative", "survival", "invasion", "deathmatch",
			"teamplay", "duel", "terminator", "lastmanstanding", "teamlms", "possession",
			"teampossession", "teamgame", "ctf", "oneflagctf", "skulltag", "domination" };

		for (size_t m = 0; m < sizeof(kModes) / sizeof(kModes[0]); ++m)
		{
			if (out.cvars[i].first == kModes[m])
			{
				out.gameMode = kModes[m];
				break;
			}
		}
	}

	return out;
}

} // namespace

std::string CustomRoot()
{
	FString dir = M_GetFuaUserPath();
	if (dir.IsEmpty())
		dir = progdir;

	if ((dir.Len() > 0) && (dir[dir.Len() - 1] != '/') && (dir[dir.Len() - 1] != '\\'))
		dir += "/";

	dir += "custompresets/";
	return std::string(dir.GetChars());
}

std::vector<std::string> CustomNames()
{
	std::vector<std::string> out;

	// [rc4l] ScanDirectory calls I_Error on a folder that is not there, which for this is not an
	// error at all: nobody has saved a preset yet. Asked first, so an empty CUSTOM tab is an empty
	// list rather than a fatal box on the way to the menu.
	const std::string root = CustomRoot();
	if (!DirEntryExists(root.c_str()))
		return out;

	TArray<FFileList> files;
	ScanDirectory(files, root.c_str());

	for (unsigned int i = 0; i < files.Size(); ++i)
	{
		// A preset is a FOLDER holding an addon.json. ScanDirectory hands back the files it found
		// within, so the entry file is what names a preset rather than the directory rows.
		FString path = files[i].Filename;
		path.Substitute("\\", "/");

		const long slash = path.LastIndexOf('/');
		if (slash < 0)
			continue;

		FString leaf = path.Mid(slash + 1);
		if (stricmp(leaf.GetChars(), "addon.json") != 0)
			continue;

		FString parent = path.Left(slash);
		const long up = parent.LastIndexOf('/');
		if (up >= 0)
			parent = parent.Mid(up + 1);

		const std::string name = parent.GetChars();

		if (name.empty() || (name == kLastFolder) || !IsCustomName(name))
			continue;

		out.push_back(name);
	}

	// Alphabetical, and only once each: a folder cannot appear twice, but a listing is a listing.
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());

	return out;
}

CustomEntry CustomLoad(const std::string &name)
{
	const std::string folder = FolderFor(name);
	if (folder.empty())
		return CustomEntry();

	return LoadFolder(folder, name);
}

std::vector<CustomEntry> CustomAll()
{
	const std::vector<std::string> names = CustomNames();

	std::vector<CustomEntry> out;
	out.reserve(names.size());

	for (size_t i = 0; i < names.size(); ++i)
	{
		const CustomEntry entry = CustomLoad(names[i]);
		if (!entry.name.empty())
			out.push_back(entry);
	}

	return out;
}

std::string CustomArtPath(const std::string &name)
{
	const std::string folder = FolderFor(name);
	if (folder.empty())
		return std::string();

	return folder + "art.png";
}

bool CustomSave(const CustomEntry &entry)
{
	const std::string folder = FolderFor(entry.name);
	if (folder.empty())
		return false;

	CreatePath(folder.c_str());

	if (!WriteWholeFile(folder + "addon.json", CustomAddonJson(entry)))
		return false;

	return WriteWholeFile(folder + "server.cfg", CustomServerCfg(entry));
}

bool CustomDelete(const std::string &name)
{
	const std::string folder = FolderFor(name);
	if (folder.empty())
		return false;

	// The three files this writes, and then the folder. Not a recursive delete: this removes what
	// it put there, and a folder somebody has put something else in stays.
	remove((folder + "addon.json").c_str());
	remove((folder + "server.cfg").c_str());
	remove((folder + "art.png").c_str());

	// The folder itself, once it is empty. Windows spells this one differently, and the failure
	// when something else is still in there is the behaviour wanted: this removes what it wrote.
#ifdef _WIN32
	_rmdir(folder.c_str());
#else
	rmdir(folder.c_str());
#endif

	return !FileExists((folder + "addon.json").c_str());
}

bool CustomSaveLast(const CustomEntry &entry)
{
	const std::string folder = CustomRoot() + kLastFolder + "/";

	CreatePath(folder.c_str());

	// [rc4l] Named, because the catalogue parser refuses an entry without one and this is read back
	// through that same parser. The name is never shown: nothing lists this folder.
	CustomEntry named = entry;
	if (named.name.empty())
		named.name = "Last played";

	if (!WriteWholeFile(folder + "addon.json", CustomAddonJson(named)))
		return false;

	return WriteWholeFile(folder + "server.cfg", CustomServerCfg(named));
}

CustomEntry CustomLoadLast()
{
	return LoadFolder(CustomRoot() + kLastFolder + "/", "Last played");
}

} // namespace zx

// [rc4l] What is saved and where. The path is the half of this nobody can guess.
CCMD( fua_custom )
{
	const unsigned int began = I_MSTime( );
	const std::vector<zx::CustomEntry> all = zx::CustomAll( );
	const unsigned int read = I_MSTime( ) - began;

	Printf( "%s\n", zx::CustomRoot( ).c_str( ));
	Printf( "%d saved preset(s), read in %ums\n", static_cast<int>( all.size( )), read );

	// [rc4l] What the CUSTOM tab's row colouring costs, which is the question that matters: this is
	// the walk-and-hash the drawing used to do PER ROW PER FRAME. Timed here so the number is a
	// measurement rather than a claim.
	for ( size_t i = 0; i < all.size( ); ++i )
	{
		const unsigned int one = I_MSTime( );

		int missing = 0;
		for ( size_t f = 0; f < all[i].files.size( ); ++f )
		{
			const FString path = zx::waddownload::FindVerifiedCopy( all[i].files[f].name.c_str( ),
				all[i].files[f].md5.empty( ) ? NULL : all[i].files[f].md5.c_str( ));

			if ( path.IsEmpty( ))
				missing++;
		}

		Printf( "    verifying %s: %d file(s), %d missing, %ums\n", all[i].name.c_str( ),
			static_cast<int>( all[i].files.size( )), missing, I_MSTime( ) - one );
	}

	for ( size_t i = 0; i < all.size( ); ++i )
	{
		Printf( "    " TEXTCOLOR_GOLD "%s" TEXTCOLOR_NORMAL "  %s, %d file(s), %d map(s), %s\n",
			all[i].name.c_str( ), all[i].iwad.c_str( ),
			static_cast<int>( all[i].files.size( )), static_cast<int>( all[i].maps.size( )),
			all[i].gameMode.empty( ) ? "default mode" : all[i].gameMode.c_str( ));
	}
}
