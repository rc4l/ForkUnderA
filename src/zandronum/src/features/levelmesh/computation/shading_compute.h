// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's light model as pure functions: what gl_SetColor and gl_SetFog leave in the render state for one surface.

#ifndef ZX_SHADING_COMPUTE_H
#define ZX_SHADING_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] The level and view settings GL's light model reads besides the surface, gathered by the caller.
struct ShadingEnv
{
	int      lightMode;          // [rc4l] glset.lightmode
	int      ambient;            // [rc4l] r_light_ambient, after the server's forced defaults
	int      fogMode;            // [rc4l] r_fogmode, after the server's forced defaults
	int      distFog;            // [rc4l] r_distfog, after the server's forced defaults
	bool     fixedColormap;      // [rc4l] a fixed colormap is on, as invulnerability and light amplification set
	bool     fadeTable;          // [rc4l] the level has a fade table (LEVEL_HASFADETABLE)
	bool     inSkybox;           // [rc4l] drawing inside a skybox, where fog is half as dense again
	bool     brightFog;          // [rc4l] glset.brightfog
	int      fogDensity;         // [rc4l] the level's fog density, halved as gl_SetFogParams keeps it
	int      outsideFogDensity;  // [rc4l] the outside fog's density, halved likewise
	unsigned outsideFogColor;    // [rc4l] the outside fog's PalEntry::d, alpha included
};

// [rc4l] One surface's light and colormap, as GL's walk hands them to gl_SetColor and gl_SetFog.
struct ShadingInput
{
	int      lightLevel;
	int      relLight;
	unsigned lightColor;     // [rc4l] FColormap::LightColor
	unsigned fadeColor;      // [rc4l] FColormap::FadeColor
	int      blendFactor;
	int      desaturation;
	bool     noFog;          // [rc4l] gl_SetFog given no colormap, as a mirror surface is
	bool     additive;       // [rc4l] fogged to black, as gl_SetFog fogs an added-on surface
};

// [rc4l] What the shader reads for the surface: vColor, uLightLevel, uFogDensity, uFogColor, uFogEnabled and the desaturation.
struct Shading
{
	float    r, g, b;
	float    softLight;      // [rc4l] 0..1 in software lighting (light mode 8), else -1
	float    fogDensity;     // [rc4l] scaled by -log2(e)/64000, ready for exp2()
	unsigned fogColor;       // [rc4l] packed rgb
	int      fogMode;        // [rc4l] 0 off, the fog mode for black fog, its negative for coloured fog
	int      desaturation;
};

// [rc4l] gl_CalcLightLevel: a sector light after the light mode's curve, the ambient floor and the relative light.
int ComputeLightLevel(int lightLevel, int relLight, bool weapon, const ShadingEnv &env);

// [rc4l] gl_CalcLightColor: the light colour scaled by the light level, or mixed by the blend factor, as bytes.
void ComputeLightColor(int light, unsigned color, int blendFactor, int lightMode, int &r, int &g, int &b);

// [rc4l] One entry of r_distfog's black-fog table, the software-light table when softTable is set.
float ComputeDistFog(int distFog, bool softTable, int light);

// [rc4l] gl_GetFogDensity: the fog density a light level and fog colour get under the level's fog rules.
float ComputeFogDensity(int lightLevel, unsigned fogColor, const ShadingEnv &env);

// [rc4l] gl_SetColor then gl_SetFog for one surface, as the render state holds them afterwards.
Shading ComputeShading(const ShadingInput &in, const ShadingEnv &env);

}} // namespace zx::levelmesh

#endif // ZX_SHADING_COMPUTE_H
