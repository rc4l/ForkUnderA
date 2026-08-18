// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/announcefields_compute.h"

namespace zx
{

bool AnnounceFlagFromByte(int raw)
{
	// [rc4l] Strictly positive, since "!= 0" would read the -1 of an exhausted stream as true.
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

	// [rc4l] Exactly a SHA-256 in hex, since a shorter one is a truncated read and a longer one is
	// not ours.
	return (length == 64);
}

} // namespace zx
