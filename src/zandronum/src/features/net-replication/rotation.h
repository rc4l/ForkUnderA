// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-replication's map rotation header: an entry's used status that a server's game code changed, sent to its clients from here.

#ifndef ZX_ROTATION_H
#define ZX_ROTATION_H

namespace zx { namespace netreplication {

// [rc4l] The entry at position changed its used status: on a server, sent to every client.
void SendMapUsedStatus(unsigned position);

}} // namespace zx::netreplication

#endif // ZX_ROTATION_H
