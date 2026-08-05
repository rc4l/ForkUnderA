// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/iwadallow_compute.h"

#include "features/wad-download/computation/downloadplan_compute.h"

// [rc4l] kFreeIwads, generated at build time from the repo-root iwadallowlist.txt by
// tools/gen-wadlists.cmake. That file is the single source of truth and the only way to add an
// entry; see its header comment, and features/wad-download/README.md.
#include "zx_waddownload_lists.h"

namespace
{

char LowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

std::string ToLower(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i)
		out.push_back(LowerAscii(s[i]));
	return out;
}

} // namespace

namespace zx
{

const char *DownloadVerdictReason(DownloadVerdict verdict)
{
	switch (verdict)
	{
	case DownloadVerdict::Allowed:		return "allowed";
	case DownloadVerdict::UnsafeName:	return "not a file name we will download";
	case DownloadVerdict::UnlistedIwad:	return "that is a game IWAD, and only IWADs known to be free "
											   "to redistribute can be downloaded";
	}
	return "refused";
}

bool IsFreeIwadName(const std::string &name)
{
	const std::string lowered = ToLower(name);
	for (size_t i = 0; i < sizeof zx::kFreeIwads / sizeof zx::kFreeIwads[0]; ++i)
	{
		if (lowered == zx::kFreeIwads[i])
			return true;
	}
	return false;
}

bool HeaderIsIwadMagic(const char *header, size_t len)
{
	if (header == NULL || len < 4)
		return false;
	return header[0] == 'I' && header[1] == 'W' && header[2] == 'A' && header[3] == 'D';
}

DownloadVerdict ClassifyWantedFile(const std::string &name, bool isIwadSlot)
{
	if (!IsSafeDownloadName(name))
		return DownloadVerdict::UnsafeName;

	if (isIwadSlot && !IsFreeIwadName(name))
		return DownloadVerdict::UnlistedIwad;

	// A PWAD passes on name alone. Nothing here tries to guess whether "coolmod.wad" is really a game
	// in disguise -- that question is answerable from the bytes, so it is asked once they arrive.
	return DownloadVerdict::Allowed;
}

DownloadVerdict ClassifyDownloadedFile(const std::string &name, const char *header, size_t len)
{
	// The name is re-checked rather than trusted from the earlier pass: this function is the last gate
	// before a file is kept, and it should be safe to call on a file that arrived by any route.
	if (!IsSafeDownloadName(name))
		return DownloadVerdict::UnsafeName;

	// The check no name-based rule can do, and the reason none is needed. Anything whose own header
	// says "I am a game" has to be a game we can confirm is free -- so doom2.wad reaches this point
	// however it was spelled or whichever slot requested it, and is refused for what it is.
	if (HeaderIsIwadMagic(header, len) && !IsFreeIwadName(name))
		return DownloadVerdict::UnlistedIwad;

	return DownloadVerdict::Allowed;
}

} // namespace zx
