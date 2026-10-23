// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-session/unlaggedreport.h"
#include "features/net-session/computation/unlaggedreport_compute.h"

#include "actor.h"
#include "c_dispatch.h"
#include "d_player.h"
#include "doomstat.h"
#include "network.h"
#include "unlagged.h"
#include "features/net-session/netrole.h"

namespace zx { namespace netsession {

namespace {

ShotLog g_log;

// [rc4l] Every other player in the game where the shot found them, and where they were at its tic once restored.
UnlaggedShot ShotBy(AActor *shooter, bool rewound)
{
	UnlaggedShot s;
	s.shooter = (int)(shooter->player - players);
	s.tic = gametic;
	s.rewindTic = rewound ? UNLAGGED_Gametic(shooter->player) : gametic;
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i] || i == s.shooter || players[i].mo == NULL || players[i].bSpectating) continue;
		const AActor *mo = players[i].mo;
		ShotPlayer p;
		p.player = i;
		p.x = FIXED2DBL(mo->x);
		p.y = FIXED2DBL(mo->y);
		p.z = FIXED2DBL(mo->z);
		p.presentX = rewound ? FIXED2DBL(players[i].restorePos[0]) : p.x;
		p.presentY = rewound ? FIXED2DBL(players[i].restorePos[1]) : p.y;
		p.presentZ = rewound ? FIXED2DBL(players[i].restorePos[2]) : p.z;
		s.players.push_back(p);
	}
	return s;
}

}

void NoteServerShot(AActor *shooter, AActor *hit)
{
	if (!zx::net::IsServer() || shooter == NULL || shooter->player == NULL) return;
	UnlaggedShot s = ShotBy(shooter, UNLAGGED_IsReconciled());
	s.hitPlayer = (hit != NULL && hit->player != NULL) ? (int)(hit->player - players) : -1;
	ComputeLogShot(g_log, s);
}

void NoteClientShot(AActor *shooter)
{
	if (!zx::net::IsClient() || shooter == NULL || shooter->player == NULL || shooter->player != &players[consoleplayer]) return;
	ComputeLogShot(g_log, ShotBy(shooter, false));
}

}} // namespace zx::netsession

// [rc4l] The shots kept as JSON, the server's rewound and the client's own; `reset` empties them and starts the numbering over.
CCMD(fua_unlagged_report)
{
	if (argv.argc() > 1 && stricmp(argv[1], "reset") == 0)
	{
		zx::netsession::g_log = zx::netsession::ShotLog();
		Printf("[unlagged] report reset\n");
		return;
	}
	const char *role = zx::net::IsServer() ? "server" : zx::net::IsClient() ? "client" : "single";
	Printf("%s\n", zx::netsession::ComputeShotLogJson(zx::netsession::g_log, role).c_str());
}
