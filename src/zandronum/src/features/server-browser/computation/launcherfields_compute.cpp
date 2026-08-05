// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/launcherfields_compute.h"

namespace zx
{

namespace
{

// A bounds-checked cursor. Every read reports whether it fitted, so a truncated block is refused at
// the point it runs out rather than producing values from memory nobody sent.
struct Cursor
{
	const unsigned char *data;
	size_t length;
	size_t at;

	Cursor(const unsigned char *d, size_t len) : data(d), length(len), at(0) {}

	bool ReadByte(int &out)
	{
		if (at >= length)
			return false;
		out = data[at];
		++at;
		return true;
	}

	// Little-endian, matching BYTESTREAM_s::WriteShort. Read UNSIGNED on purpose: the engine's
	// ReadShort casts to a signed short, so any value above 32767 comes back negative -- which for a
	// port means every server above that number looks invalid. A port is not a signed quantity.
	bool ReadShort(int &out)
	{
		if ((at + 2) > length)
			return false;
		out = data[at] | (data[at + 1] << 8);
		at += 2;
		return true;
	}

	// Null-terminated. A string running to the end of the block without its terminator is truncation:
	// the writer always emits one, so its absence means the block was cut short.
	bool ReadString(std::string &out)
	{
		out.clear();
		while (at < length)
		{
			const unsigned char c = data[at];
			++at;
			if (c == 0)
				return true;
			out.push_back(static_cast<char>(c));
		}
		return false;
	}

	bool ReadRaw(size_t count, std::string &out)
	{
		if ((at + count) > length)
			return false;
		out.assign(reinterpret_cast<const char *>(data + at), count);
		at += count;
		return true;
	}
};

} // namespace

unsigned KnownExtendedFields()
{
	return kSqf2PwadHashes | kSqf2Country | kSqf2GameModeName | kSqf2GameModeShortName |
		kSqf2VoiceChat | kSqf2DirectDownload | kSqf2IwadHash;
}

ExtendedParse ParseExtendedInfo(const unsigned char *data, size_t length, unsigned flags2,
	LauncherExtendedInfo &out, size_t &outConsumed)
{
	outConsumed = 0;

	// The whole point. Fields are variable length, so a bit we do not recognise cannot be stepped
	// over -- we would be guessing at a width and every later field would be fiction. Refuse, and let
	// the caller keep what it already had.
	if ((flags2 & ~KnownExtendedFields()) != 0)
		return ExtendedParse::UnknownField;

	LauncherExtendedInfo parsed;
	Cursor cursor(data, length);

	// Ascending bit order, which is the order the server writes them: its dispatch table is a map
	// keyed by bit value, so it emits bit 0 first and bit 6 last.

	if (flags2 & kSqf2PwadHashes)
	{
		int count = 0;
		if (!cursor.ReadByte(count))
			return ExtendedParse::Truncated;

		for (int i = 0; i < count; ++i)
		{
			std::string hash;
			if (!cursor.ReadString(hash))
				return ExtendedParse::Truncated;
			parsed.pwadHashes.push_back(hash);
		}
	}

	if (flags2 & kSqf2Country)
	{
		// Three raw bytes, NOT null-terminated -- the one field here that is not self-delimiting.
		if (!cursor.ReadRaw(3, parsed.countryCode))
			return ExtendedParse::Truncated;
	}

	if (flags2 & kSqf2GameModeName)
	{
		if (!cursor.ReadString(parsed.gameModeName))
			return ExtendedParse::Truncated;
	}

	if (flags2 & kSqf2GameModeShortName)
	{
		if (!cursor.ReadString(parsed.gameModeShortName))
			return ExtendedParse::Truncated;
	}

	if (flags2 & kSqf2VoiceChat)
	{
		// The value is not used by the browser. The BYTE STILL HAS TO BE READ: skipping it is the
		// bug this whole unit exists to make impossible.
		if (!cursor.ReadByte(parsed.voiceChat))
			return ExtendedParse::Truncated;
	}

	if (flags2 & kSqf2DirectDownload)
	{
		int flags = 0;
		int port = 0;
		if (!cursor.ReadByte(flags) || !cursor.ReadShort(port))
			return ExtendedParse::Truncated;

		parsed.prefersMirrors = ((flags & 1) != 0);
		parsed.directDownloadPort = port;
	}

	if (flags2 & kSqf2IwadHash)
	{
		if (!cursor.ReadString(parsed.iwadHash))
			return ExtendedParse::Truncated;
	}

	outConsumed = cursor.at;
	out = parsed;
	return ExtendedParse::Ok;
}

} // namespace zx
