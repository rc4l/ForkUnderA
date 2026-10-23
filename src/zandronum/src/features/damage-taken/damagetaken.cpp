// [MobiusU9B] See damagetaken.h; the strike and its text live in computation/damagetaken_compute so this file is only engine glue.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "actor.h"
#include "c_console.h"    // Printf
#include "c_dispatch.h"   // CCMD
#include "d_player.h"     // players, player_t
#include "doomdef.h"      // TICRATE, MAXPLAYERS, TELEFRAG_DAMAGE
#include "doomstat.h"     // consoleplayer, playeringame
#include "g_level.h"      // level
#include "network.h"      // NETWORK_GetState, NETWORK_InClientMode
#include "sv_commands.h"  // SERVERCOMMANDS_FuaDamageTaken
#include "sv_main.h"      // SERVER_IsValidClient
#include "v_text.h"       // TEXTCOLOR_GOLD, TEXTCOLOR_NORMAL
#include "g_shared/a_pickups.h"

#include "features/damage-taken/damagetaken.h"
#include "features/damage-taken/computation/damagetaken_compute.h"

#include <string>
#include <vector>
#include "features/net-session/netrole.h"

namespace
{
	// [MobiusU9B] Only the console player's last strike is kept, and only in this process.
	zx::damagetaken::DamageTakenStrike g_Strike = { "", -1, {} };

	// [MobiusU9B] A monster's tag (its in-game name), or a class name when asked for one.
	std::string ClassLabel(const PClass *cls, bool useTag)
	{
		if (cls == NULL)
			return std::string();
		// [MobiusU9B] GetTag needs the fallback name: without one it calls GetClass() on the class's defaults, as pwo.cpp avoids too.
		if (useTag && cls->IsDescendantOf(RUNTIME_CLASS(AActor)))
			return GetDefaultByType(cls)->GetTag(cls->TypeName.GetChars());
		return cls->TypeName.GetChars();
	}

	// [MobiusU9B] The class to name as "what hit you", or NULL when the attacker hit directly with nothing to name.
	const PClass *WeaponClass(AActor *source, AActor *inflictor)
	{
		const bool attackerHasWeapon = source != NULL && source->player != NULL && source->player->ReadyWeapon != NULL;
		switch (zx::damagetaken::ComputeDamageTakenWeaponSource(inflictor == source, inflictor->isMissile(true),
			(inflictor->GetDefault()->flags & MF_SHOOTABLE) != 0, attackerHasWeapon))
		{
		case zx::damagetaken::DamageTakenWeaponSource::Inflictor:
			return inflictor->GetClass();
		case zx::damagetaken::DamageTakenWeaponSource::AttackerWeapon:
			return source->player->ReadyWeapon->GetClass();
		case zx::damagetaken::DamageTakenWeaponSource::None:
			break;
		}
		return NULL;
	}

	// [MobiusU9B] Turns a report into labels and adds it to the console player's strike.
	void Record(const zx::damagetaken::Report &report)
	{
		using namespace zx::damagetaken;

		const bool attackerIsPlayer = report.attackerPlayer < MAXPLAYERS && playeringame[report.attackerPlayer];
		std::string playerName;
		if (attackerIsPlayer)
		{
			// [MobiusU9B] Names can carry colour codes, so the text after one goes back to the normal colour.
			playerName = players[report.attackerPlayer].userinfo.GetName();
			playerName += TEXTCOLOR_NORMAL;
		}

		DamageTakenRoll roll;
		roll.damage = report.damage;
		roll.armorAbsorbed = report.armorAbsorbed;
		roll.healthLost = report.healthLost;
		roll.telefrag = report.damage >= TELEFRAG_DAMAGE;
		roll.lethal = report.lethal;
		roll.attacker = ComputeDamageTakenAttackerLabel(attackerIsPlayer || report.attackerClass != NULL,
			attackerIsPlayer && report.attackerPlayer == static_cast<ULONG>(consoleplayer), playerName,
			ClassLabel(report.attackerClass, true));
		roll.weapon = ClassLabel(report.weaponClass, false);
		roll.type = ComputeDamageTakenTypeLabel(report.damageType != NULL ? report.damageType : "");
		ComputeDamageTakenAddRoll(g_Strike, roll, level.MapName.GetChars(), report.mapTime);
	}
}

namespace zx { namespace damagetaken {

void OnPlayerHit(player_t *player, AActor *source, AActor *inflictor, FName mod, const HitAmounts &hit)
{
	// [MobiusU9B] A client records only what the server reports, so damage it runs locally never counts.
	if (player == NULL || zx::net::InClientMode() || !ComputeDamageTakenIsRoll(inflictor != NULL, hit.armorAbsorbed, hit.healthLost))
		return;

	const ULONG playerIndex = static_cast<ULONG>(player - players);
	const bool server = zx::net::IsServer();
	if (server ? !SERVER_IsValidClient(playerIndex) : player != &players[consoleplayer])
		return;

	Report report;
	report.mapTime = level.maptime;
	report.damage = hit.damage;
	report.armorAbsorbed = hit.armorAbsorbed;
	report.healthLost = hit.healthLost;
	report.attackerPlayer = (source != NULL && source->player != NULL && source->player->mo == source)
		? static_cast<ULONG>(source->player - players) : MAXPLAYERS;
	report.attackerClass = source != NULL ? source->GetClass() : NULL;
	report.weaponClass = WeaponClass(source, inflictor);
	report.damageType = mod.GetChars();
	report.lethal = player->health <= 0;

	if (server)
		SERVERCOMMANDS_FuaDamageTaken(playerIndex, report);
	else
		Record(report);
}

void OnServerReport(const Report &report)
{
	Record(report);
}

} } // namespace zx::damagetaken

// [MobiusU9B] fua_damagetaken: prints the last strike the console player took, roll by roll.
CCMD( fua_damagetaken )
{
	const std::vector<std::string> lines = zx::damagetaken::ComputeDamageTakenLines(g_Strike, TICRATE);
	for (size_t i = 0; i < lines.size(); ++i)
		Printf("%s%s\n", i == 0 ? TEXTCOLOR_GOLD : "", lines[i].c_str());
}
