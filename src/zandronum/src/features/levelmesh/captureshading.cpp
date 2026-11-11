// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/captureshading.h"
#include "features/frame/computation/oracle_compute.h"   // [rc4l] OracleTally

#include "features/rendersettings/rendersettings.h"   // [rc4l] FColormap and RenderShadingEnv
#include "features/levelmesh/computation/shading_compute.h"
#include "r_defs.h"
#include "r_state.h"   // [rc4l] sectors, numsectors
#include "c_dispatch.h"
#include "c_console.h"
#include <string.h>


// [rc4l] Compare every captured surface's light model against GL's own setters, the oracle, counting what differs for fua_shading_report.
CVAR(Bool, fua_shading_check, false, CVAR_FUA_DEV_MODE)

namespace zx { namespace levelmesh {

static zx::frame::OracleTally g_check;

static bool SameBits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// [rc4l] What GL's setters leave in the render state for this surface, put back afterwards.
static ShadingOracle g_oracle = NULL;
void SetShadingOracle(ShadingOracle oracle) { g_oracle = oracle; }

// [Doruk-Aksoy] A surface's shading input from its light, the light its fog takes, relative light and colormap.
static ShadingInput InputOf(int lightlevel, int foglevel, int rellight, const FColormap &cm, bool noFog, bool additive)
{
	ShadingInput in;
	in.lightLevel = lightlevel;
	in.fogLevel = foglevel;
	in.relLight = rellight;
	in.lightColor = cm.LightColor.d;
	in.fadeColor = cm.FadeColor.d;
	in.blendFactor = cm.blendfactor;
	in.desaturation = cm.desaturation;
	in.noFog = noFog;
	in.additive = additive;
	return in;
}

void CapturePolyColor(int lightlevel, const FColormap &cm, float &r, float &g, float &b)
{
	ShadingEnv env = RenderShadingEnv(false);
	if (env.lightMode == 8) env.lightMode = 0;
	const ShadingInput in = InputOf(lightlevel, lightlevel, 0, cm, true, false);
	const Shading s = ComputeShading(in, env);
	r = s.r;
	g = s.g;
	b = s.b;
}

// [rc4l] Shade `out` in the frame's light model, the fixed colormap left out for a level surface.
static void Capture(int lightlevel, int foglevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, bool additive, bool levelSurface)
{
	const ShadingInput in = InputOf(lightlevel, foglevel, rellight, cm, noFog, additive);
	ShadingEnv env = RenderShadingEnv(false);
	if (levelSurface) env.fixedColormap = false;
	ShadePiece(in, env, out);

	// [rc4l] GL's setters light a level surface under the fixed colormap, which this one leaves to the shader.
	if (!fua_shading_check || g_oracle == NULL || (levelSurface && gl_fixedcolormap != CM_DEFAULT)) return;
	MeshPiece gl = out;
	g_oracle(lightlevel, foglevel, rellight, cm, noFog, additive, gl);
	g_check.checked++;
	if (SameBits(gl.colorR, out.colorR) && SameBits(gl.colorG, out.colorG) && SameBits(gl.colorB, out.colorB) &&
	    SameBits(gl.softLight, out.softLight) && SameBits(gl.fogDensity, out.fogDensity) &&
	    gl.fogColor == out.fogColor && gl.desaturation == out.desaturation && gl.fogMode == out.fogMode) return;
	if (zx::frame::ComputeOracleDiffer(g_check, 8))
		zx::frame::ComputeOracleNote(g_check, "  light %d fog %d rel %d lc %08x fc %08x bf %d nofog %d add %d: gl (%g %g %g s%g d%g c%06x m%d ds%d) model (%g %g %g s%g d%g c%06x m%d ds%d)\n",
			lightlevel, foglevel, rellight, cm.LightColor.d, cm.FadeColor.d, cm.blendfactor, noFog, additive,
			gl.colorR, gl.colorG, gl.colorB, gl.softLight, gl.fogDensity, gl.fogColor, gl.fogMode, gl.desaturation,
			out.colorR, out.colorG, out.colorB, out.softLight, out.fogDensity, out.fogColor, out.fogMode, out.desaturation);
}

void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, bool additive)
{
	Capture(lightlevel, lightlevel, rellight, cm, out, noFog, additive, false);
}

// [Doruk-Aksoy] The run's environment once taken, and the last input it shaded with what came of it.
static struct { bool envTaken, kept; ShadingEnv env; ShadingInput in; Shading shading; } g_run;

void StartShadingRun()
{
	g_run.envTaken = g_run.kept = false;
}

void CaptureRunShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out)
{
	// [Doruk-Aksoy] The oracle checks each piece it is asked about, so with it on every piece is captured alone.
	if (fua_shading_check) { CaptureShading(lightlevel, rellight, cm, out); return; }
	const ShadingInput in = InputOf(lightlevel, lightlevel, rellight, cm, false, false);
	if (!g_run.envTaken) { g_run.env = RenderShadingEnv(false); g_run.envTaken = true; }
	if (!g_run.kept || !ComputeSameShadingInput(g_run.in, in))
	{
		g_run.shading = ComputeShading(in, g_run.env);
		g_run.in = in;
		g_run.kept = true;
	}
	ApplyShading(g_run.shading, out);
}

