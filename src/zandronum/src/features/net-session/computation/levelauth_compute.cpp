// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-session/computation/levelauth_compute.h"

#include <cctype>

namespace zx { namespace netsession {

namespace {

// [Doruk-Aksoy] Map names compared as the server compares them, without case, a NULL b reading as empty.
bool SameMapName(const char *a, const char *b)
{
	if (b == nullptr) b = "";
	for (; *a != '\0' && *b != '\0'; ++a, ++b)
	{
		if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
			return false;
	}
	return *a == *b;
}

} // namespace

LevelAuthVerdict ComputeLevelAuthVerdict(bool identityHolds, const char *askedMap, const char *currentMap, bool checksumMatches)
{
	if (!identityHolds)
		return LevelAuthVerdict::RejectIdentity;

	// [Doruk-Aksoy] Its checksum is of the map it was asked for, so a map changed since then says nothing about its files.
	if (askedMap != nullptr && askedMap[0] != '\0' && !SameMapName(askedMap, currentMap))
		return LevelAuthVerdict::AskAgain;

	return checksumMatches ? LevelAuthVerdict::Accept : LevelAuthVerdict::RejectLevel;
}

}} // namespace zx::netsession
