// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Reading the optional tail of an announce, where an exhausted read means "old server"
// rather than whatever -1 would imply.

#ifndef ZX_ANNOUNCEFIELDS_COMPUTE_H
#define ZX_ANNOUNCEFIELDS_COMPUTE_H

namespace zx
{

// [rc4l] An optional trailing flag, where a negative `raw` means the packet ended and must read as
// false so a server is never credited with a capability it did not claim.
bool AnnounceFlagFromByte(int raw);

// [rc4l] Whether the revision is a long rather than a short, since reading the wrong width turns
// every field after it into nonsense.
bool AnnounceUsesLongRevision(int bytesRemaining);

// [rc4l] Whether a grouping id may merge listings, refusing anything but the 64 lower-case hex
// characters this engine writes, because merging on a bad value hides somebody's server.
bool AnnounceIdIsGroupable(const char *id);

} // namespace zx

#endif // ZX_ANNOUNCEFIELDS_COMPUTE_H
