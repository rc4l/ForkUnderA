// [rc4l] See fua_version_compute.h.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fua_version_compute.h"

namespace zx
{

namespace
{
// [rc4l] git describe appends "-<commits>-g<hash>" to the tag. Find the LAST such marker: a tag is
// allowed to contain dashes itself (v1.0-rc1), so scanning from the right is what keeps that case
// working -- searching from the left would cut "v1.0-rc1-3-gabc" down to "v1.0".
const char *FindDescribeSuffix(const char *s)
{
	const char *found = nullptr;
	for (const char *p = s; *p; ++p)
	{
		if (p[0] != '-' || p[1] != 'g' || p[2] == '\0')
			continue;
		// Require the "-g" to be preceded by "-<digits>", which is the commits-since count.
		const char *q = p - 1;
		if (q < s || *q < '0' || *q > '9')
			continue;
		while (q > s && *(q - 1) >= '0' && *(q - 1) <= '9')
			--q;
		if (q > s && *(q - 1) == '-')
			found = q - 1;
	}
	return found;
}
}

bool FuaIsStableBuild(const char *describe)
{
	if (describe == nullptr || describe[0] == '\0')
		return false;

	// [rc4l] "No describe suffix" alone is NOT enough. A clone with no tags reachable (a shallow CI
	// checkout, say) describes as a bare hash like "de55d35" -- which also has no suffix, and would
	// therefore have been reported as a stable release. Require the string to actually look like a
	// version tag as well: 'v' followed by a digit.
	if (describe[0] != 'v' || describe[1] < '0' || describe[1] > '9')
		return false;

	return FindDescribeSuffix(describe) == nullptr;
}

void FuaVersionTag(const char *describe, char *out, size_t outSize)
{
	if (out == nullptr || outSize == 0)
		return;
	if (describe == nullptr)
	{
		out[0] = '\0';
		return;
	}

	const char *suffix = FindDescribeSuffix(describe);
	size_t len = 0;
	while (describe[len] != '\0' && (suffix == nullptr || describe + len < suffix))
		++len;
	if (len > outSize - 1)
		len = outSize - 1;

	for (size_t i = 0; i < len; ++i)
		out[i] = describe[i];
	out[len] = '\0';
}

} // namespace zx
