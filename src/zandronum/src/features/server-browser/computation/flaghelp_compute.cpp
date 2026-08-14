// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See flaghelp_compute.h for where the wording comes from and the rule every line follows.

#include "features/server-browser/computation/flaghelp_compute.h"

#include <algorithm>

namespace zx
{

namespace
{

struct HelpEntry
{
	const char *name;
	const char *text;
};

// Kept in the order the flags are declared, field by field, so this reads against d_main.cpp
// rather than against itself. The table is sorted once when it is built.
const HelpEntry kHelp[] =
{
	// ------------------------------------------------------------------ dmflags
	{ "sv_nohealth",			"No health pickups spawn." },
	{ "sv_noitems",				"No powerups spawn." },
	{ "sv_noweaponspawn",		"Deathmatch-only weapons do not spawn in co-op." },
	{ "sv_noarmor",				"No armour spawns." },
	{ "sv_infiniteammo",		"Firing costs no ammo." },
	{ "sv_nomonsters",			"The map spawns no monsters." },
	{ "sv_monsterrespawn",		"Killed monsters come back." },
	{ "sv_itemrespawn",			"Picked-up items come back." },
	{ "sv_fastmonsters",		"Monsters move and attack faster." },
	{ "sv_nojump",				"Jumping is off, whatever the map asked for." },
	{ "sv_allowjump",			"Jumping is on, whatever the map asked for." },
	{ "sv_nofreelook",			"Looking up and down is off, whatever the map asked for." },
	{ "sv_allowfreelook",		"Looking up and down is on, whatever the map asked for." },
	{ "sv_nofov",				"Everyone sees at the host's field of view." },
	{ "sv_coop_loseinventory",	"Death costs you everything you carry." },
	{ "sv_coop_losekeys",		"Death costs you your keys." },
	{ "sv_coop_loseweapons",	"Death costs you your weapons." },
	{ "sv_coop_losearmor",		"Death costs you your armour." },
	{ "sv_coop_losepowerups",	"Death costs you your powerups." },
	{ "sv_coop_loseammo",		"Death costs you your ammo." },
	{ "sv_coop_halveammo",		"Death costs you half your ammo." },
	{ "sv_weaponstay",			"Weapons stay where they are after being taken." },
	{ "sv_falldamage",			"Falling hurts, the way it does in Hexen." },
	{ "sv_oldfalldamage",		"Falling hurts, the way it did in old ZDoom." },
	{ "sv_samelevel",			"The map restarts instead of moving on." },
	{ "sv_spawnfarthest",		"You respawn as far from the others as the map allows." },
	{ "sv_forcerespawn",		"The dead respawn on their own." },
	{ "sv_noexit",				"Trying to exit kills you instead." },
	{ "sv_nocrouch",			"Crouching is off, whatever the map asked for." },
	{ "sv_allowcrouch",			"Crouching is on, whatever the map asked for." },

	// ------------------------------------------------------------------ dmflags2
	{ "sv_weapondrop",			"Dying drops the weapon you were holding." },
	{ "sv_noteamswitch",		"Nobody may change team once the game has started." },
	{ "sv_doubleammo",			"Ammo pickups give twice as much." },
	{ "sv_degeneration",		"Health above your maximum drains back down to it." },
	{ "sv_bfgfreeaim",			"The BFG may be aimed freely." },
	{ "sv_barrelrespawn",		"Exploded barrels come back." },
	{ "sv_norespawninvul",		"No brief invulnerability after respawning." },
	{ "sv_shotgunstart",		"Everyone spawns holding a shotgun." },
	{ "sv_samespawnspot",		"You respawn where you died." },
	{ "sv_keepfrags",			"Frags carry over to the next map." },
	{ "sv_norespawn",			"The dead stay dead until the map changes." },
	{ "sv_losefrag",			"Being fragged costs you a frag." },
	{ "sv_infiniteinventory",	"Inventory items are never used up." },
	{ "sv_killallmonsters",		"The map must be cleared before the exit works." },
	{ "sv_noautomap",			"No automap." },
	{ "sv_noautomapallies",		"Allies do not appear on the automap." },
	{ "sv_disallowspying",		"You cannot watch through another player's eyes." },
	{ "sv_chasecam",			"Anyone may use the chase camera." },
	{ "sv_disallowsuicide",		"Nobody may kill themselves. The same switch as sv_nokill." },
	{ "sv_nokill",				"Nobody may kill themselves. The same switch as sv_disallowsuicide." },
	{ "sv_noautoaim",			"Autoaim is off for everyone." },
	{ "sv_dontcheckammo",		"Weapon switching ignores whether you have ammo for it." },
	{ "sv_killbossmonst",		"A boss dying kills whatever it spawned." },
	{ "sv_nocountendmonst",		"Monsters in the end-of-level sector do not count towards kills." },
	{ "sv_respawnsuper",		"Big powerups come back." },
	{ "sv_norunes",				"No runes spawn." },
	{ "sv_instantreturn",		"A dropped flag or skull returns at once." },
	{ "sv_noteamselect",		"The server picks your team for you." },

	// ------------------------------------------------------------------ zadmflags
	{ "sv_nomedals",			"No medals are awarded." },
	{ "sv_sharekeys",			"A key one player has opens that door for everyone." },
	{ "sv_noidentifytarget",	"Aiming at a player does not name them." },
	{ "sv_applylmsspectatorsettings", "The Last Man Standing spectator rules apply in every mode." },
	{ "sv_nounlagged",			"No lag compensation: shots land where the server sees you." },
	{ "sv_unblockplayers",		"Players walk through each other." },
	{ "sv_norocketjumping",		"Your own explosions do not launch you." },
	{ "sv_awarddamageinsteadkills", "Score comes from damage dealt rather than kills." },
	{ "sv_forcegldefaults",		"Clients must use the default video settings. An old name for sv_forcevideodefaults." },
	{ "sv_forcevideodefaults",	"Clients must use the default video settings." },
	{ "sv_nodrop",				"Players may not drop what they carry." },
	{ "sv_keepteams",			"Teams carry over to the next map." },
	{ "sv_noallyicons",			"No icon floats over an ally's head." },
	{ "sv_noenemyicons",		"No icon floats over an enemy's head." },
	{ "sv_nocoopinfo",			"No co-op information on the HUD." },
	{ "sv_nospawntelefog",		"A player spawning makes no teleport fog." },
	{ "sv_forcealpha",			"Clients must draw translucency as the map asks." },
	{ "sv_coop_spactorspawn",	"Actors spawn as they do in single player." },
	{ "sv_maxbloodscalar",		"The damage flash is drawn at full strength, whatever a client prefers." },
	{ "sv_unblockallies",		"You walk through your allies." },
	{ "sv_nounlaggedbfgtracers", "BFG tracers are not lag compensated." },
	{ "sv_shootthroughallies",	"Your shots pass through your allies." },
	{ "sv_dontpushallies",		"Your attacks do not shove your allies." },
	{ "sv_nodoorclose",			"A door cannot be pulled back down by hand." },
	{ "sv_survival_nomapresetondeath", "Survival does not reset the map when a life is lost." },
	{ "sv_deadplayerscankeepinventory", "The dead keep what they were carrying." },
	{ "sv_donthidestats",		"Other players' stats are not hidden from you." },
	{ "sv_dontkeepjoinqueue",	"The join queue is emptied between maps." },
	{ "sv_dontoverrideplayercolors", "Team play leaves everyone's own colours alone." },
	{ "sv_forcesoftwarepitchlimits", "Looking up and down is limited to the software renderer's range." },

	// ------------------------------------------------------------------ compatflags
	{ "compat_shortTex",		"Shortest textures are found the way Doom found them." },
	{ "compat_stairs",			"Stairs are built with Doom's buggier method." },
	{ "compat_limitpain",		"A Pain Elemental cannot exceed twenty Lost Souls." },
	{ "compat_silentpickup",	"Only you hear the things you pick up." },
	{ "compat_nopassover",		"Actors are infinitely tall: nothing can be stepped over." },
	{ "compat_soundslots",		"Sound is crippled the way the silent BFG trick needs." },
	{ "compat_wallrun",			"Wall running works." },
	{ "compat_notossdrops",		"Dropped items land on the floor rather than being tossed." },
	{ "compat_useblocking",		"Any special line can block a use." },
	{ "compat_nodoorlight",		"Doors do not take the BOOM light effect." },
	{ "compat_ravenscroll",		"Raven's scrolling floors run at their original speed." },
	{ "compat_soundtarget",		"Monsters are woken by sound the original way." },
	{ "compat_dehhealth",		"DEHACKED health limits behave as they did in Doom2.exe." },
	{ "compat_trace",			"Self-referencing sectors do not stop shots." },
	{ "compat_dropoff",			"Monsters get stuck over dropoffs." },
	{ "compat_boomscroll",		"Stacked BOOM scrollers add up instead of overriding." },
	{ "compat_invisibility",	"Monsters always see invisible players." },
	{ "compat_silentinstantfloors", "Floors that move instantly still make their sound." },
	{ "compat_sectorsounds",	"A sector's sounds come from its centre." },
	{ "compat_missileclip",		"Missiles clip against Doom's actor heights." },
	{ "compat_crossdropoff",	"Monsters will not walk off a dropoff." },
	{ "compat_anybossdeath",	"Any boss dying triggers the level's special." },
	{ "compat_minotaur",		"Minotaur floor flames do not appear in water." },
	{ "compat_mushroom",		"A_Mushroom keeps its original speed in DEHACKED mods." },
	{ "compat_mbfmonstermove",	"Scrollers, wind and friction push monsters." },
	{ "compat_corpsegibs",		"Crushed monsters can still be resurrected." },
	{ "compat_noblockfriends",	"Friendly monsters ignore lines that block monsters." },
	{ "compat_spritesort",		"Sprites at the same distance sort the other way round." },
	{ "compat_hitscan",			"Hitscans use Doom's own aiming and tracing." },
	{ "compat_light",			"Neighbouring light levels are found the way Doom found them." },
	{ "compat_polyobj",			"Polyobjects are drawn the way Hexen drew them." },
	{ "compat_maskedmidtex",	"Y offsets on masked midtextures are ignored." },

	// ------------------------------------------------------------------ compatflags2
	{ "compat_badangles",		"You cannot travel exactly north, south, east or west." },
	{ "compat_floormove",		"Floors move the way Doom moved them." },
	{ "compat_soundcutoff",		"A sound stops when whatever was making it is destroyed." },
	{ "compat_pushwindow",		"Lines that do not block can still be pushed." },

	// ------------------------------------------------------------------ zacompatflags
	{ "compat_netscriptsareclientside", "NET scripts run on the client rather than the server." },
	{ "compat_clientssendfullbuttoninfo", "Clients report every button they hold, not just the ones that move them." },
	{ "compat_noland",			"The land command is forbidden." },
	{ "compat_oldrandom",		"The old pseudo-random number generator." },
	{ "compat_nogravity_spheres", "Spheres float in place, as they did in Skulltag." },
	{ "compat_dont_stop_player_scripts_on_disconnect", "A player's scripts keep running after they leave." },
	{ "compat_explosionthrust",	"Explosions throw you the way old ZDoom threw you." },
	{ "compat_bridgedrops",		"Dropped items fall through bridges." },
	{ "compat_oldzdoomzmovement", "Vertical movement as old ZDoom did it." },
	{ "compat_fullweaponlower",	"A weapon must lower completely before the next one comes up." },
	{ "compat_autoaim",			"Autoaim has the vertical gaps it used to have." },
	{ "compat_silentwestspawns", "Spawning while facing west makes no sound." },
	{ "compat_plasmabump",		"Items are grabbed the way vanilla grabbed them." },
	{ "compat_instantrespawn",	"You respawn the instant you die." },
	{ "compat_disabletaunts",	"No taunts." },
	{ "compat_originalsoundcurve", "Sound fades with distance the original way." },
	{ "compat_oldintermission",	"The old intermission screens and music." },
	{ "compat_disablestealthmonsters", "Stealth monsters are visible like any other." },
	{ "compat_oldradiusdmg",	"Splash damage is infinitely tall." },
	{ "compat_nocrosshair",		"Nobody may use a crosshair." },
	{ "compat_oldweaponswitch",	"Picking a weapon up always switches to it." },
	{ "compat_noobituaries",	"No death messages." },
	{ "compat_limited_airmovement", "Steering while in the air is limited." },
	{ "compat_skulltagjumping",	"Jumping behaves the way it did in Skulltag." },
	{ "compat_resetglobalvarsonmapreset", "Global ACS variables are cleared when the map resets." },

	// ------------------------------------------------------------------ sv_forbidvoteflags
	{ "sv_nokickvote",			"No votes to kick a player." },
	{ "sv_noforcespecvote",		"No votes to force a player to spectate." },
	{ "sv_nomapvote",			"No votes to change to a chosen map." },
	{ "sv_nochangemapvote",		"No votes to change map at once." },
	{ "sv_nofraglimitvote",		"No votes on the frag limit." },
	{ "sv_notimelimitvote",		"No votes on the time limit." },
	{ "sv_nowinlimitvote",		"No votes on the win limit." },
	{ "sv_noduellimitvote",		"No votes on the duel limit." },
	{ "sv_nopointlimitvote",	"No votes on the point limit." },
	{ "sv_noflagvote",			"No votes to change a server flag." },
	{ "sv_nonextmapvote",		"No votes to skip to the next map." },
	{ "sv_nonextsecretvote",	"No votes to take the secret exit." },
	{ "sv_noresetmapvote",		"No votes to reset the map." },

	// ------------------------------------------------------------------ lmsallowedweapons
	{ "lms_allowpistol",		"The pistol may be used." },
	{ "lms_allowshotgun",		"The shotgun may be used." },
	{ "lms_allowssg",			"The super shotgun may be used." },
	{ "lms_allowchaingun",		"The chaingun may be used." },
	{ "lms_allowminigun",		"The minigun may be used." },
	{ "lms_allowrocketlauncher", "The rocket launcher may be used." },
	{ "lms_allowgrenadelauncher", "The grenade launcher may be used." },
	{ "lms_allowplasma",		"The plasma rifle may be used." },
	{ "lms_allowrailgun",		"The railgun may be used." },
	{ "lms_allowchainsaw",		"The chainsaw may be used." },

	// ------------------------------------------------------------------ lmsspectatorsettings
	{ "lms_spectatorview",		"The dead may watch the living." },
	{ "lms_spectatorchat",		"The dead may talk to the living." },
	{ "lms_spectatorvoicechat",	"The dead may speak to the living over voice." },
};

// [rc4l] The fields themselves, on the heading that folds each one. Nine lines rather than a
// paragraph: the heading answers "is what I want in here", and the switches inside answer the rest.
const HelpEntry kFieldHelp[] =
{
	{ "dmflags",				"Doom's own gameplay switches: what spawns, what respawns, what players may do." },
	{ "dmflags2",				"Further gameplay switches, added after Doom's own." },
	{ "zadmflags",				"Zandronum's server switches, mostly about players, allies and teams." },
	{ "compatflags",			"Bug-for-bug behaviour of the original engines, for maps built against it." },
	{ "compatflags2",			"Further original-engine behaviour, added later." },
	{ "zacompatflags",			"Zandronum's compatibility switches, mostly for older online behaviour." },
	{ "sv_forbidvoteflags",		"Which votes players may not call." },
	{ "lmsallowedweapons",		"Which weapons Last Man Standing allows." },
	{ "lmsspectatorsettings",	"What the dead may do while a round runs." },
};

bool ByName(const HelpEntry &a, const HelpEntry &b)
{
	return std::string(a.name) < std::string(b.name);
}

const std::vector<std::pair<std::string, std::string> > &Built()
{
	static std::vector<std::pair<std::string, std::string> > table;

	if (!table.empty())
		return table;

	std::vector<HelpEntry> sorted(kHelp, kHelp + (sizeof(kHelp) / sizeof(kHelp[0])));
	std::sort(sorted.begin(), sorted.end(), ByName);

	table.reserve(sorted.size());
	for (size_t i = 0; i < sorted.size(); ++i)
	{
		// A blank line is a placeholder for a flag this build may or may not have; it is dropped
		// rather than shown, so hovering it produces nothing instead of an empty box.
		if (sorted[i].text[0] == 0)
			continue;

		table.push_back(std::make_pair(std::string(sorted[i].name), std::string(sorted[i].text)));
	}

	return table;
}

} // namespace

const std::vector<std::pair<std::string, std::string> > &FlagHelpTable()
{
	return Built();
}

const char *FlagHelp(const std::string &name)
{
	const std::vector<std::pair<std::string, std::string> > &table = Built();

	// Sorted, so this is a bisection rather than a walk of two hundred entries per pill per frame.
	size_t lo = 0;
	size_t hi = table.size();

	while (lo < hi)
	{
		const size_t mid = lo + (hi - lo) / 2;

		if (table[mid].first < name)
			lo = mid + 1;
		else
			hi = mid;
	}

	if ((lo < table.size()) && (table[lo].first == name))
		return table[lo].second.c_str();

	return "";
}

const char *FlagFieldHelp(const std::string &name)
{
	// Nine entries, so this is a walk. A bisection here would be arithmetic nobody can check for
	// the sake of four comparisons.
	for (size_t i = 0; i < (sizeof(kFieldHelp) / sizeof(kFieldHelp[0])); ++i)
	{
		if (name == kFieldHelp[i].name)
			return kFieldHelp[i].text;
	}

	return "";
}

} // namespace zx
