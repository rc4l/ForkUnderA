// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/slotreport_compute.h"
#include "features/hwrender/computation/spriteatlas_compute.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <map>

namespace zx { namespace hwrender {

std::string ComputeSpriteFamily(const std::string &name)
{
	return name.substr(0, 4);
}

namespace {

// [Doruk-Aksoy] A plain sprite of at most maxSide a side, as ComputeAtlasEligible judges one drawn point sampled on the per-material path.
bool AtlasTakes(const SlotFacts &f, int maxSide)
{
	const AtlasCandidate c = { true, true, f.use == kSlotUseSprite, !f.shaded && !f.special, f.width, f.height };
	return ComputeAtlasEligible(c, maxSide);
}

// [Doruk-Aksoy] Places the image with its one-texel gutter, the page it went on, or -1 when it fits on none.
int Pack(std::vector<AtlasPage> &pages, int pageSize, const SlotFacts &f)
{
	AtlasPlace at;
	return ComputeAtlasPlace(pages, INT_MAX, pageSize, pageSize, f.width + 2, f.height + 2, at) ? at.page : -1;
}

// [Doruk-Aksoy] Bytes as whole MiB, rounded.
std::string MiB(long long bytes)
{
	return std::to_string((bytes + (1 << 19)) >> 20);
}

} // namespace

void ComputeSlotReport(const std::vector<SlotFacts> &slots, int pageSize, int maxSide, SlotTally &out)
{
	out = SlotTally();
	out.slots = (int)slots.size();
	std::vector<AtlasPage> plain, all, brightOnly, rest;
	std::vector<char> mixedBright;   // [Doruk-Aksoy] per page of `all`, whether it holds a brightmapped image
	std::map<std::string, SlotFamily> families;
	for (const SlotFacts &f : slots)
	{
		const int use = f.use >= 0 && f.use < kSlotUseCount ? f.use : kSlotUseOther;
		(f.brightmap ? out.brightmaps : out.images)[use]++;
		if (f.translated) out.translated++;
		if (f.shaded) out.shaded++;
		if (f.special) out.special++;
		if (f.width > maxSide || f.height > maxSide) out.oversize++;
		if (use == kSlotUseSprite)
		{
			SlotFamily &family = families[ComputeSpriteFamily(f.name)];
			(f.brightmap ? family.brightmaps : family.images)++;
			if (f.translated) family.translated++;
		}
		if (!AtlasTakes(f, maxSide)) continue;
		const long long bytes = (long long)f.width * f.height * 4;
		if (f.brightmap) { out.atlasBrightmaps++; out.ownBytes += bytes; continue; }
		const int page = Pack(all, pageSize, f);
		if (page < 0) continue;
		out.ownBytes += bytes;
		mixedBright.resize(all.size(), 0);
		if (f.hasBrightmap) mixedBright[page] = 1;
		Pack(f.hasBrightmap ? brightOnly : rest, pageSize, f);
		if (f.translated) { out.atlasTranslated++; continue; }
		Pack(plain, pageSize, f);
		out.atlasImages++;
	}
	out.atlasPages = (int)plain.size();
	out.atlasPagesAll = (int)all.size();
	out.mixedBrightPages = (int)std::count(mixedBright.begin(), mixedBright.end(), 1);
	out.groupedColourPages = (int)(brightOnly.size() + rest.size());
	out.groupedBrightPages = (int)brightOnly.size();
	for (auto &kv : families)
	{
		kv.second.name = kv.first;
		out.families.push_back(kv.second);
	}
	std::stable_sort(out.families.begin(), out.families.end(), [](const SlotFamily &a, const SlotFamily &b) {
		return a.images + a.brightmaps > b.images + b.brightmaps;
	});
}

int ComputeSlotsWithAtlas(int slots, int images, int brightmaps, int pages)
{
	return slots - images - brightmaps + pages * (brightmaps > 0 ? 2 : 1);
}

std::string ComputeSlotReportText(const SlotTally &r, int inTable, int capacity, int pageSize, int maxSide, int top)
{
	static const char *const kUses[kSlotUseCount] = { "world", "sprites", "skins", "graphics", "other" };
	const std::string n = "\n";
	std::string s = "slots wanted: " + std::to_string(r.slots) + " (" + std::to_string(inTable) + " in the table of " +
		std::to_string(capacity) + ", " + std::to_string(r.slots - inTable) + " refused once it filled)" + n + " ";
	for (int u = 0; u < kSlotUseCount; u++)
		s += std::string(u ? " |" : "") + " " + kUses[u] + " " + std::to_string(r.images[u]) + " + " + std::to_string(r.brightmaps[u]) + " brightmaps";
	s += n + "  translated " + std::to_string(r.translated) + ", GLDEFS-shaded " + std::to_string(r.shaded) + ", camera or warp " +
		std::to_string(r.special) + ", over " + std::to_string(maxSide) + " px " + std::to_string(r.oversize) + n;
	s += "  atlas of " + std::to_string(pageSize) + " pages: " + std::to_string(r.atlasImages) + " plain sprite images and " +
		std::to_string(r.atlasBrightmaps) + " brightmaps on " + std::to_string(r.atlasPages) + " pages, so " + std::to_string(r.slots) +
		" slots become " + std::to_string(ComputeSlotsWithAtlas(r.slots, r.atlasImages, r.atlasBrightmaps, r.atlasPages)) + n;
	s += "  with translated sprites too: " + std::to_string(r.atlasImages + r.atlasTranslated) + " images on " +
		std::to_string(r.atlasPagesAll) + " pages, so " +
		std::to_string(ComputeSlotsWithAtlas(r.slots, r.atlasImages + r.atlasTranslated, r.atlasBrightmaps, r.atlasPagesAll)) + n;
	const long long page = (long long)pageSize * pageSize * 4;
	s += "  memory: their own textures " + MiB(r.ownBytes) + " MiB, " + MiB(r.ownBytes * 4 / 3) + " with mips | pages with brightmapped images mixed in: " +
		std::to_string(r.atlasPagesAll) + " colour + " + std::to_string(r.mixedBrightPages) + " brightmap = " +
		MiB(page * (r.atlasPagesAll + r.mixedBrightPages)) + " MiB | brightmapped images on pages of their own: " +
		std::to_string(r.groupedColourPages) + " + " + std::to_string(r.groupedBrightPages) + " = " +
		MiB(page * (r.groupedColourPages + r.groupedBrightPages)) + " MiB" + n;
	s += "  sprite families by slots:";
	const int shown = std::min(top, (int)r.families.size());
	for (int i = 0; i < shown; i++)
	{
		const SlotFamily &f = r.families[i];
		s += std::string(i ? "," : "") + " " + f.name + " " + std::to_string(f.images + f.brightmaps) + " (" + std::to_string(f.brightmaps) +
			" brightmaps, " + std::to_string(f.translated) + " translated)";
	}
	if (shown <= 0) s += " none";
	return s;
}

void ComputeWorkingSet(const std::vector<SlotStamp> &stamps, long long frame, double nowMs, const std::vector<double> &windowsMs, std::vector<int> &counts)
{
	counts.resize(windowsMs.size() + 1, 0);
	for (const SlotStamp &s : stamps)
	{
		if (s.frame < 0) continue;
		if (s.frame == frame) counts[0]++;
		for (size_t w = 0; w < windowsMs.size(); w++)
			if (nowMs - s.ms <= windowsMs[w]) counts[w + 1]++;
	}
}

void ComputeKeepPeaks(std::vector<int> &peaks, const std::vector<int> &counts)
{
	if (peaks.size() < counts.size()) peaks.resize(counts.size(), 0);
	for (size_t i = 0; i < counts.size(); i++) peaks[i] = std::max(peaks[i], counts[i]);
}

std::string ComputeWorkingSetText(const std::vector<int> &now, const std::vector<int> &peaks, const std::vector<double> &windowsMs, int keys)
{
	const auto at = [](const std::vector<int> &v, size_t i) { return i < v.size() ? v[i] : 0; };
	std::string s = "  working set: last frame " + std::to_string(at(now, 0)) + " (peak " + std::to_string(at(peaks, 0)) + ")";
	for (size_t w = 0; w < windowsMs.size(); w++)
	{
		const long long ms = (long long)windowsMs[w];
		const std::string span = ms % 1000 == 0 ? std::to_string(ms / 1000) + " s" : std::to_string(ms) + " ms";
		s += ", last " + span + " " + std::to_string(at(now, w + 1)) + " (peak " + std::to_string(at(peaks, w + 1)) + ")";
	}
	return s + "; " + std::to_string(keys) + " keys since the level began";
}

}} // namespace zx::hwrender
