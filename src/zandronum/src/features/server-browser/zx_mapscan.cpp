// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_mapscan.h for why a file is opened on its own, and maplist_compute.h for the rules.
//
// SANITY-CHECKED AGAINST THE ENGINE THAT LOADS THEM, not against a launcher's guess. P_OpenMapData
// finds a map by its header lump and then walks GetMapIndex's table, where index 1 is THINGS and is
// REQUIRED -- or the lump right after the header is TEXTMAP, the UDMF case it checks first. So "the
// next lump is THINGS or TEXTMAP" is the engine's own gate, and a rotation built on it cannot name
// something the server will then refuse to open. It also looks up maps/<name>.wad and
// maps/<name>.map by full name, which is the archive rule.
//
// SPEED. Two costs: opening the file and walking its directory. Opening reads a WAD's lump table or
// a zip's central directory and nothing else -- no map data, no textures, no sounds -- so a 100MB
// pk3 costs the same as a 1MB one plus its entry count. The walk is linear and does no allocation
// of its own. What would be slow is doing it per frame, so it is done ONCE when the box is opened
// and cached against the load order; see the menu.

#include "features/server-browser/zx_mapscan.h"

#include "features/server-browser/computation/maplist_compute.h"

#include "resourcefiles/resourcefile.h"
#include "cmdlib.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "i_system.h"
#include "m_swap.h"
#include "v_text.h"
#include "w_wad.h"

#include <stdio.h>
#include <string.h>

namespace zx
{

namespace
{

// [rc4l] A plain WAD, read the short way: the twelve-byte header, one seek, and the whole lump
// directory in a single read.
//
// FResourceFile below is correct for every format the engine supports, and that generality is what
// it costs -- it builds an object per entry, names it, and checks it for embedded files. Measured
// on this machine: a 900MB wad took 4ms through it and a 290MB pk3 took 73ms. A wad's directory is
// sixteen bytes an entry and needs none of that, so the common case (an IWAD, a map pack) is done
// here with no allocation per lump and no work beyond the read.
//
// Returns false when this is not a wad at all, which sends the caller to the general reader rather
// than failing.
bool ReadWadDirectory(const std::string &path, std::vector<LumpEntry> &out)
{
	FILE *const fp = fopen(path.c_str(), "rb");
	if (fp == NULL)
		return false;

	wadinfo_t header;
	if (fread(&header, sizeof(header), 1, fp) != 1)
	{
		fclose(fp);
		return false;
	}

	header.Magic = LittleLong(header.Magic);
	header.NumLumps = LittleLong(header.NumLumps);
	header.InfoTableOfs = LittleLong(header.InfoTableOfs);

	if ((header.Magic != IWAD_ID) && (header.Magic != PWAD_ID))
	{
		fclose(fp);
		return false;
	}

	// A directory bigger than any real wad is a file claiming to be something it is not.
	if ((header.NumLumps == 0) || (header.NumLumps > 65536 * 4))
	{
		fclose(fp);
		return false;
	}

	if (fseek(fp, header.InfoTableOfs, SEEK_SET) != 0)
	{
		fclose(fp);
		return false;
	}

	std::vector<wadlump_t> dir(header.NumLumps);
	const size_t read = fread(&dir[0], sizeof(wadlump_t), header.NumLumps, fp);
	fclose(fp);

	if (read != header.NumLumps)
		return false;

	out.reserve(dir.size());

	for (size_t i = 0; i < dir.size(); ++i)
	{
		// The name is eight characters and NOT necessarily terminated, which is the one thing
		// everybody gets wrong when they read this table by hand.
		char name[9];
		memcpy(name, dir[i].Name, 8);
		name[8] = 0;

		out.push_back(LumpEntry(name, ""));
	}

	return true;
}

// [rc4l] What has already been read, so a file is opened once a session rather than once a look.
//
// The wad path above is free and the general one is not: a 290MB pk3 measured 69ms, which is a
// frame nobody wants dropped every time a list is redrawn. Keyed on the size as well as the path,
// so a file replaced under the same name is read again rather than remembered wrongly.
struct ScanCacheEntry
{
	std::string path;
	long size;
	std::vector<std::string> maps;
};

std::vector<ScanCacheEntry> g_scanned;

long SizeOf(const std::string &path)
{
	FILE *const fp = fopen(path.c_str(), "rb");
	if (fp == NULL)
		return -1;

	fseek(fp, 0, SEEK_END);
	const long size = ftell(fp);
	fclose(fp);

	return size;
}

} // namespace

std::vector<std::string> MapsInPath(const std::string &path)
{
	std::vector<std::string> out;

	if (path.empty() || !FileExists(path.c_str()))
		return out;

	const long size = SizeOf(path);

	for (size_t i = 0; i < g_scanned.size(); ++i)
	{
		if ((g_scanned[i].path == path) && (g_scanned[i].size == size))
			return g_scanned[i].maps;
	}

	{
		std::vector<LumpEntry> lumps;
		if (ReadWadDirectory(path, lumps))
		{
			ScanCacheEntry entry;
			entry.path = path;
			entry.size = size;
			entry.maps = MapsInFile(lumps);

			g_scanned.push_back(entry);
			return entry.maps;
		}
	}

	// [rc4l] quiet, because this is a question rather than a load: a file the player has that this
	// cannot read is not an error to announce, it simply has no maps to offer.
	FResourceFile *const file = FResourceFile::OpenResourceFile(path.c_str(), NULL, true);
	if (file == NULL)
		return out;

	std::vector<LumpEntry> lumps;
	lumps.reserve(file->LumpCount());

	for (DWORD i = 0; i < file->LumpCount(); ++i)
	{
		FResourceLump *const lump = file->GetLump(i);
		if (lump == NULL)
			continue;

		// FullName is set only for entries that HAVE a path; a plain WAD lump has just its eight
		// characters, which is the difference the two rules turn on.
		// [rc4l] FullName became an FString upstream, so an empty one -- not a null pointer -- is
		// now what "this lump has no path" looks like. GetChars() on an empty FString is "".
		lumps.push_back(LumpEntry(lump->Name, lump->FullName.GetChars()));
	}

	delete file;

	ScanCacheEntry entry;
	entry.path = path;
	entry.size = size;
	entry.maps = MapsInFile(lumps);

	g_scanned.push_back(entry);
	return entry.maps;
}

} // namespace zx

// [rc4l] The scan, printed, with what it cost. This is how the cost was measured rather than
// asserted: run it against the biggest pk3 on the machine and read the number.
CCMD( fua_maps )
{
	if ( argv.argc( ) < 2 )
	{
		Printf( "fua_maps <file>: the maps in a wad or pk3, without loading it\n" );
		return;
	}

	const unsigned int began = I_MSTime( );
	const std::vector<std::string> maps = zx::MapsInPath( argv[1] );
	const unsigned int took = I_MSTime( ) - began;

	Printf( "%d map(s) in %s, read in %ums\n", static_cast<int>( maps.size( )), argv[1], took );

	FString line;
	for ( size_t i = 0; i < maps.size( ); ++i )
	{
		line += maps[i].c_str( );
		line += " ";

		if ((( i + 1 ) % 10 ) == 0 )
		{
			Printf( "    %s\n", line.GetChars( ));
			line = "";
		}
	}

	if ( line.IsNotEmpty( ))
		Printf( "    %s\n", line.GetChars( ));
}
