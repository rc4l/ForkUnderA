// [rc4l] MBF21 thing-flag bit constants + the mnemonic->bit lookup used to parse the DeHackEd
// "MBF21 Bits" field. Kept pure (no engine headers) so the mnemonic table -- the part that is easy
// to get subtly wrong -- is unit-testable off-engine. The engine (d_dehacked.cpp
// DEH_ChangeMBF21Flags) translates a set of these DEH21F_ bits onto the native actor flag words.
// Ported behaviour: DSDA-Doom + Zandronum lz/mbf21; MBF21 spec v1.4.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_MBF21_FLAGS_COMPUTE_H
#define ZX_MBF21_FLAGS_COMPUTE_H

namespace zx { namespace mbf21 {

// MBF21 thing-flag bits, exactly as ordered in the spec's "MBF21 Bits" field. These are an
// intermediate representation: the parser ORs one bit per mnemonic, then the engine maps the set
// onto native flags (some become flags2/3/4 bits, some become gravity / ranges, nine have no
// native equivalent and live in flags8 -- see MF8_* in actor.h).
enum
{
	DEH21F_LOGRAV        = 0x00000001,   // low gravity (1/8)
	DEH21F_SHORTMRANGE   = 0x00000002,   // short missile range (archvile)
	DEH21F_DMGIGNORED    = 0x00000004,   // other things ignore its attacks (archvile)
	DEH21F_NORADIUSDMG   = 0x00000008,   // doesn't take splash damage
	DEH21F_FORCERADIUSDMG= 0x00000010,   // deals splash damage even to NORADIUSDMG targets
	DEH21F_HIGHERMPROB   = 0x00000020,   // higher missile attack probability (cyberdemon)
	DEH21F_RANGEHALF     = 0x00000040,   // use half distance for missile attack probability
	DEH21F_NOTHRESHOLD   = 0x00000080,   // no targeting threshold (archvile)
	DEH21F_LONGMELEE     = 0x00000100,   // long melee range (revenant)
	DEH21F_BOSS          = 0x00000200,   // full-volume see/death sound + splash immunity
	DEH21F_MAP07BOSS1    = 0x00000400,   // tag 666 boss on Doom 2 MAP07
	DEH21F_MAP07BOSS2    = 0x00000800,   // tag 667 boss on Doom 2 MAP07
	DEH21F_E1M8BOSS      = 0x00001000,   // E1M8 boss
	DEH21F_E2M8BOSS      = 0x00002000,   // E2M8 boss
	DEH21F_E3M8BOSS      = 0x00004000,   // E3M8 boss
	DEH21F_E4M6BOSS      = 0x00008000,   // E4M6 boss
	DEH21F_E4M8BOSS      = 0x00010000,   // E4M8 boss
	DEH21F_RIP           = 0x00020000,   // ripper projectile
	DEH21F_FULLVOLSOUNDS = 0x00040000,   // full-volume see/death sounds

	DEH21F_ALLFLAGS      = 0x0007ffff,   // union of every bit above (used to replace all at once)
};

// Resolves one "MBF21 Bits" mnemonic (case-insensitive, e.g. "RIP", "map07boss1") to its DEH21F_
// bit. Returns 0 for an unrecognised name or a null pointer. The numeric form (e.g. "0x40") is a
// caller concern -- this only knows the mnemonic table.
unsigned ComputeMbf21ThingBitFromName(const char *name);

// MBF21 WEAPON-flag bits, as ordered in the weapon "MBF21 Bits" field. Like the thing bits these
// are an intermediate representation the engine maps onto native weapon flags (see PatchWeapon).
enum
{
	DEH21WF_NOTHRUST       = 0x001,   // doesn't thrust the player's target
	DEH21WF_SILENT         = 0x002,   // weapon is silent (doesn't alert monsters)
	DEH21WF_NOAUTOFIRE     = 0x004,   // can't fire by holding the button after switching to it
	DEH21WF_FLEEMELEE      = 0x008,   // monsters consider it a melee weapon (flee when wielding)
	DEH21WF_AUTOSWITCHFROM = 0x010,   // auto-switch away from it when out of ammo, even if pref lower
	DEH21WF_NOAUTOSWITCHTO = 0x020,   // never auto-switch TO this weapon on pickup

	DEH21WF_ALLFLAGS       = 0x03f,   // union of every weapon bit above
};

// Resolves one weapon "MBF21 Bits" mnemonic (case-insensitive, e.g. "SILENT") to its DEH21WF_ bit.
// Returns 0 for an unrecognised name or a null pointer.
unsigned ComputeMbf21WeaponBitFromName(const char *name);

}} // namespace zx::mbf21

#endif // ZX_MBF21_FLAGS_COMPUTE_H
