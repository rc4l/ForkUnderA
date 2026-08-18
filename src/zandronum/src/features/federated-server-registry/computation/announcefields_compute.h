// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Reading the optional tail of a server's announce, where "absent" is a real answer.
//
// The announce grew by appending: a ban-enforcement flag, a revision, then a punch-support byte, then
// a grouping id. Every one of those is read from a stream that may simply END, because the server
// sending it was built before the field existed -- and the whole compatibility story rests on absent
// meaning the OLD behaviour rather than whatever a failed read happens to return.
//
// That is easy to get right once and wrong the next time somebody appends a field, and getting it
// wrong is silent: an exhausted read yields -1, which is not zero and is not false, and a careless
// `!= 0` would turn "this server is too old to punch" into "this server can punch" and leave the
// registry instructing something that will never answer.
//
// So the ladder's rules live here, tested, instead of being retyped per field.
//
// Header-pure by the features/ rules: no streams, no sockets.

#ifndef ZX_ANNOUNCEFIELDS_COMPUTE_H
#define ZX_ANNOUNCEFIELDS_COMPUTE_H

namespace zx
{

// An optional trailing FLAG. `raw` is what the stream gave back, with a negative value meaning the
// packet ended before this field existed.
//
// Absent is false, always, for every flag ever appended here: a server that never claimed a
// capability must never be treated as having it.
bool AnnounceFlagFromByte(int raw);

// Whether the revision is a long rather than a short, given the bytes still unread.
//
// Older servers wrote a short. The width is decided by what is LEFT rather than by a version, which
// is why it belongs in one tested place: read a long from a short and the fields after it are
// nonsense.
bool AnnounceUsesLongRevision(int bytesRemaining);

// Whether an announced grouping id may be used to merge listings.
//
// Refuses anything that is not the exact shape this engine produces -- 64 lower-case hex characters,
// being a SHA-256 -- because the id decides which listings get merged, and merging on a malformed or
// truncated value would hide somebody's server. `id` may be null, meaning the field was absent.
bool AnnounceIdIsGroupable(const char *id);

} // namespace zx

#endif // ZX_ANNOUNCEFIELDS_COMPUTE_H
