// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/projdecals.h"
#include "features/levelmesh/computation/decalproject_compute.h"

#include "r_defs.h"
#include "p_local.h"                 // P_PointInSector, for the plane a mark rides
#include "r_state.h"                 // sectors
#include "a_sharedglobal.h"          // DBaseDecal
#include "decallib.h"
#include "doomstat.h"                // gametic
#include "m_random.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "tarray.h"
#include "gl/textures/gl_material.h"

#include <math.h>

// [rc4l] Which of the two ways a mark can be drawn is in use. DEFAULT 0: the glued quad.
//
//   0  the glued quad Doom has always drawn, captured into the mesh as four vertices. GL draws this
//      shape too, so it is the only mode where the two renderers agree exactly -- and the only one
//      where a mark is the size and shape the artist authored.
//   1  drawn as the mark's own BOX and resolved per fragment against the depth and normal the world
//      already wrote. No geometry is made for a mark at all.
//
// Mode 1 is OFF by default because a box projection stretches its picture by 1/cos on anything it
// does not meet square-on, and nothing removes that -- the levers only move it around. On a big
// blast mark spanning a corner the trade can be worth it; on a bullet chip or a plasma scorch, a few
// units across and never spanning anything, the stretch is the only thing the projection adds. The
// common case is small marks, so the common case decides the default.
//
// There was a third mode for a while, which cut the geometry inside the box into triangles on the
// CPU. It is gone: a triangle is the smallest thing that can carry an alpha, so every fade it did
// was in slices and every join between two surfaces was two constants meeting.
CVAR(Int, fua_decalmode, 0, CVAR_ARCHIVE)
// [rc4l] Marks on floors and ceilings, which Doom itself never makes.
//
// Independent of fua_decalmode on purpose. Projection on a WALL is a trade: it replaces a quad
// that was already right, buying continuity round a corner at the cost of stretching a mark at a
// grazing angle -- which is why walls went back to the quad. On a FLOOR there is no trade to lose.
// The alternative there is no mark at all, and the projection is at its best on one: aimed
// straight down the surface normal, where nothing is grazing it.
CVAR(Bool, fua_decal_flats, true, CVAR_ARCHIVE)


// [rc4l] The most oblique a mark is allowed to be, as the cosine of the angle off head-on.
//
// This is the stretch knob, and the cheapest one: a planar projection lands its picture on the
// surface stretched by 1/cos, so capping the tilt caps the stretch -- 0.7 is 45 degrees and 1.41x,
// where the old 0.35 was 70 degrees and 2.9x, which is a visible smear. It costs nothing else,
// because the projection stays a single plane and so stays continuous across a corner.
//
// 1.0 removes the tilt altogether: every mark lands square-on, perfectly round, and the direction
// the shot came from stops showing in the picture.
CVAR(Float, fua_projdecal_maxskew, 0.7f, CVAR_ARCHIVE)

// [rc4l] How far the box reaches BEHIND the contact point, as a fraction of the picture's size.
//
// This is the reach that lets a mark carry onto the floor in front of a wall, or round a corner,
// when the hit was square-on and the projection's own slant demands nothing of its own. See
// ComputeDecalBoxDepth for the part that is not a free choice.
CVAR(Float, fua_projdecal_depth, 0.6f, CVAR_ARCHIVE)

// [rc4l] Print each mark as it is made.
//
// A mark that comes out wrong cannot be diagnosed from a screenshot: a picture clipped by a plane, a
// picture stretched across a grazing surface, and a picture whose texture simply looks like that are
// the same handful of dark pixels. The numbers say which.
CVAR(Bool, fua_projdecal_debug, false, 0)

EXTERN_CVAR(Int, cl_maxdecals)

// [rc4l] Its own RNG stream, not pr_decal(), because a projection must not change what the engine
// rolls. Sharing one would desync the game against a demo or another client the moment a mark was
// drawn -- the renderer would be consuming the simulation's random numbers.
static FRandom pr_projdecal ("ProjDecal");