void CaptureSpriteShading(int lightlevel, int foglevel, int rellight, const FColormap &cm, const zx::sprites::SpriteDrawLook &look,
                          unsigned thingColor, MeshPiece &out)
{
	Capture(lightlevel, foglevel, rellight, cm, out, false, look.additiveFog, false);
	if (look.shadow) out.colorR = out.colorG = out.colorB = zx::sprites::kShadowColor;
	const PalEntry color(thingColor);
	out.colorR *= color.r / 255.f;
	out.colorG *= color.g / 255.f;
	out.colorB *= color.b / 255.f;
	out.alpha = look.alpha;
	// [rc4l] GL draws the subtraction itself with fog off, and lays the fog over it in a second pass.
	if (look.fogLayer) { out.fogMode = 0; out.fogDensity = 0.f; }
}

void CaptureFogLayer(int lightlevel, int foglevel, int rellight, const FColormap &cm, MeshPiece &out)
{
	MeshPiece fogged = out;
	Capture(lightlevel, foglevel, rellight, cm, fogged, false, false, false);
	out.fogMode = fogged.fogMode;
	out.fogDensity = fogged.fogDensity;
	out.fogColor = fogged.fogColor;
	out.softLight = fogged.softLight;
	out.blendMode = zx::sprites::kBlendTranslucent;
	out.alphaTest = false;
	out.fogPass = true;
}

void CaptureLevelShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog)
{
	Capture(lightlevel, lightlevel, rellight, cm, out, noFog, false, true);
}

static unsigned int g_shadeGeneration = 0;
unsigned int LevelShadeGeneration() { return g_shadeGeneration; }
void NoteLevelShadeChanged() { g_shadeGeneration++; }

SectorLightEntry SectorLightEntryOf(int sector)
{
	SectorLightEntry e;
	if (sector < 0 || sector >= numsectors) return e;
	const sector_t &s = sectors[sector];
	FColormap cm;
	cm = s.rc.ColorMap;
	e.lightLevel = s.rc.lightlevel;
	e.floorLight = gl_ClampLight(s.rc.GetFloorLight());
	e.ceilingLight = gl_ClampLight(s.rc.GetCeilingLight());
	e.lightColor = cm.LightColor.d;
	e.fadeColor = cm.FadeColor.d;
	e.blendFactor = cm.blendfactor;
	e.desaturation = cm.desaturation;
	return e;
}

void CaptureLevelShadingFrom(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, const SectorLightRef &guess)
{
	Capture(lightlevel, lightlevel, rellight, cm, out, noFog, false, true);
	if (guess.sector < 0) return;
	const SectorLitInput in = ComputeSectorLitInput(SectorLightEntryOf(guess.sector), guess, RenderShadingEnv(false).fadeTable);
	if (!ComputeSectorLitReproduces(in, lightlevel, rellight - getExtraLight(), cm.LightColor.d, cm.FadeColor.d, cm.blendfactor, cm.desaturation)) return;
	out.sectorLight = guess;
	out.sectorLitNoFog = noFog;
}

}} // namespace zx::levelmesh

DEV_MODE_CCMD( fua_shading_report )
{
	const zx::frame::OracleTally &t = zx::levelmesh::g_check;
	Printf( "light model: %d surfaces checked against GL, %d differed\n%s", t.checked, t.differed, t.firsts.c_str( ) );
	zx::frame::ComputeOracleReset( zx::levelmesh::g_check );
}
