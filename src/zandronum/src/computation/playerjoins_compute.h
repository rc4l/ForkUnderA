// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The rules behind GAMEEVENT_PLAYERJOINS, kept apart from the game modes and teams it reads.

#ifndef ZX_PLAYERJOINS_COMPUTE_H
#define ZX_PLAYERJOINS_COMPUTE_H

namespace zx { namespace playerjoins {

// [rc4l] Ported from zandronum@96f065639407d151b0b82031f636c49cbbe03e3c: the team the event names, noTeam for a random team or a mode without teams.
unsigned ComputePlayerJoinsEventTeam(bool teamValid, unsigned team, unsigned noTeam);

}} // namespace zx::playerjoins

#endif
