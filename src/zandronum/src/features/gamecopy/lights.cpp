// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/lights.h"
#include "features/gamecopy/computation/serial_compute.h"
#include "features/hwrender/computation/lighttouch_compute.h"   // [rc4l] ComputeLightLinksStale, ComputeSortedTouches
#include "features/dynlights/dynlight.h"
#include "features/telemetry/telemetry.h"

#include "r_defs.h"
#include "r_state.h"

#include <unordered_map>

namespace zx { namespace gamecopy {

namespace {

std::vector<LightRecord> g_lights;
std::vector<int> g_subFirst, g_subCount;   // [rc4l] each subsector's run in g_subLights
std::vector<int> g_subLights;
std::unordered_map<const ADynamicLight *, int> g_indexOf;   // [rc4l] each live light's place in g_lights, this fill only

// [rc4l] The ids a light is linked to, as PieceStackId names surfaces.
void CollectLinks(ADynamicLight *light, std::vector<unsigned> &out)
{
	out.clear();
	for (FLightNode *n = light->touching_subsectors; n != NULL; n = n->nextTarget)
		if (n->targSubsector != NULL) out.push_back((unsigned)(n->targSubsector - subsectors));
	for (FLightNode *n = light->touching_sides; n != NULL; n = n->nextTarget)
		if (n->targLine != NULL) out.push_back((unsigned)(numsubsectors + (n->targLine - sides)));
	zx::hwrender::ComputeSortedTouches(out);
}

} // namespace

const LightRecord *Lights(int &count)
{
	count = (int)g_lights.size();
	return g_lights.empty() ? NULL : &g_lights[0];
}

const int *SubsectorLights(int subsector, int &count)
{
	count = subsector >= 0 && subsector < (int)g_subCount.size() ? g_subCount[subsector] : 0;
	return count > 0 ? &g_subLights[g_subFirst[subsector]] : NULL;
}

void InitLights()
{
	g_lights.clear();
	g_indexOf.clear();
	g_subFirst.clear();
	g_subCount.clear();
	g_subLights.clear();
}

void FillLights()
{
	ZX_ZONE("copy/light_fill");   // [rc4l] the light records and subsector light lists taken
	size_t n = 0;
	g_indexOf.clear();
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	for (ADynamicLight *light; (light = it.Next()) != NULL; n++)
	{
		if (n >= g_lights.size()) g_lights.emplace_back();
		LightRecord &r = g_lights[n];
		// [rc4l] A light's id is its actor's, given by the actor fill before this one.
		const zx::hwrender::LightLinkKey kept = { r.id, r.x.Raw(), r.y.Raw(), r.z.Raw(), FIXED2FLOAT(r.linkRadius) };
		const zx::hwrender::LightLinkKey now = { light->rcSerial, light->x.Raw(), light->y.Raw(), light->z.Raw(), FIXED2FLOAT(light->radius) };
		if (kept.light == kNoId || zx::hwrender::ComputeLightLinksStale(kept, now)) CollectLinks(light, r.links);
		r.id = light->rcSerial;
		r.target = light->target;
		r.x = light->x; r.y = light->y; r.z = light->z;
		r.linkRadius = light->radius;
		r.radius = light->GetRadius();
		r.red = light->GetRed(); r.green = light->GetGreen(); r.blue = light->GetBlue();
		r.active = light->IsActive();
		r.subtractive = light->IsSubtractive();
		r.additive = light->IsAdditive();
		r.dontLightSelf = (light->flags4 & MF4_DONTLIGHTSELF) != 0;
		g_indexOf[light] = (int)n;
	}
	g_lights.resize(n);
	// [rc4l] Each subsector's lights in its light list's order, the order GL sums a sprite's light in.
	g_subFirst.assign(numsubsectors, 0);
	g_subCount.assign(numsubsectors, 0);
	g_subLights.clear();
	for (int s = 0; s < numsubsectors; s++)
	{
		g_subFirst[s] = (int)g_subLights.size();
		for (FLightNode *node = subsectors[s].lighthead; node != NULL; node = node->nextLight)
		{
			const auto i = g_indexOf.find(node->lightsource);
			if (i != g_indexOf.end()) g_subLights.push_back(i->second);
		}
		g_subCount[s] = (int)g_subLights.size() - g_subFirst[s];
	}
	ZX_COUNT("copy/lights", "lights", n);
}

}} // namespace zx::gamecopy
