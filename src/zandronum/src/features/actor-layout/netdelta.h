// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] What a server last sent of an actor's motion in a MoveThing, kept off the actor since the snapshot carries most actors' motion.
#ifndef ZX_NETDELTA_H
#define ZX_NETDELTA_H

#include "basictypes.h"

// [Doruk-Aksoy] Zeros, as these fields read on an actor no MoveThing has gone out for.
struct ZxNetDelta
{
	// [BB] Last tic in which the server sent a xyz-position / movedir update about this actor to the clients.
	int lastNetXUpdateTic = 0, lastNetYUpdateTic = 0, lastNetZUpdateTic = 0;
	int lastNetVelXUpdateTic = 0, lastNetVelYUpdateTic = 0, lastNetVelZUpdateTic = 0, lastNetMovedirUpdateTic = 0;
	// [BB] Last xyz-velocity that was sent to the client.
	fixed_t lastVelx = 0, lastVely = 0, lastVelz = 0;
	// [BB] Last movedir that was sent to the client.
	BYTE lastMovedir = 0;
};

#endif
