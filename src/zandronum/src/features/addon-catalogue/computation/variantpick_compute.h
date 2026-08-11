// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which way of playing an entry the player gets, and what to call the server it starts.
//
// Separate from the parsing because it answers a different question at a different time: the file
// says what the choices ARE, this says which one is in force given what the player last picked.
// Those drift apart the moment a catalogue is updated -- a remembered choice can name a variant that
// no longer exists, and the answer to that must be a definite fallback rather than an empty panel.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_VARIANTPICK_COMPUTE_H
#define ZX_VARIANTPICK_COMPUTE_H

#include <string>
#include <vector>

#include "features/addon-catalogue/computation/addonfile_compute.h"

namespace zx
{

struct VariantPick
{
	// Index into the entry's variants, or -1 when the entry has none. -1 is not a failure: it is the
	// ordinary case of a pack that plays one way, and the caller draws no panel for it.
	int index;

	// What to exec. Always answered, variants or not, so the caller never has to special-case the
	// plain entry: with no variants this is the pack's own server.cfg.
	std::string cfg;

	// What to show, and what the server is called. Empty when there are no variants.
	std::string name;

	// Everything this way of playing loads, in load order: the entry's own files followed by the
	// variant's. Resolved here so no caller has to remember that the two lists concatenate, and so
	// the panel, the download plan and the launch cannot each answer it differently.
	std::vector<AddonFileRef> files;

	VariantPick() : index(-1) {}
};

// The cfg an entry without variants plays, and the one a default variant should name so that an
// older build, which ignores variants entirely, lands on the same thing.
const char *const kDefaultVariantCfg = "server.cfg";

// `wantedId` is what the player last chose, which may be empty (no preference yet) or name a variant
// that has since been removed from the catalogue. Either way the answer is the entry's default, or
// the first variant when none claims to be default.
VariantPick PickVariant(const AddonEntry &entry, const std::string &wantedId);

// What to call a server running this. The variant goes in the NAME because a joiner reading a list
// of servers cannot see the cfg: "Skulltag" tells them nothing about whether they are about to join
// an invasion or a duel, and finding out by joining is the cost this avoids.
std::string ComposeServerName(const std::string &entryName, const std::string &variantName,
	const std::string &suffix);

} // namespace zx

#endif // ZX_VARIANTPICK_COMPUTE_H
