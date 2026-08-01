// [rc4l] Who is allowed to change the FOV, and to what — the pure decision behind the `fov`
// CVAR and its options-menu slider.
//
// The slider needs a CVAR to bind to, but a CVAR is written directly by the menu, by the config
// file and by the console, with no chance to say "no" the way the old `fov` CCMD could. So the
// permission rules move here, where they can be tested, and the CVAR handler does exactly what
// this unit tells it. Two rules apply:
//
//   * `sv_nofov` / DF_NO_FOV — a setting controller has locked FOV. Only the arbitrator may
//     change it, and their change applies to EVERY player (this is Zandronum's existing
//     behaviour via DEM_FOV; we keep it, unlike Q-Zandronum, which deleted the lock outright
//     in 65e0aad7f).
//   * `fov_change_cooldown_tics` — a server-set minimum gap between a client's FOV changes,
//     so FOV cannot be flicked every tic as a peeking exploit. Ported from Q-Zandronum, which
//     added it as the softer replacement for the lock; here it stacks WITH the lock instead.
//
// Header-pure (no engine/CVAR includes) so the rules are tested rather than inferred from
// reading the CVAR handler.
//
// Provenance: Q-Zandronum 65e0aad7f (the menu slider + fov-as-CVAR) and its
// fov_change_cooldown_tics. https://github.com/IgeNiaI/Q-Zandronum/commit/65e0aad7f26a8246d80be00cdf1ddcb8bd73ecef
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FOVREQUEST_COMPUTE_H
#define ZX_FOVREQUEST_COMPUTE_H

namespace zx
{

// What the engine should do with an attempted FOV change.
enum FovRequestVerdict
{
	FOV_SET_MINE,          // ordinary case: change this player's FOV (DEM_MYFOV)
	FOV_SET_EVERYONE,      // FOV is locked and we are the arbitrator: set it for all (DEM_FOV)
	FOV_DENIED_LOCKED,     // FOV is locked and we are not the arbitrator
	FOV_DENIED_COOLDOWN,   // too soon after the last change
};

// The renderer/CCMD clamp: 5..179 degrees, matching the range the old `fov` CCMD enforced and
// what DEM_FOV can carry in a byte.
int FovRequestClamp(int fov);

// True when a change now would land inside the server's minimum gap. A cooldown of 0 disables
// the rule entirely. `lastChangeTic` is the tic of this player's previous accepted change.
bool FovCooldownActive(int gametic, int lastChangeTic, int cooldownTics);

// The verdict for a change attempted right now.
//   fovLocked     - dmflags & DF_NO_FOV
//   isArbitrator  - consoleplayer == Net_Arbitrator
//   isClient      - NETWORK_GetState() == NETSTATE_CLIENT (only clients are rate-limited; a
//                   local game has no server to enforce it and no exploit to prevent)
FovRequestVerdict FovRequestDecision(bool fovLocked, bool isArbitrator, bool isClient,
                                     int gametic, int lastChangeTic, int cooldownTics);

// The FOV a player should spawn with. Respawning used to hard-reset everyone to 90, throwing away
// a preference the player had deliberately set. Their own choice is restored instead — but only
// for the local player: `fov` is a client CVAR, so on a server (or for any OTHER player in the
// game) it is not that player's preference and must not be stamped onto them. Each client
// re-asserts its own FOV over the wire anyway.
//
// This restores the player's BASE fov only. Weapon zoom lives in ReadyWeapon->FOVScale and is
// re-applied per tic, never stored here, so dying with a sniper scope raised and respawning with
// a pistol correctly comes back unzoomed.
//
//   isLocalPlayer - the player being spawned is this machine's own (consoleplayer)
//   isServer      - NETWORK_GetState() == NETSTATE_SERVER (never has a meaningful local FOV)
//   playerFovCvar - the `fov` CVAR
float FovOnSpawn(bool isLocalPlayer, bool isServer, float playerFovCvar);

} // namespace zx

#endif // ZX_FOVREQUEST_COMPUTE_H
