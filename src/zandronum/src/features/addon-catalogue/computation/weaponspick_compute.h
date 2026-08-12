// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How fast weapons fire, and what that drags along with it.
//
// sv_fastweapons is an Int 0 to 2 and the cvar clamps itself to that range, so the range here is the
// engine's rather than a number chosen in the panel:
//
//   0  the weapons as the pack timed them
//   1  every weapon state cut to a single tick
//   2  as 1, and the states with no action function take no time at all
//
// Above zero it also turns INFINITE AMMO on, which is the part worth having a unit for. It is not a
// second setting that happens to be set nearby: a weapon firing at two or three times its designed
// rate empties a backpack in a fraction of the time the pack's ammo placement assumes, and every map
// balanced on that placement stops being finishable. The two belong together, so they are decided
// together in one place rather than left to each caller to remember.
//
// Never turned back OFF at zero. The packs that ask for the control are packs whose own cfgs already
// set sv_infiniteammo -- Invasion UAC and Destination Unknown cannot be finished without it -- and a
// control that wrote "false" at its default stop would quietly overrule them.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_WEAPONSPICK_COMPUTE_H
#define ZX_WEAPONSPICK_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx
{

// The lowest and highest sv_fastweapons the engine keeps, and how many stops that is.
int FastWeaponsMax();

// What is in force: `wanted` clamped into range, with any NEGATIVE meaning nothing chosen. One rule
// rather than a special case for -1, the same reading LivesFor gives a negative.
int FastWeaponsValue(int wanted);

// The cvars that put `wanted` into effect, for an entry that OFFERS the control. An entry that does
// not gets an empty list: a pack that never invited the setting must not be handed a zero that
// overrides its own cfg.
std::vector<std::pair<std::string, std::string> > FastWeaponsCvars(bool offered, int wanted);

// [rc4l] The weapon speed and the mix cannot both be had, and this says which one has the panel.
//
// sv_fastweapons works on the states of whatever weapons are loaded. Brutal Doom, Complex Doom and
// Hard Doom all REPLACE the weapons, with their own timings, their own reload frames and in Complex
// Doom's case a random weapon set per pickup. Cutting every state to a tick on top of that is not a
// faster version of that mod; it is that mod with its animation system taken away, and what comes
// out is a different bug depending on which one was loaded.
//
// So at most one of the two is in force. Whichever is already off its default locks the other, and
// there is no order to remember because the lock is symmetric:
//
//   * A speed above Normal takes the mix back to the baseline and holds it there.
//   * A mix off the baseline pins the speed to Normal.
//
// The speed wins ties. It has to win something, and a remembered mix carried in from another
// experience is the likelier of the two to be the stale one -- a mix is kept across every entry that
// offers it, and the speed is only offered by the handful that ask for it.
struct WeaponsPlan
{
	int speed;				// the speed actually in force; 0 whenever a mod owns the weapons

	bool speedAdjustable;	// whether the slider may be moved at all
	bool mixLocked;			// whether the mix pills are inert
	bool forceBaselineMix;	// whether the mix in force is the baseline whatever was chosen

	WeaponsPlan() : speed(0), speedAdjustable(false), mixLocked(false), forceBaselineMix(false) {}
};

// `offered` and `wanted` are as FastWeaponsCvars reads them. `mixIsBaseline` is whether the mix axis
// is sitting on its first offered choice, which is the one that adds nothing.
WeaponsPlan PlanWeapons(bool offered, int wanted, bool mixIsBaseline);

} // namespace zx

#endif // ZX_WEAPONSPICK_COMPUTE_H
