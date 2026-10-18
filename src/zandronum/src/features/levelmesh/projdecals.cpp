// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/projdecals.h"
#include "features/levelmesh/computation/decalproject_compute.h"
#include "features/levelmesh/computation/packedcolor_compute.h"
#include "features/sprites/computation/spritestyle_compute.h"   // [rc4l] ComputeSpriteBlendMode
#include "features/gamecopy/decals.h"   // [rc4l] the floor and ceiling marks as the copy holds them
#include "features/materials/materials.h"

#include "actor.h"        // [rc4l] RF_*
#include "c_cvars.h"
#include "textures/textures.h"

#include <math.h>
#include <unordered_map>
#include <vector>

// [rc4l] The most oblique a mark is allowed to be, as the cosine of the angle off head-on: 0.7 is 45 degrees and a 1.41x stretch, 1.0 lands every mark square-on.
CVAR(Float, fua_projdecal_maxskew, 0.7f, CVAR_ARCHIVE)

// [rc4l] How far the box reaches behind the contact point, as a fraction of the picture's size, so a mark carries onto the wall beside it (see ComputeDecalBoxDepth).
CVAR(Float, fua_projdecal_depth, 0.6f, CVAR_ARCHIVE)

// [rc4l] Print each mark's box as it is worked out, since a clipped, a stretched and a dark picture look alike on screen.
CVAR(Bool, fua_projdecal_debug, false, 0)

