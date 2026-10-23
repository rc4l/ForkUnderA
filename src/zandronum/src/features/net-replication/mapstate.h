// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] net-replication's public header: the map state only a server writes, sent to its clients from one place when it changes.

#ifndef ZX_MAPSTATE_H
#define ZX_MAPSTATE_H

#include "features/net-replication/computation/mapstate_compute.h"

namespace zx { namespace netreplication {

// [rc4l] A level's start on a server: every sector, line and side as clients load them, which a joiner is sent the difference from.
void BeginLevel();

// [rc4l] A sector, line or side whose server-written look changed, sent once at the tic's end with whatever else changed in it.
void TouchSector(int index);
void TouchLine(int index);
void TouchSide(int index);

// [rc4l] Families of a row game code sent in its own compact form (one command for a whole tag or line id), which the change set then holds as sent.
void NoteSentSector(int index, unsigned families);
void NoteSentLine(int index, unsigned families);

// [rc4l] The tic's end on a server: the touched rows' changes to every client, then a slice of every row compared, so a change nobody touched still goes out within a second and is counted as missed.
void SendChanges();

// [rc4l] A joiner: every family that differs from the level's start, to it alone.
void SendToJoiner(unsigned client);

}} // namespace zx::netreplication

#endif // ZX_MAPSTATE_H
