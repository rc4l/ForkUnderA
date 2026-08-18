// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/announcefields_compute.h"

namespace zx
{

bool AnnounceFlagFromByte(int raw)
{
	// Strictly positive. Not "!= 0", which would read the -1 of an exhausted stream as true and grant
	// a capability to every server too old to have claimed it.
	return (raw > 0);
}

bool AnnounceUsesLongRevision(int bytesRemaining)
{
	return (bytesRemaining >= 4);
}

bool AnnounceIdIsGroupable(const char *id)
{
	if (id == 0)
		return false;

	int length = 0;
	for (const char *p = id; *p != 0; ++p)
	{
		const char c = *p;
		const bool hex = ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f'));
		if (!hex)
			return false;

		++length;
		if (length > 64)
			return false;
	}

	// Exactly a SHA-256 in hex. A shorter one is a truncated read and a longer one is not ours, and
	// either would be a way to have listings merged that are not one server.
	return (length == 64);
}

} // namespace zx