namespace zx { namespace levelmesh {

namespace {

using zx::gamecopy::FlatDecalRecord;

// [rc4l] A mark's box as last worked out, and what it was worked out from.
struct CachedBox
{
	FlatDecalRecord from;
	DecalBox box;
	bool boxed;
};

std::unordered_map<const void *, CachedBox> g_boxes;
TArray<GpuDecal> g_gpuDecals;

// [rc4l] Whether two records give the same box, which a fade or a colour change does not touch.
bool SameShape(const FlatDecalRecord &a, const FlatDecalRecord &b)
{
	if (a.PicNum != b.PicNum || a.ScaleX != b.ScaleX || a.ScaleY != b.ScaleY) return false;
	if (a.ContactX != b.ContactX || a.ContactY != b.ContactY || a.ContactZ != b.ContactZ) return false;
	if ((a.RenderFlags ^ b.RenderFlags) & (RF_XFLIP | RF_YFLIP)) return false;
	for (int i = 0; i < 3; i++) if (a.Normal[i] != b.Normal[i] || a.Travel[i] != b.Travel[i]) return false;
	return true;
}

// [rc4l] The mark's box: its basis from the way the hit went and the plane it landed on, its size from its picture, its depth from ComputeDecalBoxDepth; false for a picture with nothing to draw.
bool BuildBox(const FlatDecalRecord &m, DecalBox &box)
{
	FTexture *texture = TexMan[m.PicNum];
	if (texture == NULL) return false;
	const void *mat = zx::materials::MaterialForTexture(texture, true);
	if (mat == NULL) return false;
	int matW, matH;
	zx::materials::MaterialRenderSize(mat, matW, matH);

	float right[3], up[3], axis[3];
	BuildDecalBasis(m.Travel, m.Normal, fua_projdecal_maxskew, right, up, axis);
	const float halfW = matW * FIXED2FLOAT(m.ScaleX) * 0.5f;
	const float halfH = matH * FIXED2FLOAT(m.ScaleY) * 0.5f;
	if (halfW < 0.5f || halfH < 0.5f) return false;
	ApplyDecalFlip(!!(m.RenderFlags & RF_XFLIP), !!(m.RenderFlags & RF_YFLIP), right, up);

	// [rc4l] A flat mark rests exactly on its plane, so its origin needs no advancing.
	const float pos[3] = { FIXED2FLOAT(m.ContactX), FIXED2FLOAT(m.ContactY), FIXED2FLOAT(m.ContactZ) };
	DecalOriginFromImpact(pos, axis, 0.f, box.origin);
	for (int i = 0; i < 3; i++) { box.right[i] = right[i]; box.up[i] = up[i]; box.axis[i] = axis[i]; }
	box.halfW = halfW;
	box.halfH = halfH;
	const float cornerRadius = sqrtf(halfW*halfW + halfH*halfH);
	const float cosTheta = -(axis[0]*m.Normal[0] + axis[1]*m.Normal[1] + axis[2]*m.Normal[2]);
	ComputeDecalBoxDepth(cornerRadius, cosTheta, (float)fua_projdecal_depth, box.near_, box.far_);
	if (fua_projdecal_debug)
		Printf("projdecal: half %.1f x %.1f  depth -%.1f..+%.1f  axis (%.2f, %.2f, %.2f)\n",
			box.halfW, box.halfH, box.near_, box.far_, axis[0], axis[1], axis[2]);
	return true;
}

// [rc4l] A record's box, from the cache while it still has the shape it was worked out for.
const CachedBox &BoxOf(const FlatDecalRecord &r)
{
	CachedBox &c = g_boxes[r.who];
	if (c.from.who != r.who || !SameShape(c.from, r))
	{
		c.from = r;
		c.boxed = BuildBox(r, c.box);
	}
	return c;
}

} // namespace

bool ProjectedDecalsActive() { return !zx::gamecopy::FlatDecals().empty(); }

void ClearProjectedDecals()
{
	g_boxes.clear();
	g_gpuDecals.Clear();
}

int GetProjectedDecalsGpu(const GpuDecal **out)
{
	g_gpuDecals.Clear();
	const std::vector<FlatDecalRecord> &marks = zx::gamecopy::FlatDecals();
	// [rc4l] A cache that outgrew the marks by much has kept boxes for marks that are gone.
	if (g_boxes.size() > 2 * marks.size() + 64) g_boxes.clear();
	for (const FlatDecalRecord &d : marks)
	{
		const float alpha = (d.RenderFlags & RF_INVISIBLE) ? 0.f : FIXED2FLOAT(d.Alpha);
		if (alpha <= 0.004f) continue;
		const CachedBox &cached = BoxOf(d);
		if (!cached.boxed) continue;
		FTexture *texture = TexMan[d.PicNum];
		if (texture == NULL) continue;
		const void *mat = zx::materials::MaterialForTexture(texture, true);
		if (mat == NULL) continue;

		GpuDecal g;
		// Into MESH space: (x, z-up, y). Doing it here keeps the shader free of the convention.
		const DecalBox &b = cached.box;
		g.centre[0] = b.origin[0]; g.centre[1] = b.origin[2]; g.centre[2] = b.origin[1];
		// Divided by the half-extent, so the box test in the shader is a comparison against one.
		const float iw = (b.halfW > 0.001f) ? 1.f / b.halfW : 0.f;
		const float ih = (b.halfH > 0.001f) ? 1.f / b.halfH : 0.f;
		g.right[0] = b.right[0]*iw; g.right[1] = b.right[2]*iw; g.right[2] = b.right[1]*iw;
		g.up[0]    = b.up[0]*ih;    g.up[1]    = b.up[2]*ih;    g.up[2]    = b.up[1]*ih;
		g.axis[0]  = b.axis[0];     g.axis[1]  = b.axis[2];     g.axis[2]  = b.axis[1];
		g.halfW = b.halfW; g.halfH = b.halfH;
		g.anchorSector = d.AnchorSector;
		g.anchorPlane  = d.AnchorPlane;
		g.anchorOffset = FIXED2FLOAT(d.AnchorOffset);
		g.near_ = b.near_; g.far_ = b.far_;
		// [rc4l] A shaded decal's texture is an alpha mask and its colour is its own AlphaColor.
		g.redToAlpha = !!(d.RenderStyle.Flags & STYLEF_RedIsAlpha);
		g.translation = d.Translation;
		g.blend = zx::sprites::ComputeSpriteBlendMode(d.RenderStyle.BlendOp, d.RenderStyle.DestAlpha, alpha);
		g.fullbright = !!(d.RenderFlags & RF_FULLBRIGHT);
		if (g.redToAlpha) ComputeUnpackRGB(d.AlphaColor, g.r, g.g, g.b);
		else g.r = g.g = g.b = 1.f;
		g.a = alpha;
		g.material = mat;
		g_gpuDecals.Push(g);
	}
	if (out) *out = g_gpuDecals.Size() ? &g_gpuDecals[0] : NULL;
	return (int)g_gpuDecals.Size();
}

// [rc4l] Name the marks that exist, with the alpha that decides whether they are drawn, the plane each rides, and where it rests and faces.
void PrintProjectedDecals(int limit)
{
	const std::vector<FlatDecalRecord> &marks = zx::gamecopy::FlatDecals();
	for (size_t i = 0; i < marks.size() && (int)i < limit; i++)
	{
		const FlatDecalRecord &d = marks[i];
		FTexture *t = TexMan[d.PicNum];
		Printf("  mark %u: %s alpha %.3f, sector %d %s, at z %.1f facing (%.3f, %.3f, %.3f)\n", (unsigned)i, t != NULL ? t->Name.GetChars() : "?",
			FIXED2FLOAT(d.Alpha), d.AnchorSector, d.AnchorPlane == 0 ? "floor" : "ceiling", FIXED2FLOAT(d.ContactZ), d.Normal[0], d.Normal[1], d.Normal[2]);
	}
}

int GetProjectedDecalCount() { return (int)zx::gamecopy::FlatDecals().size(); }


}} // namespace zx::levelmesh
