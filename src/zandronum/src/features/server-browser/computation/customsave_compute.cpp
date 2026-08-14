// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See customsave_compute.h for why a preset is a catalogue entry and for the name question.

#include "features/server-browser/computation/customsave_compute.h"

namespace zx
{

namespace
{

// [rc4l] A JSON string, escaped. Only the five things JSON actually requires plus control
// characters: a path with a backslash in it is the common case here, and getting that wrong writes
// a file nothing can read back.
std::string JsonString(const std::string &value)
{
	std::string out = "\"";

	for (size_t i = 0; i < value.size(); ++i)
	{
		const char c = value[i];

		switch (c)
		{
		case '"':	out += "\\\""; break;
		case '\\':	out += "\\\\"; break;
		case '\n':	out += "\\n"; break;
		case '\r':	out += "\\r"; break;
		case '\t':	out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 32)
			{
				// Anything else unprintable is dropped rather than escaped: it has no business in
				// a filename or a preset name, and \u escapes would be a parser this does not need.
				break;
			}

			out += c;
			break;
		}
	}

	out += "\"";
	return out;
}

bool IsCommentOrBlank(const std::string &line)
{
	for (size_t i = 0; i < line.size(); ++i)
	{
		if ((line[i] == ' ') || (line[i] == '\t'))
			continue;

		return ((line[i] == '/') && ((i + 1) < line.size()) && (line[i + 1] == '/'));
	}

	return true;
}

std::string Trim(const std::string &s)
{
	size_t first = 0;
	while ((first < s.size()) && ((s[first] == ' ') || (s[first] == '\t') || (s[first] == '\r')))
		first++;

	size_t last = s.size();
	while ((last > first) &&
		((s[last - 1] == ' ') || (s[last - 1] == '\t') || (s[last - 1] == '\r')))
	{
		last--;
	}

	return s.substr(first, last - first);
}

} // namespace

std::string CustomAddonJson(const CustomEntry &entry)
{
	// [rc4l] Written by hand rather than through a writer, because this is nine fields and a list.
	// The escaping is the part that has to be right, and it is one function above.
	std::string out = "{\n";

	out += "  \"name\": " + JsonString(entry.name) + ",\n";

	// A preset the player built has no summary anybody wrote, and an invented one would be a lie
	// in a list. The line says where it came from instead.
	out += "  \"summary\": \"Saved from the NEW screen.\",\n";
	out += "  \"iwad\": " + JsonString(entry.iwad) + ",\n";

	// [rc4l] kind is REQUIRED of a catalogue entry, and only pve and pvp are kinds. Derived from the
	// gamemode rather than asked about, because the mode already answers it.
	out += entry.bPvP ? "  \"kind\": \"pvp\",\n" : "  \"kind\": \"pve\",\n";

	if (!entry.gameMode.empty())
		out += "  \"gamemode\": " + JsonString(entry.gameMode) + ",\n";

	if (!entry.maps.empty())
		out += "  \"map\": " + JsonString(entry.maps[0]) + ",\n";

	out += "  \"files\": [";

	for (size_t i = 0; i < entry.files.size(); ++i)
	{
		out += (i == 0) ? "\n" : ",\n";
		out += "    {\n";
		out += "      \"name\": " + JsonString(entry.files[i].name);

		// [rc4l] The md5 is what lets a missing file be FETCHED rather than merely missed: it is
		// how the download path names a file. Left out when it could not be worked out, which is
		// honest -- a wrong hash would refuse a file that was actually correct.
		if (!entry.files[i].md5.empty())
			out += ",\n      \"md5\": " + JsonString(entry.files[i].md5);

		out += "\n    }";
	}

	out += entry.files.empty() ? "]\n" : "\n  ]\n";
	out += "}\n";

	return out;
}

