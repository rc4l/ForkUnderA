// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/worldpipelines_compute.h"

namespace zx { namespace hwrender {

WorldPipelineDesc ComputeWorldPipelineDesc(int pipeline)
{
	static const WorldPipelineDesc kDescs[kWorldPipelines] = {
		{ "fua scene PSO opaque",               kWpStageOpaque,   kWpBlendNone,  true,  false },
		{ "fua scene PSO masked",               kWpStageMasked,   kWpBlendNone,  true,  false },
		{ "fua scene PSO translucent",          kWpStageTrans,    kWpBlendAlpha, true,  false },
		{ "fua scene PSO additive",             kWpStageTrans,    kWpBlendAdd,   true,  false },
		{ "fua sprite PSO masked",              kWpStageMasked,   kWpBlendNone,  false, false },
		{ "fua sprite PSO translucent",         kWpStageTrans,    kWpBlendAlpha, false, false },
		{ "fua sprite PSO additive",            kWpStageTrans,    kWpBlendAdd,   false, false },
		{ "fua decal PSO masked",               kWpStageMasked,   kWpBlendNone,  false, true  },
		{ "fua decal PSO translucent",          kWpStageTrans,    kWpBlendAlpha, false, true  },
		{ "fua decal PSO additive",             kWpStageTrans,    kWpBlendAdd,   false, true  },
		{ "fua decal PSO redalpha translucent", kWpStageRedAlpha, kWpBlendAlpha, false, true  },
		{ "fua decal PSO redalpha additive",    kWpStageRedAlpha, kWpBlendAdd,   false, true  },
		{ "fua scene PSO masked + gbuffer",     kWpStageGBuffer,  kWpBlendNone,  true,  false },
		{ "fua sprite PSO reverse subtract",    kWpStageTrans,    kWpBlendRevSub, false, false },
		{ "fua sprite PSO subtract",            kWpStageTrans,    kWpBlendSub,   false, false },
		{ "fua sprite PSO fuzz",                kWpStageTrans,    kWpBlendFuzz,  false, false },
		{ "fua decal PSO fuzz",                 kWpStageTrans,    kWpBlendFuzz,  false, true  },
		{ "fua HUD model PSO translucent",      kWpStageTrans,    kWpBlendAlpha, false, false, true },
		{ "fua HUD model PSO additive",         kWpStageTrans,    kWpBlendAdd,   false, false, true },
		{ "fua HUD model PSO fuzz",             kWpStageTrans,    kWpBlendFuzz,  false, false, true },
		{ "fua HUD model PSO reverse subtract", kWpStageTrans,    kWpBlendRevSub, false, false, true },
		{ "fua HUD model PSO subtract",         kWpStageTrans,    kWpBlendSub,   false, false, true },
	};
	return kDescs[(pipeline >= 0 && pipeline < kWorldPipelines) ? pipeline : kWpOpaque];
}

int ComputeBatchPipeline(int blendMode)
{
	if (blendMode == 0) return kWpMasked;
	return (blendMode == 2) ? kWpAdd : kWpTrans;
}

int ComputeDynamicPipeline(int blendMode, bool redAlpha, bool depthBias, bool hud)
{
	const bool add = blendMode == 2;
	if (redAlpha) return add ? kWpAddRedAlpha : kWpTransRedAlpha;
	if (blendMode == 3 && depthBias) return kWpFuzzDecal;
	if (depthBias) return add ? kWpAddDecal : kWpTransDecal;
	// [rc4l] Sprites and models by blend mode 0 to 5, in the world and on the HUD.
	static const int kByBlend[6][2] = {
		{ kWpTransNoCull, kWpTransHud }, { kWpTransNoCull, kWpTransHud }, { kWpAddNoCull, kWpAddHud },
		{ kWpFuzzNoCull, kWpFuzzHud }, { kWpRevSubNoCull, kWpRevSubHud }, { kWpSubNoCull, kWpSubHud },
	};
	return kByBlend[(blendMode >= 0 && blendMode < 6) ? blendMode : 1][hud ? 1 : 0];
}

}} // namespace zx::hwrender
