// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/userdir/computation/userdir_compute.h"

#include <algorithm>

namespace zx { namespace userdir {

std::string ComputeFuaUserPath(const char *givenPath, const char *cachePath, const char *gameName)
{
	if (givenPath != nullptr && *givenPath != 0)
	{
		std::string given(givenPath);
		std::replace(given.begin(), given.end(), '\\', '/');
		while (given.size() > 1 && given.back() == '/')
			given.pop_back();
		return given;
	}

	std::string path(cachePath != nullptr ? cachePath : "");
	std::replace(path.begin(), path.end(), '\\', '/');
	for (int i = 0; i < 2; ++i)
	{
		const std::string::size_type slash = path.rfind('/');
		if (slash != std::string::npos && slash > 0)
			path.resize(slash);
	}
	return path + "/" + (gameName != nullptr ? gameName : "");
}

}} // namespace zx::userdir
