// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/captureshading.h"
#include "features/frame/computation/oracle_compute.h"   // [rc4l] OracleTally

#include "features/rendersettings/rendersettings.h"   // [rc4l] FColormap and RenderShadingEnv
#include "features/levelmesh/computation/shading_compute.h"
#include "c_dispatch.h"
#include "c_console.h"
#include <string.h>


// [rc4l] Compare every captured surface's light model against GL's own setters, the oracle, counting what differs for fua_shading_report.
CVAR(Bool, fua_shading_check, false, 0)

namespace zx { namespace levelmesh {

static zx::frame::OracleTally g_check;

static bool SameBits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// [rc4l] What GL's setters leave in the render state for this surface, put back afterwards.
static ShadingOracle g_oracle = NULL;
void SetShadingOracle(ShadingOracle oracle) { g_oracle = oracle; }

void CapturePolyColor(int lightlevel, const FColormap &cm, float &r, float &g, float &b)
{
	ShadingEnv env = RenderShadingEnv(false);
	if (env.lightMode == 8) env.lightMode = 0;
	ShadingInput in;
	in.lightLevel = lightlevel;
	in.relLight = 0;
	in.lightColor = cm.LightColor.d;
	in.fadeColor = cm.FadeColor.d;
	in.blendFactor = cm.blendfactor;
	in.desaturation = cm.desaturation;
	in.noFog = true;
	in.additive = false;
	const Shading s = ComputeShading(in, env);
	r = s.r;
	g = s.g;
	b = s.b;
}

// [rc4l] Shade `out` in the frame's light model, the fixed colormap left out for a level surface.
static void Capture(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, bool additive, bool levelSurface)
{
	ShadingInput in;
	in.lightLevel = lightlevel;
	in.relLight = rellight;
	in.lightColor = cm.LightColor.d;
	in.fadeColor = cm.FadeColor.d;
	in.blendFactor = cm.blendfactor;
	in.desaturation = cm.desaturation;
	in.noFog = noFog;
	in.additive = additive;
	ShadingEnv env = RenderShadingEnv(false);
	if (levelSurface) env.fixedColormap = false;
	ShadePiece(in, env, out);

	// [rc4l] GL's setters light a level surface under the fixed colormap, which this one leaves to the shader.
	if (!fua_shading_check || g_oracle == NULL || (levelSurface && gl_fixedcolormap != CM_DEFAULT)) return;
	MeshPiece gl = out;
	g_oracle(lightlevel, rellight, cm, noFog, additive, gl);
	g_check.checked++;
	if (SameBits(gl.colorR, out.colorR) && SameBits(gl.colorG, out.colorG) && SameBits(gl.colorB, out.colorB) &&
	    SameBits(gl.softLight, out.softLight) && SameBits(gl.fogDensity, out.fogDensity) &&
	    gl.fogColor == out.fogColor && gl.desaturation == out.desaturation && gl.fogMode == out.fogMode) return;
	if (zx::frame::ComputeOracleDiffer(g_check, 8))
		zx::frame::ComputeOracleNote(g_check, "  light %d rel %d lc %08x fc %08x bf %d nofog %d add %d: gl (%g %g %g s%g d%g c%06x m%d ds%d) model (%g %g %g s%g d%g c%06x m%d ds%d)\n",
			lightlevel, rellight, cm.LightColor.d, cm.FadeColor.d, cm.blendfactor, noFog, additive,
			gl.colorR, gl.colorG, gl.colorB, gl.softLight, gl.fogDensity, gl.fogColor, gl.fogMode, gl.desaturation,
			out.colorR, out.colorG, out.colorB, out.softLight, out.fogDensity, out.fogColor, out.fogMode, out.desaturation);
}

void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, bool additive)
{
	Capture(lightlevel, rellight, cm, out, noFog, additive, false);
}

void CaptureSpriteShading(int lightlevel, int rellight, const FColormap &cm, const zx::sprites::SpriteDrawLook &look,
                          unsigned thingColor, MeshPiece &out)
{
	Capture(lightlevel, rellight, cm, out, false, look.additiveFog, false);
	if (look.shadow) out.colorR = out.colorG = out.colorB = zx::sprites::kShadowColor;
	const PalEntry color(thingColor);
	out.colorR *= color.r / 255.f;
	out.colorG *= color.g / 255.f;
	out.colorB *= color.b / 255.f;
	out.alpha = look.alpha;
}

void CaptureLevelShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog)
{
	Capture(lightlevel, rellight, cm, out, noFog, false, true);
}

}} // namespace zx::levelmesh

CCMD( fua_shading_report )
{
	const zx::frame::OracleTally &t = zx::levelmesh::g_check;
	Printf( "light model: %d surfaces checked against GL, %d differed\n%s", t.checked, t.differed, t.firsts.c_str( ) );
	zx::frame::ComputeOracleReset( zx::levelmesh::g_check );
}
