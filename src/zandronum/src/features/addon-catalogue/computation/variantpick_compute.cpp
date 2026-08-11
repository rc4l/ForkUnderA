// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/variantpick_compute.h"

namespace zx
{

namespace
{

// The entry's files, then the chosen variant's. Order is load order, and the entry's come first
// because they are the base a variant adds to.
void ResolveFiles(const AddonEntry &entry, const AddonVariant &variant,
	std::vector<AddonFileRef> &out)
{
	out = entry.files;
	out.insert(out.end(), variant.files.begin(), variant.files.end());
}

} // namespace

VariantPick PickVariant(const AddonEntry &entry, const std::string &wantedId)
{
	VariantPick pick;

	if (entry.variants.empty())
	{
		pick.cfg = kDefaultVariantCfg;
		pick.files = entry.files;
		return pick;
	}

	// What the player asked for, if it is still there. Checked FIRST so a remembered choice survives
	// a catalogue update that added variants around it.
	if (!wantedId.empty())
	{
		for (size_t i = 0; i < entry.variants.size(); ++i)
		{
			if (entry.variants[i].id != wantedId)
				continue;

			pick.index = static_cast<int>(i);
			pick.cfg = entry.variants[i].cfg;
			pick.name = entry.variants[i].name;
			ResolveFiles(entry, entry.variants[i], pick.files);
			return pick;
		}
	}

	// No preference, or one that named a variant this catalogue no longer has. Both land on the
	// default: a choice that has been deleted is not a reason to show nothing, and the player who
	// never chose has not expressed an opinion to honour.
	size_t chosen = 0;
	for (size_t i = 0; i < entry.variants.size(); ++i)
	{
		if (entry.variants[i].isDefault)
		{
			chosen = i;
			break;
		}
	}

	pick.index = static_cast<int>(chosen);
	pick.cfg = entry.variants[chosen].cfg;
	pick.name = entry.variants[chosen].name;
	ResolveFiles(entry, entry.variants[chosen], pick.files);
	return pick;
}

std::string ComposeServerName(const std::string &entryName, const std::string &variantName,
	const std::string &suffix)
{
	std::string out = entryName;

	// A colon rather than a dash or brackets: the variant qualifies the pack rather than being an
	// aside about it, and a server list is read at a glance.
	if (!variantName.empty())
	{
		if (!out.empty())
			out += ": ";
		out += variantName;
	}

	if (!suffix.empty())
	{
		if (!out.empty())
			out += " ";
		out += "(" + suffix + ")";
	}

	return out;
}

} // namespace zx
