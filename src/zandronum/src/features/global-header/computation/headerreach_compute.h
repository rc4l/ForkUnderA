// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What colour "Play Online!" is, and why.
//
// The tab is the most prominent thing on the menu, so it has to be honest about whether pressing it
// will lead anywhere. Three answers worth telling apart:
//
//   GREEN   a registry answered us, so the wider internet is demonstrably reachable.
//   ORANGE  no registry answered, but we have a real local address, so LAN play still works.
//   GREY    neither. Pressing this leads to an empty list and a form that cannot publish.
//
// CHECKING IS NOT GREY, and that distinction is the whole reason this is a unit rather than two
// nested ifs at the draw site. A registry query takes a few seconds, and for those seconds "we have
// not heard back yet" and "you have no internet" are indistinguishable if you only look at whether
// an answer arrived. Painting the tab grey in that window tells the player their network is broken
// every single time they open the menu, and they will believe it, because it is the first thing
// they see. The exact shape of the throttled-status bug, which is recent enough to still sting.
//
// PROOF BEATS PENDING. One registry answering is proof of internet even while three others are
// still outstanding, so an Ok anywhere in the list settles it immediately rather than waiting for
// the slowest one to time out.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_HEADERREACH_COMPUTE_H
#define ZX_HEADERREACH_COMPUTE_H

namespace zx
{

enum class HeaderReach
{
	// Asked, nothing back yet. An honest "hang on", never a verdict.
	Checking,

	// A registry answered. The internet is reachable, whatever else is true.
	Internet,

	// Nothing answered, but there is a real local address to host and play on.
	LanOnly,

	// No network worth the name.
	Offline,
};

// How the tab should be painted. Kept separate from the verdict so the drawing does not have to
// re-derive "which of these count as bad", and so a fourth verdict later cannot silently inherit a
// colour nobody chose for it.
enum class ReachTint
{
	Neutral,  // still asking; the tab's ordinary colour, not a judgement
	Green,
	Orange,
	Grey,
};

struct ReachIn
{
	// At least one registry came back Ok. Proof, and it outranks everything else here.
	bool anyRegistryAnswered;

	// At least one registry is still outstanding. What separates Checking from Offline.
	bool anyRegistryPending;

	// We hold an address that is not loopback, so there is a network attached even if nothing on
	// the far side of it has spoken to us.
	bool haveLocalNetwork;

	ReachIn() : anyRegistryAnswered(false), anyRegistryPending(false), haveLocalNetwork(false) {}
};

HeaderReach ComputeHeaderReach(const ReachIn &in);

ReachTint HeaderReachTint(HeaderReach reach);

// The hover text. Says what is true and, when something is wrong, what it means for the player --
// a tooltip that only restates the colour is a tooltip nobody reads twice.
const char *HeaderReachTooltip(HeaderReach reach);

// Can the tab be pressed? Only Offline says no, and it says no because the far side is an empty
// list and a host form that cannot publish. Checking stays pressable: making the player wait on a
// timer they cannot see is worse than letting them open a list that fills in a moment.
bool PlayOnlineSelectable(HeaderReach reach);

} // namespace zx

#endif // ZX_HEADERREACH_COMPUTE_H
