// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/addonfile_compute.h"

#include <cctype>
#include <cstdlib>

namespace zx
{

namespace
{

// A cursor over the text. Every read is bounds-checked against `end` rather than relying on a
// terminator, because the caller hands us whatever was in the file.
struct Reader
{
	const char *p;
	const char *end;

	bool Done() const { return p >= end; }
	char Peek() const { return Done() ? '\0' : *p; }

	void SkipSpace()
	{
		while (!Done() && std::isspace(static_cast<unsigned char>(*p)))
			++p;
	}

	bool Take(char c)
	{
		SkipSpace();
		if (Done() || *p != c)
			return false;
		++p;
		return true;
	}
};

// A JSON string, with the escapes that can appear in a filename or a summary. \u is deliberately
// refused: supporting it means committing to UTF-16 surrogate pairs, and no field here needs it.
bool ReadString(Reader &r, std::string &out)
{
	r.SkipSpace();
	if (!r.Take('"'))
		return false;

	out.clear();
	while (!r.Done())
	{
		const char c = *r.p++;

		if (c == '"')
			return true;

		if (c != '\\')
		{
			out += c;
			continue;
		}

		if (r.Done())
			return false;

		switch (*r.p++)
		{
		case '"':  out += '"';  break;
		case '\\': out += '\\'; break;
		case '/':  out += '/';  break;
		case 'b':  out += '\b'; break;
		case 'f':  out += '\f'; break;
		case 'n':  out += '\n'; break;
		case 'r':  out += '\r'; break;
		case 't':  out += '\t'; break;
		default:   return false;
		}
	}
	return false;	// ran out before the closing quote
}

bool ReadInt(Reader &r, int &out)
{
	r.SkipSpace();

	const char *start = r.p;
	if (!r.Done() && (*r.p == '-' || *r.p == '+'))
		++r.p;

	const char *digits = r.p;
	while (!r.Done() && std::isdigit(static_cast<unsigned char>(*r.p)))
		++r.p;

	if (r.p == digits)
		return false;

	out = std::atoi(std::string(start, r.p).c_str());
	return true;
}

// [rc4l] A JSON boolean, and ONLY a boolean. "default": 1 and "default": "yes" are refused rather
// than guessed at, because the one field this reads decides which way a pack plays by default and a
// guess there is a silent wrong answer rather than a loud one.
bool ReadBool(Reader &r, bool &out)
{
	r.SkipSpace();

	const size_t left = static_cast<size_t>(r.end - r.p);

	if ((left >= 4) && (std::string(r.p, r.p + 4) == "true"))
	{
		r.p += 4;
		out = true;
		return true;
	}

	if ((left >= 5) && (std::string(r.p, r.p + 5) == "false"))
	{
		r.p += 5;
		out = false;
		return true;
	}

	return false;
}

// Skip one value of any shape, so an unknown key does not have to be understood to be ignored.
bool SkipValue(Reader &r)
{
	r.SkipSpace();
	if (r.Done())
		return false;

	const char c = r.Peek();

	if (c == '"')
	{
		std::string ignored;
		return ReadString(r, ignored);
	}

	if (c == '{' || c == '[')
	{
		const char close = (c == '{') ? '}' : ']';
		++r.p;

		int depth = 1;
		while (!r.Done() && depth > 0)
		{
			const char d = r.Peek();

			if (d == '"')
			{
				std::string ignored;
				if (!ReadString(r, ignored))
					return false;
				continue;
			}

			if (d == '{' || d == '[')
				++depth;
			else if (d == '}' || d == ']')
				--depth;

			++r.p;
		}
		(void)close;
		return depth == 0;
	}

	// A bare literal: number, true, false, null. Consume to the next structural character.
	while (!r.Done() && *r.p != ',' && *r.p != '}' && *r.p != ']')
		++r.p;
	return true;
}

// Takes a std::string so a reason can NAME the thing it is about: a pack with six variants needs to
// be told which one is wrong, not that one of them is.
AddonEntry Fail(const std::string &id, const std::string &why)
{
	AddonEntry e;
	e.id = id;
	e.valid = false;
	e.error = why;
	return e;
}

bool ReadFilesArray(Reader &r, std::vector<AddonFileRef> &out)
{
	if (!r.Take('['))
		return false;

	r.SkipSpace();
	if (r.Take(']'))
		return true;	// an empty list parses; whether it is USEFUL is checked by the caller

	for (;;)
	{
		if (!r.Take('{'))
			return false;

		AddonFileRef ref;
		r.SkipSpace();

		if (!r.Take('}'))
		{
			for (;;)
			{
				std::string key;
				if (!ReadString(r, key) || !r.Take(':'))
					return false;

				if (key == "name")
				{
					if (!ReadString(r, ref.name))
						return false;
				}
				else if (key == "md5")
				{
					if (!ReadString(r, ref.md5))
						return false;
				}
				else if (!SkipValue(r))
				{
					return false;
				}

				if (r.Take(','))
					continue;
				if (r.Take('}'))
					break;
				return false;
			}
		}

		out.push_back(ref);

		if (r.Take(','))
			continue;
		if (r.Take(']'))
			return true;
		return false;
	}
}

// [rc4l] The label, spelled out rather than guessed. Anything else lands on Unknown, which the
// caller refuses by name: "kind is not pve or pvp" tells the author what to write, where a generic
// malformed-value would leave them hunting.
VariantKind ParseKind(const std::string &s)
{
	if (s == "pve")
		return VariantKind::PvE;
	if (s == "pvp")
		return VariantKind::PvP;

	return VariantKind::Unknown;
}

// [rc4l] The variants array. Same shape as the files array, and deliberately the same reading of an
// unknown key: skipped, so a catalogue written for a later build still loads here.
bool ReadVariantsArray(Reader &r, std::vector<AddonVariant> &out)
{
	if (!r.Take('['))
		return false;

	r.SkipSpace();
	if (r.Take(']'))
		return true;	// an empty array is caught by the caller, which knows it means nothing

	for (;;)
	{
		if (!r.Take('{'))
			return false;

		AddonVariant v;
		r.SkipSpace();

		if (!r.Take('}'))
		{
			for (;;)
			{
				std::string key;
				if (!ReadString(r, key) || !r.Take(':'))
					return false;

				if (key == "id")
				{
					if (!ReadString(r, v.id))
						return false;
				}
				else if (key == "name")
				{
					if (!ReadString(r, v.name))
						return false;
				}
				else if (key == "cfg")
				{
					if (!ReadString(r, v.cfg))
						return false;
				}
				else if (key == "tooltip")
				{
					if (!ReadString(r, v.tooltip))
						return false;
				}
				else if (key == "kind")
				{
					std::string kind;
					if (!ReadString(r, kind))
						return false;
					v.kind = ParseKind(kind);
				}
				else if (key == "files")
				{
					if (!ReadFilesArray(r, v.files))
						return false;
				}
				else if (key == "default")
				{
					if (!ReadBool(r, v.isDefault))
						return false;
				}
				else if (!SkipValue(r))
				{
					return false;
				}

				if (r.Take(','))
					continue;
				if (r.Take('}'))
					break;
				return false;
			}
		}

		out.push_back(v);

		if (r.Take(','))
			continue;
		if (r.Take(']'))
			return true;
		return false;
	}
}

bool LooksLikeMd5(const std::string &s)
{
	if (s.size() != 32)
		return false;

	for (size_t i = 0; i < s.size(); ++i)
	{
		const char c = s[i];
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
		if (!hex)
			return false;	// upper case is refused too: the store keys on one spelling
	}
	return true;
}

// A bare filename and nothing else. A path here is either a mistake or an attempt to reach outside
// the places the loader is meant to look, and the same rule already guards the host command line.
bool IsBareFilename(const std::string &s)
{
	if (s.empty())
		return false;
	if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
		return false;
	if (s.find("..") != std::string::npos)
		return false;
	if (s.find(':') != std::string::npos)
		return false;

	return true;
}

} // namespace

const char *DescribeVariantKind(VariantKind kind)
{
	switch (kind)
	{
	case VariantKind::PvE:
		return "PvE";
	case VariantKind::PvP:
		return "PvP";
	case VariantKind::Unknown:
		break;
	}
	return "Unlabelled";
}

AddonEntry ParseAddonFile(const std::string &id, const std::string &json)
{
	Reader r;
	r.p = json.c_str();
	r.end = r.p + json.size();

	if (!r.Take('{'))
		return Fail(id, "not a JSON object");

	AddonEntry entry;
	entry.id = id;

	int schema = 0;
	bool sawSchema = false;

	r.SkipSpace();
	if (!r.Take('}'))
	{
		for (;;)
		{
			std::string key;
			if (!ReadString(r, key) || !r.Take(':'))
				return Fail(id, "malformed key");

			bool ok = true;
			if (key == "schema")		{ ok = ReadInt(r, schema); sawSchema = true; }
			else if (key == "name")		{ ok = ReadString(r, entry.name); }
			else if (key == "summary")	{ ok = ReadString(r, entry.summary); }
			else if (key == "iwad")		{ ok = ReadString(r, entry.iwad); }
			else if (key == "map")		{ ok = ReadString(r, entry.map); }
			else if (key == "files")	{ ok = ReadFilesArray(r, entry.files); }
			else if (key == "variants")	{ ok = ReadVariantsArray(r, entry.variants); }
			else if (key == "kind")		{ std::string k; ok = ReadString(r, k); entry.kind = ParseKind(k); }
			else						{ ok = SkipValue(r); }

			if (!ok)
				return Fail(id, "malformed value");

			if (r.Take(','))
				continue;
			if (r.Take('}'))
				break;
			return Fail(id, "malformed object");
		}
	}

	// Trailing content means the file is not what it appears to be, so it is refused rather than
	// half-read.
	r.SkipSpace();
	if (!r.Done())
		return Fail(id, "trailing content after the object");

	if (!sawSchema)
		return Fail(id, "no schema");
	if (schema > kAddonSchema)
		return Fail(id, "written for a newer ZandroX");
	if (schema < 1)
		return Fail(id, "bad schema");

	if (entry.name.empty())
		return Fail(id, "no name");

	// [rc4l] What a variant LOADS must be non-empty, which is not the same as the entry having files
	// of its own any more. A pack whose ways of playing share nothing puts everything in its variants
	// and leaves this list empty, so requiring it here would refuse exactly that shape. The real rule
	// -- every way of playing loads something -- is checked with the variants below.
	if (entry.variants.empty() && entry.files.empty())
		return Fail(id, "no files");

	for (size_t i = 0; i < entry.files.size(); ++i)
	{
		if (!IsBareFilename(entry.files[i].name))
			return Fail(id, "a file name is not a bare filename");
		if (!LooksLikeMd5(entry.files[i].md5))
			return Fail(id, "a file has no usable md5");
	}

	if (!entry.iwad.empty() && !IsBareFilename(entry.iwad))
		return Fail(id, "iwad is not a bare filename");

	// A map lump name reaches a command line, so it may not carry a path or read as another flag.
	if (!entry.map.empty() && (!IsBareFilename(entry.map) || (entry.map[0] == '-') || (entry.map[0] == '+')))
		return Fail(id, "map is not a plain lump name");

	// [rc4l] Variants, if the entry claims any. An entry with none is the ordinary case and skips all
	// of this; an entry that says "variants" and then offers nothing usable is refused rather than
	// quietly treated as having none, because the panel would then be silently missing.
	// [rc4l] The label is required, and required of whichever thing is actually the experience: the
	// variants when there are any, the entry itself when there are not. An unlabelled experience is
	// what this exists to stop, and a pack with one way to play is not a reason to know less about
	// it. Refused by name at startup, where the author can see and fix it.
	if (entry.variants.empty() && (entry.kind == VariantKind::Unknown))
		return Fail(id, "this experience does not say whether it is pve or pvp: add \"kind\"");

	if (!entry.variants.empty())
	{
		int defaults = 0;

		for (size_t i = 0; i < entry.variants.size(); ++i)
		{
			const AddonVariant &v = entry.variants[i];

			if (v.id.empty())
				return Fail(id, "a variant has no id");
			if (v.name.empty())
				return Fail(id, "a variant has no name");

			// Same rule as every other filename here: it reaches a loader, so it may not carry a
			// path or climb out of the entry's own folder.
			if (!IsBareFilename(v.cfg))
				return Fail(id, "a variant's cfg is not a bare filename");

			// Named, because a pack can have six of them and "a variant" would leave the author
			// reading all six to find out which one they forgot.
			if (v.kind == VariantKind::Unknown)
			{
				return Fail(id, "the experience variant '" + v.name +
					"' does not say whether it is pve or pvp: add \"kind\"");
			}

			for (size_t j = 0; j < i; ++j)
			{
				if (entry.variants[j].id == v.id)
					return Fail(id, "two variants share an id");
			}

			// The same rules the entry's own files obey. A variant's list reaches the same loader and
			// the same by-hash store, so a path or a bad hash is exactly as dangerous there.
			for (size_t f = 0; f < v.files.size(); ++f)
			{
				if (!IsBareFilename(v.files[f].name))
					return Fail(id, "the experience variant '" + v.name +
						"' names a file that is not a bare filename");
				if (!LooksLikeMd5(v.files[f].md5))
					return Fail(id, "the experience variant '" + v.name +
						"' has a file with no usable md5");
			}

			// [rc4l] THE rule the entry-level check gave up: what this way of playing actually loads,
			// which is the entry's files plus its own. A variant that resolves to nothing would start
			// a server on the bare IWAD, which is not what anybody picked.
			if (entry.files.empty() && v.files.empty())
				return Fail(id, "the experience variant '" + v.name + "' loads no files");

			if (v.isDefault)
				++defaults;
		}

		// Ambiguity is refused rather than resolved by order. Which way a pack plays when nobody has
		// chosen is exactly the sort of thing that must not depend on how the file happens to be
		// sorted, and two claims to it is a mistake the author can see and fix.
		if (defaults > 1)
			return Fail(id, "more than one variant claims to be the default");
	}

	entry.valid = true;
	return entry;
}

} // namespace zx