std::string CustomServerCfg(const CustomEntry &entry)
{
	std::string out = "// Saved from the NEW screen. Edit it there, or by hand.\n\n";

	if (!entry.gameMode.empty())
		out += entry.gameMode + " 1\n\n";

	for (size_t i = 0; i < entry.cvars.size(); ++i)
	{
		const std::string &name = entry.cvars[i].first;
		const std::string &value = entry.cvars[i].second;

		// A name with a space in it is not a cvar, and a value with a newline would write a line
		// this did not mean. Dropped rather than escaped, the same rule the command line follows.
		if (name.empty() || (name.find(' ') != std::string::npos))
			continue;
		if ((value.find('\n') != std::string::npos) || (value.find('\r') != std::string::npos))
			continue;

		out += name + " " + value + "\n";
	}

	if (!entry.maps.empty())
	{
		out += "\n// The rotation, in order.\n";

		for (size_t i = 0; i < entry.maps.size(); ++i)
			out += "addmap " + entry.maps[i] + "\n";
	}

	return out;
}

void ParseCustomCfg(const std::string &text,
	std::vector<std::pair<std::string, std::string> > &cvars, std::vector<std::string> &maps)
{
	cvars.clear();
	maps.clear();

	size_t at = 0;

	while (at <= text.size())
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();

		const std::string line = Trim(text.substr(at, end - at));
		at = end + 1;

		if (IsCommentOrBlank(line))
			continue;

		const size_t space = line.find(' ');
		if (space == std::string::npos)
			continue;

		const std::string name = line.substr(0, space);
		const std::string value = Trim(line.substr(space + 1));

		// A rotation is not a setting, so it comes back separately.
		if (name == "addmap")
		{
			if (!value.empty())
				maps.push_back(value);

			continue;
		}

		cvars.push_back(std::make_pair(name, value));
	}
}

bool IsCustomName(const std::string &name)
{
	if (name.empty() || (name.size() > 48))
		return false;

	// Leading and trailing spaces make two names that look identical, so they are not names.
	if ((name[0] == ' ') || (name[name.size() - 1] == ' '))
		return false;

	for (size_t i = 0; i < name.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(name[i]);

		if (c < 32)
			return false;

		if ((c == '/') || (c == '\\') || (c == ':') || (c == '*') || (c == '?') ||
			(c == '"') || (c == '<') || (c == '>') || (c == '|'))
		{
			return false;
		}
	}

	// "." and ".." are directories, whatever a filesystem thinks of the characters.
	if ((name == ".") || (name == ".."))
		return false;

	return true;
}

SaveState NextSaveState(const std::string &name, const std::vector<std::string> &taken,
	bool bAlreadyAsked, size_t fileCount)
{
	// Answered before the name, because no name makes a configuration with nothing in it saveable.
	if (fileCount == 0)
		return SaveState::NoFiles;

	if (name.empty())
		return SaveState::Empty;

	if (!IsCustomName(name))
		return SaveState::Bad;

	bool bTaken = false;
	for (size_t i = 0; i < taken.size(); ++i)
	{
		if (taken[i] == name)
		{
			bTaken = true;
			break;
		}
	}

	if (!bTaken)
		return SaveState::Ready;

	// [rc4l] Taken: the first Confirm asks, the second replaces. Losing a configuration somebody
	// spent time on to a mistyped name is the one failure here that cannot be undone.
	return bAlreadyAsked ? SaveState::Replace : SaveState::Asking;
}

const char *SaveStatusText(SaveState state)
{
	switch (state)
	{
	case SaveState::Empty:		return "Give it a name";
	case SaveState::NoFiles:	return "Add at least one file before saving this";
	case SaveState::Bad:		return "That name cannot be used for a folder";
	// [rc4l] Written for a reader rather than for a width: the box wraps these to fit. Shortening a
	// line to make it fit was the wrong fix, and the next line added would have needed it again.
	case SaveState::Asking:		return "That name is already in use. Confirm again to replace it";
	case SaveState::Replace:	return "Confirm now replaces what is saved under that name";
	case SaveState::Ready:		return "";
	case SaveState::Fresh:		return "";
	}

	return "";
}

bool SaveStatusIsWarning(SaveState state)
{
	return (state == SaveState::Bad) || (state == SaveState::Asking) ||
		(state == SaveState::Replace) || (state == SaveState::NoFiles);
}

} // namespace zx
