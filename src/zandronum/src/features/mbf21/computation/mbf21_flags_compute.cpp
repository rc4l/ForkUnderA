// [rc4l] Implementation of the MBF21 thing-flag mnemonic lookup. See mbf21_flags_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "mbf21_flags_compute.h"

namespace zx { namespace mbf21 {

namespace {

struct BitName { unsigned bit; const char *name; };

// The mnemonics the spec accepts in "MBF21 Bits", paired with their bit. Order is irrelevant here
// (the field ORs them together); it mirrors the enum for readability.
const BitName kThingBits[] =
{
	{ DEH21F_LOGRAV,         "LOGRAV" },
	{ DEH21F_SHORTMRANGE,    "SHORTMRANGE" },
	{ DEH21F_DMGIGNORED,     "DMGIGNORED" },
	{ DEH21F_NORADIUSDMG,    "NORADIUSDMG" },
	{ DEH21F_FORCERADIUSDMG, "FORCERADIUSDMG" },
	{ DEH21F_HIGHERMPROB,    "HIGHERMPROB" },
	{ DEH21F_RANGEHALF,      "RANGEHALF" },
	{ DEH21F_NOTHRESHOLD,    "NOTHRESHOLD" },
	{ DEH21F_LONGMELEE,      "LONGMELEE" },
	{ DEH21F_BOSS,           "BOSS" },
	{ DEH21F_MAP07BOSS1,     "MAP07BOSS1" },
	{ DEH21F_MAP07BOSS2,     "MAP07BOSS2" },
	{ DEH21F_E1M8BOSS,       "E1M8BOSS" },
	{ DEH21F_E2M8BOSS,       "E2M8BOSS" },
	{ DEH21F_E3M8BOSS,       "E3M8BOSS" },
	{ DEH21F_E4M6BOSS,       "E4M6BOSS" },
	{ DEH21F_E4M8BOSS,       "E4M8BOSS" },
	{ DEH21F_RIP,            "RIP" },
	{ DEH21F_FULLVOLSOUNDS,  "FULLVOLSOUNDS" },
};

// ASCII case-insensitive equality of a query against a table entry. `upperName` always comes from
// kThingBits and is uppercase ASCII by construction, so only the query side is folded -- a locale-
// free, header-free (no <strings.h>) compare where every branch is reachable from a test.
bool CaseEq(const char *query, const char *upperName)
{
	for (;; ++query, ++upperName)
	{
		char c = *query;
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
		if (c != *upperName) return false;
		if (c == '\0') return true;
	}
}

} // namespace

unsigned ComputeMbf21ThingBitFromName(const char *name)
{
	if (name == nullptr) return 0;
	for (const BitName &bn : kThingBits)
	{
		if (CaseEq(name, bn.name)) return bn.bit;
	}
	return 0;
}

namespace {
const BitName kWeaponBits[] =
{
	{ DEH21WF_NOTHRUST,       "NOTHRUST" },
	{ DEH21WF_SILENT,         "SILENT" },
	{ DEH21WF_NOAUTOFIRE,     "NOAUTOFIRE" },
	{ DEH21WF_FLEEMELEE,      "FLEEMELEE" },
	{ DEH21WF_AUTOSWITCHFROM, "AUTOSWITCHFROM" },
	{ DEH21WF_NOAUTOSWITCHTO, "NOAUTOSWITCHTO" },
};
} // namespace

unsigned ComputeMbf21WeaponBitFromName(const char *name)
{
	if (name == nullptr) return 0;
	for (const BitName &bn : kWeaponBits)
	{
		if (CaseEq(name, bn.name)) return bn.bit;
	}
	return 0;
}

}} // namespace zx::mbf21
