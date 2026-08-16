// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] sv_fua_friendlymonsters -- nothing in the level fights the player.
//
// Written for driving the engine to look at things: warping somewhere to judge lighting, a texture
// or a sky means standing still in a place the level would rather you did not, and every monster in
// earshot then spends the visit shooting at the camera. Muzzle flashes and projectiles land in the
// frame, blood recolours the floor, and eventually the player dies and the view is somewhere else.
//
// `notarget` is the obvious answer and it is not enough on its own: CF_NOTARGET is only consulted
// where a monster ACQUIRES a target (P_LookForPlayers and the noise-alert path in p_enemy.cpp), so
// anything that locked on before it went up keeps its target, keeps chasing and keeps firing. It is
// also a toggle, which makes it easy to disarm by running a tool twice.
//
// MF_FRIENDLY is the flag the engine already uses for monsters that are on the player's side, so it
// is honoured everywhere targeting decisions are made rather than at the one site a cheat patches.
// A cvar is also a STATE, not a toggle: setting it twice is setting it, which a driving tool can
// rely on. Clearing it hands every monster back its hostility.

#ifndef ZX_FRIENDLYMONSTERS_H
#define ZX_FRIENDLYMONSTERS_H

class AActor;

namespace zx
{

// Apply the current cvar value to one actor. Called from AActor::PostBeginPlay so monsters that
// arrive later -- teleport ambushes, spawners, `summon` -- are covered too, not just the ones
// standing there when the cvar was set.
void FriendlyMonsters_Spawned( AActor *mo );

// [rc4l] Called for each actor as it comes back out of a savegame.
//
// MF_FRIENDLY, MF2_DORMANT and STFlags are ALL serialised (p_mobj.cpp AActor::Serialize), so a save
// taken while the cvar was on bakes every monster friendly and asleep. CUSTOM_CVAR only fires when
// the value CHANGES, so loading that save with the cvar already off would restore nothing and leave
// a permanently pacified level with no way back. This reconciles what the save carried against what
// the cvar currently says, so the effect cannot outlive the session that asked for it.
void FriendlyMonsters_Loaded( AActor *mo );

} // namespace zx

#endif
