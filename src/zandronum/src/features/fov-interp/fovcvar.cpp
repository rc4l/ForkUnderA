// [rc4l] The `fov` CVAR — what the options-menu slider binds to — plus the server's
// fov_change_cooldown_tics rate limit.
//
// FOV used to be a CCMD, which a menu slider cannot bind to. Q-Zandronum solved that by turning
// it into a CVAR (65e0aad7f); we do the same, but keep the permission path they deleted:
//
//   * The write still goes out as DEM_FOV / DEM_MYFOV, so demos, prediction and the server all
//     see FOV changes exactly as before. Nothing new goes on the wire.
//   * sv_nofov / DF_NO_FOV still governs who may change it. Q-Zandronum removed that lock
//     outright; here the lock stands and their cooldown stacks with it.
//
// A refused write is rolled back with ForceSet, so the slider and the config file cannot end up
// showing an FOV the player was never granted.
//
// Provenance: https://github.com/IgeNiaI/Q-Zandronum/commit/65e0aad7f26a8246d80be00cdf1ddcb8bd73ecef
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "computation/fovrequest_compute.h"

#include "doomstat.h"
#include "d_player.h"
#include "d_net.h"
#include "d_protocol.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "cmdlib.h"
#include "network.h"
#include "sv_main.h"
#include "features/net-session/netrole.h"
#include "fovcvar.h"

EXTERN_CVAR (Int, dmflags)

// [rc4l] Server-set minimum gap, in tics, between one client's FOV changes. 0 (the default)
// disables the rule, so behaviour is unchanged unless a server opts in. CVAR_SERVERINFO carries
// it to clients through the existing serverinfo machinery — no new network command.
CUSTOM_CVAR (Int, fov_change_cooldown_tics, 0, CVAR_SERVERINFO|CVAR_NOSAVE|CVAR_GAMEPLAYSETTING)
{
	if (self < 0)
		self = 0;
	else if (self > 255)
		self = 255;

	// [rc4l] Let the clients know about the change, the same way every other gameplay
	// serverinfo CVAR does (see sv_aircontrol).
	SERVER_SettingChanged( self, false );
}

// [rc4l] The player's requested FOV. Replaces the old `fov` CCMD: a slider needs something to
// bind to. Written by the menu, the console and the config, so every permission decision has to
// happen in here rather than in a command handler.
CUSTOM_CVAR (Float, fov, 90.f, CVAR_ARCHIVE)
{
	// Guard against the re-entry our own rollback would otherwise cause.
	static bool restoring = false;
	if (restoring)
		return;

	player_t *player = &players[consoleplayer];
	const int requested = zx::FovRequestClamp (static_cast<int> (self));

	const zx::FovRequestVerdict verdict = zx::FovRequestDecision (
		(dmflags & DF_NO_FOV) != 0,
		consoleplayer == Net_Arbitrator,
		zx::net::IsClient(),
		gametic,
		player->lastFOVChangeTic,
		fov_change_cooldown_tics);

	if (verdict == zx::FOV_DENIED_LOCKED || verdict == zx::FOV_DENIED_COOLDOWN)
	{
		if (verdict == zx::FOV_DENIED_LOCKED)
		{
			Printf ("A setting controller has disabled FOV changes.\n");
		}
		else
		{
			const int tics = fov_change_cooldown_tics;
			Printf ("You can only change FOV once every %d tics.\n", tics);
		}

		// Put the CVAR back where it was, so the slider does not display a value the player
		// never actually got.
		restoring = true;
		UCVarValue previous;
		previous.Float = player->DesiredFOV;
		self.ForceSet (previous, CVAR_Float);
		restoring = false;
		return;
	}

	// Snap the CVAR to the clamped value so the config and the slider agree with the engine.
	if (static_cast<float> (requested) != static_cast<float> (self))
	{
		restoring = true;
		UCVarValue clamped;
		clamped.Float = static_cast<float> (requested);
		self.ForceSet (clamped, CVAR_Float);
		restoring = false;
	}

	// [rc4l] Same wire path the CCMD used. DEM_FOV sets it for everyone (arbitrator under a
	// lock); DEM_MYFOV sets only ours.
	// [rc4l] The server's limits apply to what is sent, while the CVAR keeps the player's own choice for the next server.
	const float limited = zx::ComputeFovWithinServerLimits (static_cast<float> (requested), sv_minfov, sv_maxfov);
	if (verdict == zx::FOV_SET_EVERYONE)
	{
		Net_WriteByte (DEM_FOV);
	}
	else
	{
		// Just do this here in client games, matching the old CCMD.
		if ( zx::net::IsClient() )
			player->DesiredFOV = limited;

		Net_WriteByte (DEM_MYFOV);
	}
	Net_WriteByte (static_cast<int> (limited));

	player->lastFOVChangeTic = gametic;
}

// [rc4l] Ported from zandronum@7b3e7adb14602e455ab83cdd7d8645fef960b6fa: sv_minfov and sv_maxfov, checked against each other and applied to every player when either changes.
static void ChangeFovLimit (FFloatCVar &self, float otherLimit, bool isMin)
{
	const zx::FovLimitFix fix = zx::ComputeFovLimit (self, otherLimit, isMin, zx::net::IsClient());
	if (fix.verdict != zx::FOV_LIMIT_KEPT)
	{
		if (fix.verdict == zx::FOV_LIMIT_CROSSED)
			Printf (isMin ? "sv_minfov must be less than sv_maxfov; adjusting value by 1.\n"
			              : "sv_maxfov must be greater than sv_minfov; adjusting value by 1.\n");
		self = fix.value;
		return;
	}

	SERVER_SettingChanged (self, false);

	// [rc4l] 3.3 clamps each player's current FOV, which bakes a raised scope into their own; we clamp the FOV they chose.
	for (unsigned int i = 0; i < MAXPLAYERS; i++)
	{
		if (PLAYER_IsValidPlayer (i))
			players[i].DesiredFOV = zx::ComputeFovWithinServerLimits (players[i].DesiredFOV, sv_minfov, sv_maxfov);
	}
}

CUSTOM_CVAR (Float, sv_minfov, 5.f, CVAR_ARCHIVE|CVAR_SERVERINFO|CVAR_GAMEPLAYSETTING)
{
	ChangeFovLimit (self, sv_maxfov, true);
}

CUSTOM_CVAR (Float, sv_maxfov, 179.f, CVAR_ARCHIVE|CVAR_SERVERINFO|CVAR_GAMEPLAYSETTING)
{
	ChangeFovLimit (self, sv_minfov, false);
}

float FOV_OnSpawn (bool isLocalPlayer)
{
	return zx::FovOnSpawn (isLocalPlayer, zx::net::IsServer(), fov, sv_minfov, sv_maxfov);
}
