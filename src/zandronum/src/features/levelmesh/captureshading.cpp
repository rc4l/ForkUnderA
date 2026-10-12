// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/captureshading.h"

#include "templates.h"   // [rc4l] gl_lightdata.h's inline gl_ClampLight uses clamp<>
#include "gl/renderer/gl_colormap.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "c_dispatch.h"
#include "c_console.h"
#include <string.h>

EXTERN_CVAR(Int, gl_fogmode)

// [rc4l] Compare every captured surface's light model against GL's own setters, the oracle, counting what differs for fua_shading_report.
CVAR(Bool, fua_shading_check, false, 0)

namespace zx { namespace levelmesh {

static int g_checked = 0, g_differed = 0;
static FString g_firstDiffs;

static bool SameBits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

// [rc4l] What GL's setters leave in the render state for this surface, put back afterwards.
static void GLShading(int lightlevel, int rellight, const FColormap &cm, bool noFog, bool additive, MeshPiece &out)
{
	const FRenderState::ShadingSnapshot before = gl_RenderState.SaveShading();
	FColormap local = cm;
	gl_SetColor(lightlevel, rellight, local, 1.0f);
	if (noFog) gl_SetFog(255, 0, NULL, false);
	else       gl_SetFog(lightlevel, rellight, &local, additive);
	gl_RenderState.GetColorRGB(out.colorR, out.colorG, out.colorB);
	const float *lp = gl_RenderState.GetLightParms();
	out.softLight    = lp[3];
	out.fogDensity   = lp[2];
	out.fogColor     = gl_RenderState.GetFogColor().d & 0xffffff;
	out.desaturation = gl_RenderState.GetDesaturation();
	if (!gl_RenderState.IsFogEnabled()) out.fogMode = 0;
	else out.fogMode = (out.fogColor == 0) ? (int)gl_fogmode : -(int)gl_fogmode;
	gl_RenderState.RestoreShading(before);
}

void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog, bool additive)
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
	ShadePiece(in, gl_ShadingEnv(), out);

	if (!fua_shading_check) return;
	MeshPiece gl = out;
	GLShading(lightlevel, rellight, cm, noFog, additive, gl);
	g_checked++;
	if (SameBits(gl.colorR, out.colorR) && SameBits(gl.colorG, out.colorG) && SameBits(gl.colorB, out.colorB) &&
	    SameBits(gl.softLight, out.softLight) && SameBits(gl.fogDensity, out.fogDensity) &&
	    gl.fogColor == out.fogColor && gl.desaturation == out.desaturation && gl.fogMode == out.fogMode) return;
	if (g_differed++ < 8)
		g_firstDiffs.AppendFormat("  light %d rel %d lc %08x fc %08x bf %d nofog %d add %d: gl (%g %g %g s%g d%g c%06x m%d ds%d) model (%g %g %g s%g d%g c%06x m%d ds%d)\n",
			lightlevel, rellight, cm.LightColor.d, cm.FadeColor.d, cm.blendfactor, noFog, additive,
			gl.colorR, gl.colorG, gl.colorB, gl.softLight, gl.fogDensity, gl.fogColor, gl.fogMode, gl.desaturation,
			out.colorR, out.colorG, out.colorB, out.softLight, out.fogDensity, out.fogColor, out.fogMode, out.desaturation);
}

}} // namespace zx::levelmesh

CCMD( fua_shading_report )
{
	Printf( "light model: %d surfaces checked against GL, %d differed\n%s", zx::levelmesh::g_checked,
		zx::levelmesh::g_differed, zx::levelmesh::g_firstDiffs.GetChars( ) );
	zx::levelmesh::g_checked = zx::levelmesh::g_differed = 0;
	zx::levelmesh::g_firstDiffs = "";
}
