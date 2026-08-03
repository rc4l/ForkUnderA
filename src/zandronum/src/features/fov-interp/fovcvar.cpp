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
		NETWORK_GetState( ) == NETSTATE_CLIENT,
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
	if (verdict == zx::FOV_SET_EVERYONE)
	{
		Net_WriteByte (DEM_FOV);
	}
	else
	{
		// Just do this here in client games, matching the old CCMD.
		if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
			player->DesiredFOV = static_cast<float> (requested);

		Net_WriteByte (DEM_MYFOV);
	}
	Net_WriteByte (requested);

	player->lastFOVChangeTic = gametic;
}
