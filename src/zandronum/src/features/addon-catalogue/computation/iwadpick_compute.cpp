// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/iwadpick_compute.h"

#include "features/wad-download/computation/iwadsubstitute_compute.h"

#include <cctype>

namespace zx
{

namespace
{

bool SameFile(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;

	for (size_t i = 0; i < a.size(); ++i)
	{
		const unsigned char ca = static_cast<unsigned char>(a[i]);
		const unsigned char cb = static_cast<unsigned char>(b[i]);
		if (std::tolower(ca) != std::tolower(cb))
			return false;
	}
	return true;
}

bool Has(const std::vector<std::string> &available, const std::string &name)
{
	if (name.empty())
		return false;

	for (size_t i = 0; i < available.size(); ++i)
	{
		if (SameFile(available[i], name))
			return true;
	}
	return false;
}

} // namespace

IwadPick PickIwad(const std::string &preferred, const std::vector<std::string> &available)
{
	IwadPick pick;
	pick.wanted = preferred;

	// Owning it settles it. The substitute table is a fallback and asking it first would hand a
	// player Freedoom while their Doom II sat on the disk.
	if (Has(available, preferred))
	{
		pick.choice = IwadChoice::Preferred;
		pick.iwad = preferred;
		return pick;
	}

	const std::string substitute = FreeIwadSubstituteFor(preferred);
	if (!Has(available, substitute))
		return pick;	// None: nothing to host on

	pick.iwad = substitute;
	pick.choice = IwadChoice::Substitute;
	return pick;
}

} // namespace zx
