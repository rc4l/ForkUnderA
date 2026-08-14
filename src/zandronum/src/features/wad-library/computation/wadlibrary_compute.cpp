// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-library/computation/wadlibrary_compute.h"

#include <algorithm>
#include <cstring>

namespace zx
{

namespace
{

char Lower(char c)
{
	if ((c >= 'A') && (c <= 'Z'))
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

std::string LowerCopy(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out += Lower(s[i]);
	return out;
}

// The extension, lower case, without the dot. Empty when there is no dot in the last component.
std::string ExtensionOf(const std::string &name)
{
	const size_t dot = name.find_last_of('.');
	if ((dot == std::string::npos) || (dot + 1 >= name.size()))
		return std::string();

	return LowerCopy(name.substr(dot + 1));
}

} // namespace

bool IsLoadableWadName(const std::string &name)
{
	// [rc4l] What the engine can actually be handed with -file. A dehacked patch is included because
	// people do load them alongside a wad, and .zip because a pk3 IS a zip and plenty are shipped
	// under the plain extension.
	static const char *const kLoadable[] = { "wad", "pk3", "pk7", "ipk3", "ipk7", "zip", "7z",
		"deh", "bex", "lmp" };

	const std::string ext = ExtensionOf(name);
	if (ext.empty())
		return false;

	for (size_t i = 0; i < sizeof(kLoadable) / sizeof(kLoadable[0]); ++i)
	{
		if (ext == kLoadable[i])
			return true;
	}

	return false;
}

bool IsEngineOwnedName(const std::string &name)
{
	const std::string lower = LowerCopy(name);

	// [rc4l] Prefix rather than exact, because the core pk3 carries the version in its name and a
	// list matching only today's spelling would start offering the old one the day it changes.
	static const char *const kPrefixes[] = { "fua_core", "zandronum", "skulltag_actors",
		"brightmaps", "game_support", "lights", "voxels" };

	for (size_t i = 0; i < sizeof(kPrefixes) / sizeof(kPrefixes[0]); ++i)
	{
		const size_t n = std::strlen(kPrefixes[i]);
		if ((lower.size() >= n) && (lower.compare(0, n, kPrefixes[i]) == 0))
			return true;
	}

	return false;
}

const std::vector<std::string> &KnownIwadNames()
{
	// [rc4l] The games this engine can run, by the filenames they ship under. Exact matches only:
	// "doom2.wad" is an IWAD and "doom2_extras.wad" is not, and a prefix test cannot tell them
	// apart. A file that IS an IWAD under some other name simply appears in this list, which costs
	// one confusing row and never costs a player their pick.
	//
	// Built once on the first ask rather than declared at namespace scope, because a namespace-scope
	// container has an initialisation order nobody controls.
	static std::vector<std::string> names;

	if (names.empty())
	{
		static const char *const kIwads[] = {
			"doom.wad", "doom1.wad", "doom2.wad", "doom2f.wad", "doomu.wad", "bfgdoom.wad",
			"bfgdoom2.wad", "plutonia.wad", "tnt.wad", "freedoom1.wad", "freedoom2.wad",
			"freedm.wad", "heretic.wad", "heretic1.wad", "hexen.wad", "hexdd.wad", "strife1.wad",
			"strife0.wad", "chex.wad", "chex3.wad", "action2.wad", "harm1.wad", "hacx.wad",
			"square1.pk3", "delaweare.wad",
		};

		for (size_t i = 0; i < sizeof(kIwads) / sizeof(kIwads[0]); ++i)
			names.push_back(kIwads[i]);
	}

	return names;
}

bool IsKnownIwadName(const std::string &name)
{
	const std::string lower = LowerCopy(name);
	const std::vector<std::string> &known = KnownIwadNames();

	for (size_t i = 0; i < known.size(); ++i)
	{
		if (lower == known[i])
			return true;
	}

	return false;
}

std::string SearchFold(const std::string &text)
{
	std::string out;
	out.reserve(text.size());

	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];

		// [rc4l] Letters and digits only. Everybody spells a version differently -- "brutalv21",
		// "brutal_v21", "brutal-v21", "brutal v21" -- and a search that made the player guess which
		// one the file used would be a search that mostly fails.
		if (((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')))
			out += c;
		else if ((c >= 'A') && (c <= 'Z'))
			out += static_cast<char>(c - 'A' + 'a');
	}

	return out;
}

bool LibraryMatches(const LibraryFile &file, const std::string &key)
{
	if (key.empty())
		return true;

	return file.key.find(key) != std::string::npos;
}

bool LibraryFileLess(const LibraryFile &a, const LibraryFile &b)
{
	if (a.key != b.key)
		return a.key < b.key;
	if (a.size != b.size)
		return a.size < b.size;

	return a.path < b.path;
}

namespace
{

// The same order, reached through the row's index. One comparison, so the scanner's sort and this
// one cannot drift apart.
struct ByName
{
	const std::vector<LibraryFile> *files;

	explicit ByName(const std::vector<LibraryFile> &f) : files(&f) {}

	bool operator()(const LibraryRow &a, const LibraryRow &b) const
	{
		return LibraryFileLess((*files)[a.index], (*files)[b.index]);
	}
};

} // namespace

std::vector<LibraryRow> BuildLibraryRows(const std::vector<LibraryFile> &files,
	const std::string &key)
{
	// [rc4l] SORT, THEN MERGE NEIGHBOURS. Deduplicating by searching the rows built so far is
	// quadratic, which at the twenty thousand files this is designed for is four hundred million
	// comparisons and precisely the freeze the whole feature exists to avoid. Sorting is the step
	// we need anyway, and it puts every copy of one file next to its twins for free.
	std::vector<LibraryRow> out;
	out.reserve(files.size());

	for (size_t i = 0; i < files.size(); ++i)
	{
		if (LibraryMatches(files[i], key))
			out.push_back(LibraryRow(i, 1));
	}

	// [rc4l] Skipped when the input already arrives in this order, which is the ordinary case: the
	// scan sorts once when it finishes, and filtering preserves order, so every keystroke after
	// that would otherwise re-sort twenty thousand rows to reach the order they were already in.
	// The check is one linear pass against a sort that is not, and it keeps this correct for a
	// caller that has not sorted anything.
	if (!std::is_sorted(out.begin(), out.end(), ByName(files)))
		std::sort(out.begin(), out.end(), ByName(files));

	// The first of each run survives and counts the rest. ByName orders by path last, so which copy
	// that is does not depend on the order the scan happened to produce.
	size_t write = 0;
	for (size_t read = 0; read < out.size(); ++read)
	{
		const LibraryFile &here = files[out[read].index];

		if (write > 0)
		{
			const LibraryFile &prev = files[out[write - 1].index];
			if ((prev.key == here.key) && (prev.size == here.size))
			{
				++out[write - 1].copies;
				continue;
			}
		}

		out[write++] = out[read];
	}

	out.resize(write);
	return out;
}

size_t LibraryFileCap()
{
	// [rc4l] Twenty thousand is the number this was designed against, and the cap is well above it
	// so a big collection is listed rather than truncated. It exists for the search path that
	// contains a drive root, where the alternative is a scan that never ends.
	return 100000;
}

int LibraryDepthCap()
{
	// Deep enough for how people actually file things -- wads/<game>/<pack>/ -- and shallow enough
	// that pointing this at a home directory does not walk everything in it.
	return 5;
}

} // namespace zx
