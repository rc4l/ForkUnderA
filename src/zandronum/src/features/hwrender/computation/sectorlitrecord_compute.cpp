// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/sectorlitrecord_compute.h"
#include "features/hwrender/computation/desaturate_compute.h"
#include "features/hwrender/computation/texflags_compute.h"
#include "features/levelmesh/computation/packedcolor_compute.h"

namespace zx { namespace hwrender {

void ComputeSectorLitRecord(ScenePieceData &pd, const zx::levelmesh::SectorLightRef &r, bool noFog)
{
	pd.r = (float)r.sector;
	pd.g = (float)r.kind;
	pd.b = (float)r.sideLight;
	pd.softLight = (float)r.contrast;
	pd.fogR = r.sideAbsolute ? 1.f : 0.f;
	pd.fogG = r.sideLightFog ? 1.f : 0.f;
	pd.fogB = noFog ? 1.f : 0.f;
	pd.fogDensity = 0.f;
	pd.fogMode = 0.f;
	pd.desaturation = 0.f;
	pd.texFlags = (float)((unsigned)pd.texFlags | kTexFlagSectorLit);
}

void ComputeSectorLitShade(ScenePieceData &pd, const zx::levelmesh::SectorLightEntry &e, zx::levelmesh::ShadingEnv env, int extraLight)
{
	zx::levelmesh::SectorLightRef r;
	r.sector = (int)pd.r;
	r.kind = (int)pd.g;
	r.sideLight = (int)pd.b;
	r.contrast = (int)pd.softLight;
	r.sideAbsolute = pd.fogR > 0.5f;
	r.sideLightFog = pd.fogG > 0.5f;
	const zx::levelmesh::SectorLitInput lit = zx::levelmesh::ComputeSectorLitInput(e, r, env.fadeTable);
	zx::levelmesh::ShadingInput in;
	in.lightLevel = lit.lightLevel;
	in.relLight = lit.relLight + extraLight;
	in.lightColor = lit.lightColor;
	in.fadeColor = lit.fadeColor;
	in.blendFactor = lit.blendFactor;
	in.desaturation = lit.desaturation;
	in.noFog = pd.fogB > 0.5f;
	in.additive = false;
	// [rc4l] A level surface is lit outside the fixed colormap, which the shader applies itself (CaptureLevelShading).
	env.fixedColormap = false;
	const zx::levelmesh::Shading s = zx::levelmesh::ComputeShading(in, env);
	pd.r = s.r; pd.g = s.g; pd.b = s.b;
	pd.softLight = s.softLight;
	pd.fogDensity = s.fogDensity;
	pd.fogMode = (float)s.fogMode;
	zx::levelmesh::ComputeUnpackRGB(s.fogColor, pd.fogR, pd.fogG, pd.fogB);
	pd.desaturation = ComputeDesaturationFactor(s.desaturation);
}

unsigned ComputeSectorLightCapacity(int sectors)
{
	const unsigned n = sectors > 0 ? (unsigned)sectors : 0u;
	const unsigned grown = n + n / 4;
	return grown > 1024u ? grown : 1024u;
}

}} // namespace zx::hwrender
