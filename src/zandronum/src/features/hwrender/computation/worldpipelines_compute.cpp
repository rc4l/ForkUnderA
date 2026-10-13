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
	};
	return kDescs[(pipeline >= 0 && pipeline < kWorldPipelines) ? pipeline : kWpOpaque];
}

int ComputeBatchPipeline(int blendMode)
{
	if (blendMode == 0) return kWpMasked;
	return (blendMode == 2) ? kWpAdd : kWpTrans;
}

int ComputeDynamicPipeline(int blendMode, bool redAlpha, bool depthBias)
{
	const bool add = blendMode == 2;
	if (redAlpha) return add ? kWpAddRedAlpha : kWpTransRedAlpha;
	if (depthBias) return add ? kWpAddDecal : kWpTransDecal;
	if (blendMode == 4) return kWpRevSubNoCull;
	if (blendMode == 5) return kWpSubNoCull;
	return add ? kWpAddNoCull : kWpTransNoCull;
}

}} // namespace zx::hwrender
