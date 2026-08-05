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
	case DownloadVerdict::UnvouchedIwadBuild:
										return "the file that arrived is not a build of that IWAD we "
											   "have vouched for";
	}
	return "refused";
}

bool IsVouchedIwadBuild(const std::string &sha256Hex, const std::string &name)
{
	const std::string digest = ToLower(sha256Hex);
	const std::string lowered = ToLower(name);

	// An empty digest means we could not hash the file. That is "cannot vouch", never "fine" -- a
	// failed read must not be a way through the only check that stops a renamed commercial IWAD.
	if (digest.empty())
		return false;

	for (int i = 0; i < kFreeIwadHashCount; ++i)
	{
		if (digest == kFreeIwadHashes[i][0] && lowered == kFreeIwadHashes[i][1])
			return true;
	}
	return false;
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

DownloadVerdict ClassifyDownloadedFile(const std::string &name, const char *header, size_t len,
	const std::string &sha256Hex)
{
	// The name is re-checked rather than trusted from the earlier pass: this function is the last gate
	// before a file is kept, and it should be safe to call on a file that arrived by any route.
	if (!IsSafeDownloadName(name))
		return DownloadVerdict::UnsafeName;

	// Not an IWAD -> it is a mod, and mods cannot be enumerated. Nothing more to ask.
	if (!HeaderIsIwadMagic(header, len))
		return DownloadVerdict::Allowed;

	// It is a game. Two questions, in this order, because they fail for different reasons and the
	// player deserves the right one: is this filename even supposed to be a free IWAD, and are these
	// actually the bytes of one?
	if (!IsFreeIwadName(name))
		return DownloadVerdict::UnlistedIwad;

	// The gate. doom2.wad served under the name freedoom2.wad has passed every check up to this line.
	if (!IsVouchedIwadBuild(sha256Hex, name))
		return DownloadVerdict::UnvouchedIwadBuild;

	return DownloadVerdict::Allowed;
}

} // namespace zx
