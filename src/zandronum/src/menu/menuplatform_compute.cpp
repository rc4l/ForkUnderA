// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "menu/menuplatform_compute.h"

#include <cctype>

namespace zx {

namespace {

struct PlatformName
{
	const char *keyword;
	MenuPlatform platform;
};

const PlatformName kNames[] = {
	{ "windows", MenuPlatform::Windows },
	{ "mac", MenuPlatform::Mac },
	{ "unix", MenuPlatform::Unix },
	{ "web", MenuPlatform::Web },
};

// [rc4l] The platform a keyword names, NULL for none.
const PlatformName *Find(const char *keyword)
{
	if (keyword == nullptr) return nullptr;
	for (const PlatformName &name : kNames)
	{
		const char *a = keyword, *b = name.keyword;
		while (*a != '\0' && std::tolower((unsigned char)*a) == *b) { a++; b++; }
		if (*a == '\0' && *b == '\0') return &name;
	}
	return nullptr;
}

} // namespace

bool ComputeIsMenuPlatformKeyword(const char *keyword)
{
	return Find(keyword) != nullptr;
}

bool ComputeMenuPlatformMatches(const char *keyword, MenuPlatform platform)
{
	const PlatformName *name = Find(keyword);
	return name != nullptr && name->platform == platform;
}

} // namespace zx