namespace zx { namespace levelmesh {

// [rc4l] See projdecals.h: one answer for the four places that ask.
bool ProjectedDecalsActive() { return fua_decalmode != 0 || fua_decal_flats; }


namespace {

// [rc4l] What Doom knew about the projectile, carried from where it was still true.
struct ImpactContext
{
	bool  valid;
	float vel[3];     // map units per tic, at the moment of impact
	float radius;     // the projectile's own radius: how far short of the surface it stopped
};

ImpactContext g_impact = { false, { 0.f, 0.f, 0.f }, 0.f };

struct ProjDecal
{
	// [rc4l] The engine's decal, when there is one. It owns the fade, the lifetime and the
	// cl_maxdecals recycling, and the alpha is read off it every frame rather than reproduced.
	//
	// NULL for a mark the engine never made -- one on a floor, where Doom decals nothing. Those fade
	// themselves, from the fader's own timing read once at spawn, which is the only case where the
	// curve has to be repeated here at all.
	DBaseDecal  *owner;
	int          spawnTic;
	int          fadeStart;   // tics of full alpha; -1 when this never fades
	int          fadeTime;
	float        baseAlpha;
	float        currentAlpha;

	DecalBox     box;
	FTextureID   pic;
	int          translation;
	unsigned int alphaColor;
	bool         redToAlpha;
	bool         additive;
	bool         fullbright;

	// [rc4l] See GpuDecal: the plane this mark rides, and its height above it.
	int          anchorSector;
	int          anchorPlane;
	float        anchorOffset;
};

TArray<ProjDecal> g_decals;
TArray<GpuDecal>  g_gpuDecals;

// [rc4l] How a mark looks, gathered from wherever it came from.
//
// The engine's decal and a bare template describe the same thing in different words, and the
// projection needs it in one. Filling this in is the only difference between the two entry points.
struct MarkStyle
{
	FTextureID   pic;
	fixed_t      scaleX, scaleY;
	float        alpha;
	unsigned int alphaColor;
	int          translation;
	FRenderStyle style;
	DWORD        renderFlags;
};

inline float Dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

// [rc4l] The face of the line that was hit, pointing at the side the projectile came from.
//
// Which side matters: a mark is only visible from the side it was made on, and the basis uses this
// to decide where to face when the projectile had no usable direction of its own.
void NormalFromLine(line_t *line, float out[3])
{
	out[0] = 0.f; out[1] = 0.f; out[2] = 1.f;
	if (line == NULL) return;
	const float dx = FIXED2FLOAT(line->dx), dy = FIXED2FLOAT(line->dy);
	const float len = sqrtf(dx*dx + dy*dy);
	if (len < 1e-4f) return;
	out[0] = dy / len; out[1] = -dx / len; out[2] = 0.f;
	if (g_impact.valid && Dot3(out, g_impact.vel) > 0.f)
	{
		out[0] = -out[0]; out[1] = -out[1];
	}
}

// [rc4l] Keep a mark, and retire the oldest ownerless one once there are too many.
//
// The ones with an owner are already limited: cl_maxdecals recycles the engine's decals and each
// takes its projection with it. A floor scorch has no owner and nothing to recycle it, so without
// this a long session accumulates them until the level ends. The same cvar sets the limit, because
// it is the same question the player was answering when they set it.
void StoreDecal(const ProjDecal &incoming)
{
	g_decals.Push(incoming);

	if (cl_maxdecals > 0)
	{
		unsigned ownerless = 0;
		for (unsigned i = 0; i < g_decals.Size(); i++) if (g_decals[i].owner == NULL) ownerless++;
		while (ownerless > (unsigned)cl_maxdecals)
		{
			for (unsigned i = 0; i < g_decals.Size(); i++)
			{
				if (g_decals[i].owner == NULL) { g_decals.Delete(i); break; }
			}
			ownerless--;
		}
	}
}

// [rc4l] Work out the mark's box and keep it. The one place a projection is built.
//
// `surfN` is the surface that stopped the projectile, which answers the two questions the direction
// of travel cannot: which way to face when there is no usable velocity, and which way to tilt a
// grazing hit back towards square-on.
//
// `advance` is how far short of the surface the caller's point is. A projectile is an axis-aligned
// BOX in Doom, so a missile stopped by a wall has its centre a radius short of it and the box has to
// be pushed forward to sit on the geometry. A hit on a FLOOR is not like that: the missile comes to
// rest exactly on the plane and the caller passes the plane's own height, so there is nothing to
// close -- and pushing anyway drives the box a full radius THROUGH the floor. A plasma ball has a
// radius of 13 and a scorch 15 units across, so its whole picture ended up underground.
void BuildProjection(const MarkStyle &style, DBaseDecal *owner, int fadeStart, int fadeTime,
                     fixed_t x, fixed_t y, fixed_t z, const float surfN[3], float advance)
{
	FTexture *texture = TexMan[style.pic];
	if (texture == NULL) return;
	FMaterial *mat = FMaterial::ValidateTexture(texture, true);
	if (mat == NULL) return;

	// A missing context is a real case -- a decal placed by a script, a puff with no direction -- and
	// it is spelled "no velocity", which BuildDecalBasis turns into a head-on projection.
	const float noVelocity[3] = { 0.f, 0.f, 0.f };
	float right[3], up[3], axis[3];
	BuildDecalBasis(g_impact.valid ? g_impact.vel : noVelocity, surfN, fua_projdecal_maxskew,
		right, up, axis);

	const float halfW = mat->TextureWidth()  * FIXED2FLOAT(style.scaleX) * 0.5f;
	const float halfH = mat->TextureHeight() * FIXED2FLOAT(style.scaleY) * 0.5f;
	if (halfW < 0.5f || halfH < 0.5f) return;

	// [rc4l] The flips, applied to the AXES rather than to a texture coordinate.
	//
	// Mirroring the picture is exactly negating the axis it is read along, so doing it here means
	// nothing downstream has to know about it. DECALDEF's randomflipx/randomflipy exist so repeated
	// marks do not look stamped; an earlier version implemented them by moving the quad instead,
	// which put a BFG's scorch and its glow side by side because they flipped independently.
	ApplyDecalFlip(!!(style.renderFlags & RF_XFLIP), !!(style.renderFlags & RF_YFLIP), right, up);

	DecalBox box;
	const float pos[3] = { FIXED2FLOAT(x), FIXED2FLOAT(y), FIXED2FLOAT(z) };
	DecalOriginFromImpact(pos, axis, advance, box.origin);
	for (int i = 0; i < 3; i++) { box.right[i] = right[i]; box.up[i] = up[i]; box.axis[i] = axis[i]; }
	box.halfW = halfW;
	box.halfH = halfH;

	// The depth is not a free choice -- see ComputeDecalBoxDepth, where it is stated and tested. It
	// is measured from the picture's CORNER, which reaches sqrt(2) further than its half-width: from
	// the half-width the corners came up a third short and the box sliced them off.
	const float cornerRadius = sqrtf(halfW*halfW + halfH*halfH);
	const float cosTheta = -(axis[0]*surfN[0] + axis[1]*surfN[1] + axis[2]*surfN[2]);
	ComputeDecalBoxDepth(cornerRadius, cosTheta, (float)fua_projdecal_depth, box.near_, box.far_);

	ProjDecal decal;
	decal.owner = owner;
	decal.spawnTic = gametic;
	decal.fadeStart = fadeStart;
	decal.fadeTime = fadeTime;
	decal.baseAlpha = style.alpha;
	decal.currentAlpha = style.alpha;
	decal.box = box;
	decal.pic = style.pic;
	decal.translation = style.translation;
	decal.alphaColor = style.alphaColor;
	decal.redToAlpha = !!(style.style.Flags & STYLEF_RedIsAlpha);
	decal.additive = (style.style.BlendOp == STYLEOP_Add && style.style.DestAlpha == STYLEALPHA_One);
	decal.fullbright = !!(style.renderFlags & RF_FULLBRIGHT);

	if (fua_projdecal_debug)
	{
		Printf("projdecal: half %.1f x %.1f  depth -%.1f..+%.1f  axis (%.2f, %.2f, %.2f)  vel %s\n",
			box.halfW, box.halfH, box.near_, box.far_, axis[0], axis[1], axis[2],
			g_impact.valid ? "yes" : "none");
	}

	// [rc4l] Anchor the mark to the plane it landed on, if it landed on one.
	//
	// Only a FLAT hit has a plane to ride: the normal is vertical, so the surface is the sector's
	// floor or ceiling and which one is the sign of that normal. A wall projection is left
	// unanchored -- there is no single plane under it, and a projection paints whatever its box
	// finds, so the honest anchor is the surface at the point of impact and nothing else.
	{
		decal.anchorSector = -1;
		decal.anchorPlane = 0;
		decal.anchorOffset = 0.f;
		const float upness = surfN[2];
		if (upness > 0.7f || upness < -0.7f)
		{
			sector_t *sec = P_PointInSector(x, y);
			if (sec != NULL)
			{
				const int plane = (upness > 0.f) ? 0 : 1;
				const fixed_t planeZ = (plane == 0)
					? sec->floorplane.ZatPoint(x, y) : sec->ceilingplane.ZatPoint(x, y);
				decal.anchorSector = (int)(sec - sectors);
				decal.anchorPlane = plane;
				decal.anchorOffset = FIXED2FLOAT(z) - FIXED2FLOAT(planeZ);
			}
		}
	}

	StoreDecal(decal);
}

void SpawnFromTemplate(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                       const float surfaceNormal[3], float advance)
{
	// [rc4l] No mode test here: the two callers disagree about which switch applies. A wall mark
	// needs fua_decalmode, a floor mark needs fua_decal_flats, and each asks for itself.
	if (tpl == NULL) return;

	MarkStyle style;
	style.pic = tpl->PicNum;
	style.scaleX = tpl->ScaleX;
	style.scaleY = tpl->ScaleY;
	// The template stores half the alpha, exactly as ApplyToDecal reads it.
	style.alpha = FIXED2FLOAT((fixed_t)tpl->Alpha << 1);
	style.alphaColor = (tpl->RenderStyle.Flags & STYLEF_ColorIsFixed) ? (0xff000000 | tpl->ShadeColor) : 0xffffffff;
	style.translation = tpl->Translation;
	style.style = tpl->RenderStyle;
	style.renderFlags = tpl->RenderFlags & ~(FDecalTemplate::DECAL_RandomFlipX | FDecalTemplate::DECAL_RandomFlipY);

	// [rc4l] The random flips, rolled here because nothing else will do it for this mark.
	//
	// ApplyToDecal rolls them into the engine's decal; a mark with no engine decal behind it never
	// passes through there, so without this every floor scorch in the level is the same graphic in
	// the same orientation.
	if (tpl->RenderFlags & FDecalTemplate::DECAL_RandomFlipX) { if (pr_projdecal() & 1) style.renderFlags |= RF_XFLIP; }
	if (tpl->RenderFlags & FDecalTemplate::DECAL_RandomFlipY) { if (pr_projdecal() & 1) style.renderFlags |= RF_YFLIP; }

	int fadeStart = -1, fadeTime = 0;
	if (!GetDecalFadeTiming(tpl->Animator, fadeStart, fadeTime)) fadeStart = -1;

	BuildProjection(style, NULL, fadeStart, fadeTime, x, y, z, surfaceNormal, advance);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Making marks
// ---------------------------------------------------------------------------------------------

void SetImpactContext(fixed_t velX, fixed_t velY, fixed_t velZ, fixed_t radius)
{
	g_impact.valid = true;
	g_impact.vel[0] = FIXED2FLOAT(velX);
	g_impact.vel[1] = FIXED2FLOAT(velY);
	g_impact.vel[2] = FIXED2FLOAT(velZ);
	g_impact.radius = FIXED2FLOAT(radius);
}

void ClearImpactContext()
{
	g_impact.valid = false;
	g_impact.vel[0] = g_impact.vel[1] = g_impact.vel[2] = 0.f;
	g_impact.radius = 0.f;
}

void SpawnProjectedDecal(DBaseDecal *owner, const FDecalTemplate *tpl,
                         fixed_t x, fixed_t y, fixed_t z, line_t *hitLine)
{
	(void)tpl;
	if (fua_decalmode == 0 || owner == NULL) return;

	float surfN[3];
	NormalFromLine(hitLine, surfN);

	MarkStyle style;
	style.pic = owner->PicNum;
	style.scaleX = owner->ScaleX;
	style.scaleY = owner->ScaleY;
	style.alpha = FIXED2FLOAT(owner->Alpha);
	style.alphaColor = owner->AlphaColor;
	style.translation = owner->Translation;
	style.style = owner->RenderStyle;
	style.renderFlags = owner->RenderFlags;

	// No fade of its own: the owner has one, and reading it is always right.
	BuildProjection(style, owner, -1, 0, x, y, z, surfN, g_impact.valid ? g_impact.radius : 0.f);
}

void SpawnProjectedDecalHere(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                             const float surfaceNormal[3])
{
	// The floor/ceiling case, and the only one fua_decal_flats admits on its own -- nothing else in
	// the engine marks a flat.
	if (!ProjectedDecalsActive()) return;
	// [rc4l] A mark is often TWO decals, and this path has to spawn both itself.
	//
	// DECALDEF's `lowerdecal` puts one graphic underneath another: the BFG's mark is a green glow
	// with a black scorch beneath it. DImpactDecal::StaticCreate walks that chain, so a mark on a
	// WALL gets both -- it recurses, and the hook that mirrors each decal into a projection is inside
	// the recursion. A mark on a FLOOR never goes near StaticCreate, because Doom does not decal
	// floors at all, so it is handed the generator's template and has to do the walk itself. Without
	// it exactly one of the two appears -- a BFG leaving its glow on the ground with no scorch.
	//
	// Lowest first, so it is stored first and drawn underneath, which is the order StaticCreate uses.
	// The depth limit is a cycle guard: a template chain is map data and can be malformed.
	const FDecalTemplate *chain[8];
	int depth = 0;
	for (const FDecalTemplate *t = tpl; t != NULL && depth < 8; depth++)
	{
		chain[depth] = t;
		if (t->LowerDecal == NULL) { depth++; break; }
		const FDecalTemplate *lower = t->LowerDecal->GetDecal();
		if (lower == t) { depth++; break; }
		t = lower;
	}

	// Zero advance: the caller passes the plane's own height, which IS the contact point.
	for (int i = depth - 1; i >= 0; i--) SpawnFromTemplate(chain[i], x, y, z, surfaceNormal, 0.f);
}

void SpawnProjectedDecalOnLine(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                               line_t *hitLine)
{
	// A WALL mark, so it takes the wall switch. fua_decal_flats must not put projections back onto
	// walls by a side door: that stretching is what sent walls back to the glued quad.
	if (fua_decalmode == 0) return;
	float surfN[3];
	NormalFromLine(hitLine, surfN);
	SpawnFromTemplate(tpl, x, y, z, surfN, g_impact.valid ? g_impact.radius : 0.f);
}

void ForgetProjectedDecal(DBaseDecal *owner)
{
	for (unsigned i = 0; i < g_decals.Size(); )
	{
		if (g_decals[i].owner == owner) g_decals.Delete(i);
		else i++;
	}
}

void ClearProjectedDecals()
{
	g_decals.Clear();
	g_gpuDecals.Clear();
	ClearImpactContext();
}

// ---------------------------------------------------------------------------------------------
// Keeping them, and handing them to the backend
// ---------------------------------------------------------------------------------------------

// [rc4l] Age every mark by one frame, and drop the ones that are over.
//
// Separate from drawing them, and called whatever the mode is: nothing about a mark's lifetime is
// the renderer's business, and if the ageing lived inside the drawing then turning marks off would
// freeze every transient one at the alpha it had when the mode changed.
void UpdateProjectedDecals()
{
	for (unsigned i = 0; i < g_decals.Size(); )
	{
		ProjDecal &d = g_decals[i];
		d.currentAlpha = 0.f;

		if (d.owner != NULL)
		{
			// [rc4l] The alpha is READ, never reproduced. The engine's own thinker fades the decal,
			// and an earlier version that copied the fade curve instead ran a glow at two thirds
			// brightness the instant it appeared and had it gone while the engine still had a second
			// of it left. Beside GL that reads as "the glow is dimmer in Vulkan".
			if (!(d.owner->RenderFlags & RF_INVISIBLE)) d.currentAlpha = FIXED2FLOAT(d.owner->Alpha);
			i++;
			continue;
		}

		// Nothing else is going to fade this one. Full alpha until the decay starts, then down to
		// nothing over the decay time -- the fader's own curve, from its own numbers.
		float alpha = d.baseAlpha;
		if (d.fadeStart >= 0)
		{
			const int age = gametic - d.spawnTic;
			if (age >= d.fadeStart)
			{
				const int into = age - d.fadeStart;
				if (d.fadeTime <= 0 || into >= d.fadeTime)
				{
					// Over. Drop it rather than draw nothing forever.
					g_decals.Delete(i);
					continue;
				}
				alpha *= 1.f - (float)into / (float)d.fadeTime;
			}
		}
		d.currentAlpha = alpha;
		i++;
	}
}

int GetProjectedDecalsGpu(const GpuDecal **out)
{
	g_gpuDecals.Clear();
	if (!ProjectedDecalsActive()) { if (out) *out = NULL; return 0; }

	for (unsigned i = 0; i < g_decals.Size(); i++)
	{
		const ProjDecal &d = g_decals[i];
		if (d.currentAlpha <= 0.004f) continue;

		FTexture *texture = TexMan[d.pic];
		if (texture == NULL) continue;
		FMaterial *mat = FMaterial::ValidateTexture(texture, true);
		if (mat == NULL) continue;

		GpuDecal g;
		// Into MESH space: (x, z-up, y). Doing it here keeps the shader free of the convention.
		const DecalBox &b = d.box;
		g.centre[0] = b.origin[0]; g.centre[1] = b.origin[2]; g.centre[2] = b.origin[1];
		// Divided by the half-extent, so the box test in the shader is a comparison against one.
		const float iw = (b.halfW > 0.001f) ? 1.f / b.halfW : 0.f;
		const float ih = (b.halfH > 0.001f) ? 1.f / b.halfH : 0.f;
		g.right[0] = b.right[0]*iw; g.right[1] = b.right[2]*iw; g.right[2] = b.right[1]*iw;
		g.up[0]    = b.up[0]*ih;    g.up[1]    = b.up[2]*ih;    g.up[2]    = b.up[1]*ih;
		g.axis[0]  = b.axis[0];     g.axis[1]  = b.axis[2];     g.axis[2]  = b.axis[1];
		g.halfW = b.halfW; g.halfH = b.halfH;
		g.anchorSector = d.anchorSector;
		g.anchorPlane  = d.anchorPlane;
		g.anchorOffset = d.anchorOffset;
		g.near_ = b.near_; g.far_ = b.far_;

		// [rc4l] A shaded decal's texture is an alpha MASK and its colour is its own AlphaColor.
		// Sampled as an ordinary image the red channel reads as brightness and a black burn paints
		// white; multiplied into the tint, a green glow times (mask, 0, 0) is black.
		g.redToAlpha = d.redToAlpha;
		g.additive = d.additive;
		g.fullbright = d.fullbright;
		if (d.redToAlpha)
		{
			g.r = ((d.alphaColor >> 16) & 0xff) / 255.f;
			g.g = ((d.alphaColor >> 8) & 0xff) / 255.f;
			g.b = (d.alphaColor & 0xff) / 255.f;
		}
		else
		{
			g.r = g.g = g.b = 1.f;
		}
		g.a = d.currentAlpha;
		g.material = mat;
		g_gpuDecals.Push(g);
	}

	if (out) *out = g_gpuDecals.Size() ? &g_gpuDecals[0] : NULL;
	return (int)g_gpuDecals.Size();
}

int GetProjectedDecalCount() { return (int)g_decals.Size(); }

}} // namespace zx::levelmesh

namespace zx { namespace hwrender {
void GetDeferredDecalStats(int &boxes, int &draws, int &textures, const char **bail);
}}

CCMD(fua_projdecals_stats)
{
	int boxes = 0, draws = 0, textures = 0;
	const char *bail = "";
	zx::hwrender::GetDeferredDecalStats(boxes, draws, textures, &bail);
	Printf("projected decals: %d live\n", zx::levelmesh::GetProjectedDecalCount());
	Printf("deferred pass: %d boxes in %d draws, %d textures%s%s\n",
		boxes, draws, textures, (bail && *bail) ? ", stopped: " : "", (bail && *bail) ? bail : "");
}
