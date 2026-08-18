// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/projdecals.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/computation/decalproject_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "m_bbox.h"
#include "a_sharedglobal.h"          // DBaseDecal
#include "decallib.h"
#include "doomstat.h"
#include "m_random.h"
#include "p_local.h"
#include "p_trace.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "tarray.h"
#include "templates.h"
#include "gl/textures/gl_material.h"
#include "gl/data/gl_vertexbuffer.h" // FFlatVertex

#include <math.h>

// [rc4l] The switch between the two ways a mark can be drawn, so they can be compared live.
//
// 1 projects onto the geometry inside a box; 0 falls back to the glued quad the GL renderer draws,
// which is the same shape in both backends. Keeping the old path a cvar away is what makes "is this
// better?" a question anyone can answer in one console command rather than a rebuild.
EXTERN_CVAR (Int, cl_maxdecals)

CVAR(Bool, fua_projdecals, true, CVAR_ARCHIVE)

// How square-on a surface must be to the projection to receive any of it. Edge-on surfaces would
// take a zero-area sliver of infinitely stretched texture.
CVAR(Float, fua_projdecal_facing, 0.08f, CVAR_ARCHIVE)

// The most oblique a mark is allowed to be, as the cosine of the angle off head-on. A rocket
// arriving nearly parallel to a wall would otherwise smear a mark the length of the corridor.
CVAR(Float, fua_projdecal_maxskew, 0.35f, CVAR_ARCHIVE)

// [rc4l] How DEEP the box is, as a fraction of the picture's own size.
//
// This is what stops a mark wrapping round a corner from becoming a long streak. A surface lying
// nearly along the projection is covered for the box's whole depth, so the depth IS the length of
// the streak; a blast is roughly spherical, so a depth near the picture's own size is the shape it
// actually has. Deeper reaches further onto a floor in front of a wall and starts to smear; shallower
// cuts the wrap short, which is the failure this whole system exists to remove.
CVAR(Float, fua_projdecal_depth, 0.6f, CVAR_ARCHIVE)

// [rc4l] Fade a surface's share by how squarely the blast met it.
//
// Soot lands on a surface in proportion to the cosine of the angle it arrives at -- the same reason
// a light at a grazing angle lights a wall less. Without it, the sliver of wall that a projection
// grazes takes the picture at FULL strength and reads as a hard-edged black streak beside the mark
// rather than as the edge of it. 0 disables the fade entirely, for comparison.
CVAR(Float, fua_projdecal_falloff, 0.0f, CVAR_ARCHIVE)

// [rc4l] Print the box and every surface it cut, at the moment a mark is made.
//
// A mark that comes out the wrong SHAPE cannot be diagnosed from a screenshot: a picture clipped by
// a depth plane, a picture stretched across a grazing wall, and a picture whose texture simply looks
// like that are the same handful of dark pixels. The numbers say which.
CVAR(Bool, fua_projdecal_debug, false, 0)

// [rc4l] How many slices of depth a surface's share is drawn in.
//
// One is no ramp at all: the far end of a mark reaching across a floor stops in a straight line
// where the box ends. More slices is a smoother run-out for more pieces; four is enough to read as
// a fade at the size decals actually are.
CVAR(Int, fua_projdecal_bands, 4, CVAR_ARCHIVE)

// [rc4l] Whether a mark has to be VISIBLE from where the blast landed.
//
// Without this a box that pokes through a wall prints on whatever is on the other side of it, and a
// scorch appears in the next room with the wall it came through still solid between them. On by
// default; off is only useful for seeing what the box would have covered.
CVAR(Bool, fua_projdecal_occlude, true, CVAR_ARCHIVE)

// [rc4l] Its own RNG stream, not pr_decal(), because a projection must not change what the engine
// rolls. Sharing one would make the game desync against a demo or another client the moment a
// mark was drawn -- the renderer would be consuming the simulation's random numbers.
static FRandom pr_projdecal ("ProjDecal");

namespace zx { namespace levelmesh {

namespace {

// [rc4l] What Doom knew about the projectile, carried from where it was still true.
struct ImpactContext
{
	bool  valid;
	float vel[3];     // map units per tic, at the moment of impact
	float radius;     // the projectile's own radius: how far short of the surface it stopped
};

ImpactContext g_impact = { false, { 0.f, 0.f, 0.f }, 0.f };

// One surface's share of one mark: already triangulated, already in mesh space, with the light of
// the surface it landed on.
struct DecalPatch
{
	TArray<FFlatVertex> tris;
	int        lightLevel;
	float      strength;    // how much of the blast reached this surface: see fua_projdecal_falloff
	FColormap  colormap;
};

struct ProjDecal
{
	// [rc4l] The engine's decal, when there is one. It owns the fade, the lifetime and the
	// cl_maxdecals recycling, and the alpha is read off it every frame rather than reproduced.
	//
	// NULL for a mark the engine never made -- one on a floor, where Doom decals nothing. Those fade
	// themselves, from the fader's own timing read once at spawn, which is the only case where the
	// curve has to be repeated here at all.
	DBaseDecal        *owner;
	int                spawnTic;
	int                fadeStart;   // tics of full alpha; -1 when this never fades
	int                fadeTime;
	float              baseAlpha;

	TArray<DecalPatch> patches;
	FTextureID         pic;
	int                translation;
	unsigned int       alphaColor;
	bool               redToAlpha;
	bool               additive;
	bool               shadow;
	bool               fullbright;
	float              sortX, sortY, sortZ;
};

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

TArray<ProjDecal> g_decals;

// [rc4l] The point past which something has gone wrong and the frame is worth more than the mark.
//
// Not a tuning knob, and deliberately far above what a mark needs: it used to be 48, which a single
// BFG on an ordinary floor came within three of, and a cap that bites mid-gather drops whatever it
// had not reached yet -- so the mark ends in a straight line along a subsector boundary. That is
// indistinguishable from every other way a mark can be cut off, which is why g_capBit counts it and
// fua_projdecals_stats prints it rather than letting it happen quietly.
const unsigned kMaxPatchesPerDecal = 192;
const unsigned kMaxVertsPerPatch   = 96;

// How many marks have been truncated by the cap since the level loaded. See fua_projdecals_stats.
int g_capBit = 0;

inline float Dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

// ---------------------------------------------------------------------------------------------
// Gathering the geometry a box could possibly touch
// ---------------------------------------------------------------------------------------------

void CollectSubsectors(void *node, const fixed_t box[4], TArray<subsector_t *> &out)
{
	if (((size_t)node) & 1)
	{
		subsector_t *sub = (subsector_t *)((BYTE *)node - 1);
		if (sub != NULL && sub->numlines >= 3 && out.Size() < 256) out.Push(sub);
		return;
	}

	node_t *bsp = (node_t *)node;
	for (int side = 0; side < 2; side++)
	{
		const fixed_t *cb = bsp->bbox[side];
		// The child's own bounding box, so a box in one room never walks into the next one.
		if (box[BOXRIGHT] < cb[BOXLEFT] || box[BOXLEFT] > cb[BOXRIGHT]) continue;
		if (box[BOXTOP] < cb[BOXBOTTOM] || box[BOXBOTTOM] > cb[BOXTOP]) continue;
		CollectSubsectors(bsp->children[side], box, out);
	}
}

// [rc4l] The world-space corners of one candidate surface, plus which way it faces.
struct Candidate
{
	float      poly[4 * 3];
	int        count;
	float      normal[3];
	sector_t  *lightFrom;
};

void PlaneNormal(const secplane_t &p, float out[3])
{
	// secplane_t stores ax + by + cz + d = 0 with c positive for a floor and negative for a ceiling,
	// so the raw coefficients already point out of the surface and into the room.
	out[0] = FIXED2FLOAT(p.a);
	out[1] = FIXED2FLOAT(p.b);
	out[2] = FIXED2FLOAT(p.c);
	const float len = sqrtf(Dot3(out, out));
	if (len > 1e-6f) { out[0] /= len; out[1] /= len; out[2] /= len; }
	else { out[0] = 0.f; out[1] = 0.f; out[2] = 1.f; }
}

// A wall quad from a seg, between two heights measured at each end. Wound so that (v1 bottom, v2
// bottom, v2 top, v1 top) traces the face -- convex, which is all the clipper requires.
void PushWallCandidate(TArray<Candidate> &out, seg_t *seg,
                       float z1bot, float z1top, float z2bot, float z2top, sector_t *lightFrom)
{
	if (z1top - z1bot <= 0.01f && z2top - z2bot <= 0.01f) return;
	if (out.Size() >= kMaxPatchesPerDecal) return;

	Candidate c;
	c.count = 4;
	c.lightFrom = lightFrom;
	const float x1 = seg->v1->fx, y1 = seg->v1->fy;
	const float x2 = seg->v2->fx, y2 = seg->v2->fy;
	c.poly[0] = x1; c.poly[1]  = y1; c.poly[2]  = z1bot;
	c.poly[3] = x2; c.poly[4]  = y2; c.poly[5]  = z2bot;
	c.poly[6] = x2; c.poly[7]  = y2; c.poly[8]  = z2top;
	c.poly[9] = x1; c.poly[10] = y1; c.poly[11] = z1top;

	// Walking v1 to v2, the front side is to the right: (dy, -dx).
	const float dx = x2 - x1, dy = y2 - y1;
	const float len = sqrtf(dx*dx + dy*dy);
	if (len < 1e-4f) return;
	c.normal[0] = dy / len;
	c.normal[1] = -dx / len;
	c.normal[2] = 0.f;
	out.Push(c);
}

void GatherCandidates(const DecalBox &box, TArray<Candidate> &out)
{
	// The box's world-space extent: its half-diagonal in each axis.
	const float reach = (box.near_ > box.far_) ? box.near_ : box.far_;
	const float ex = fabsf(box.right[0]) * box.halfW + fabsf(box.up[0]) * box.halfH + fabsf(box.axis[0]) * reach;
	const float ey = fabsf(box.right[1]) * box.halfW + fabsf(box.up[1]) * box.halfH + fabsf(box.axis[1]) * reach;

	fixed_t bbox[4];
	bbox[BOXLEFT]   = FLOAT2FIXED(box.origin[0] - ex);
	bbox[BOXRIGHT]  = FLOAT2FIXED(box.origin[0] + ex);
	bbox[BOXBOTTOM] = FLOAT2FIXED(box.origin[1] - ey);
	bbox[BOXTOP]    = FLOAT2FIXED(box.origin[1] + ey);

	TArray<subsector_t *> subs;
	if (numnodes > 0) CollectSubsectors(nodes + numnodes - 1, bbox, subs);
	else if (numsubsectors > 0) subs.Push(&subsectors[0]);

	for (unsigned i = 0; i < subs.Size() && out.Size() < kMaxPatchesPerDecal; i++)
	{
		subsector_t *sub = subs[i];
		sector_t *sec = (sub->render_sector != NULL) ? sub->render_sector : sub->sector;
		if (sec == NULL) continue;

		// --- the walls of this subsector ---
		for (DWORD k = 0; k < sub->numlines && out.Size() < kMaxPatchesPerDecal; k++)
		{
			seg_t *seg = sub->firstline + k;
			if (seg->linedef == NULL || seg->sidedef == NULL) continue;
			sector_t *front = seg->frontsector ? seg->frontsector : sec;
			if (front == NULL) continue;

			const float f1 = FIXED2FLOAT(front->floorplane.ZatPoint(seg->v1));
			const float f2 = FIXED2FLOAT(front->floorplane.ZatPoint(seg->v2));
			const float c1 = FIXED2FLOAT(front->ceilingplane.ZatPoint(seg->v1));
			const float c2 = FIXED2FLOAT(front->ceilingplane.ZatPoint(seg->v2));

			sector_t *back = seg->backsector;
			if (back == NULL)
			{
				PushWallCandidate(out, seg, f1, c1, f2, c2, front);
			}
			else
			{
				// The step up to the far room, and the header hanging down from the ceiling. These
				// are the only parts of a two-sided line that are solid enough to hold a mark; the
				// open span between them is where the engine's own decal code gives up, which is
				// exactly the case a projection does not have to care about.
				const float bf1 = FIXED2FLOAT(back->floorplane.ZatPoint(seg->v1));
				const float bf2 = FIXED2FLOAT(back->floorplane.ZatPoint(seg->v2));
				const float bc1 = FIXED2FLOAT(back->ceilingplane.ZatPoint(seg->v1));
				const float bc2 = FIXED2FLOAT(back->ceilingplane.ZatPoint(seg->v2));
				if (bf1 > f1 + 0.01f || bf2 > f2 + 0.01f) PushWallCandidate(out, seg, f1, bf1, f2, bf2, front);
				if (bc1 < c1 - 0.01f || bc2 < c2 - 0.01f) PushWallCandidate(out, seg, bc1, c1, bc2, c2, front);
			}
		}

		// --- its floor and ceiling ---
		//
		// A subsector is convex but can have many edges, so it goes in as a fan of TRIANGLES rather
		// than one polygon: the clipper takes a fixed-size candidate, and a triangle is the shape
		// that is always small enough. The box throws away every piece it does not touch anyway.
		const int n = (int)sub->numlines;
		if (n < 3) continue;
		for (int plane = 0; plane < 2 && out.Size() < kMaxPatchesPerDecal; plane++)
		{
			const secplane_t &p = (plane == 0) ? sec->floorplane : sec->ceilingplane;
			float nrm[3];
			PlaneNormal(p, nrm);

			vertex_t *v0 = sub->firstline[0].v1;
			const float x0 = v0->fx, y0 = v0->fy;
			const float z0 = (float)p.ZatPoint((double)x0, (double)y0);

			for (int k = 1; k + 1 < n && out.Size() < kMaxPatchesPerDecal; k++)
			{
				vertex_t *va = sub->firstline[k].v1;
				vertex_t *vb = sub->firstline[k + 1].v1;
				Candidate c;
				c.count = 3;
				c.lightFrom = sec;
				c.normal[0] = nrm[0]; c.normal[1] = nrm[1]; c.normal[2] = nrm[2];
				c.poly[0] = x0;     c.poly[1] = y0;     c.poly[2] = z0;
				c.poly[3] = va->fx; c.poly[4] = va->fy; c.poly[5] = (float)p.ZatPoint((double)va->fx, (double)va->fy);
				c.poly[6] = vb->fx; c.poly[7] = vb->fy; c.poly[8] = (float)p.ZatPoint((double)vb->fx, (double)vb->fy);
				out.Push(c);
			}
		}
	}
}

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

// [rc4l] Could the blast actually SEE this piece of geometry?
//
// The box is a volume, and a volume does not stop at walls: a mark near a corner reaches through it
// and prints on the far side, so a scorch turns up in the next room with the wall it came through
// still solid in between. Nothing in the clip can notice that -- the geometry really is inside the
// box -- so the question has to be asked of the map.
//
// One trace from the impact to the middle of the piece. Started a little back along the direction of
// travel, because the impact point itself sits ON the surface that was hit and a trace beginning
// inside a wall answers nothing; stopped a little short, because the destination IS a surface and
// arriving at it is not being blocked by it.
bool BlockedFromImpact(const DecalBox &box, const float eye[3], const float *local, int n)
{
	float cu = 0.f, cv = 0.f, cw = 0.f;
	for (int i = 0; i < n; i++) { cu += local[i*3 + 0]; cv += local[i*3 + 1]; cw += local[i*3 + 2]; }
	cu /= (float)n; cv /= (float)n; cw /= (float)n;

	const float target[3] = {
		box.origin[0] + box.right[0]*cu + box.up[0]*cv + box.axis[0]*cw,
		box.origin[1] + box.right[1]*cu + box.up[1]*cv + box.axis[1]*cw,
		box.origin[2] + box.right[2]*cu + box.up[2]*cv + box.axis[2]*cw,
	};
	// [rc4l] Traced from where the PROJECTILE was, not from the box's origin.
	//
	// The origin has been advanced along the direction of travel by the projectile's radius, to put
	// the box on the geometry rather than short of it -- which means it is usually a few units INSIDE
	// the wall that was hit. A trace starting inside a wall is answering a different question, and
	// the answer it gives is "blocked": every piece of floor in front of the wall was rejected as
	// invisible, and the mark stopped dead at the skirting board.
	//
	// The missile's own last position cannot be inside anything, because it was there.
	const float kBackOff = 2.f, kStopShort = 2.f;
	const float start[3] = {
		eye[0] - box.axis[0]*kBackOff,
		eye[1] - box.axis[1]*kBackOff,
		eye[2] - box.axis[2]*kBackOff,
	};

	float dir[3] = { target[0] - start[0], target[1] - start[1], target[2] - start[2] };
	const float dist = sqrtf(Dot3(dir, dir));
	if (dist <= kStopShort) return false;      // right here: nothing can be in the way
	dir[0] /= dist; dir[1] /= dist; dir[2] /= dist;

	sector_t *sec = P_PointInSector(FLOAT2FIXED(start[0]), FLOAT2FIXED(start[1]));
	if (sec == NULL) return false;

	FTraceResults res;
	const bool hit = Trace(FLOAT2FIXED(start[0]), FLOAT2FIXED(start[1]), FLOAT2FIXED(start[2]), sec,
		FLOAT2FIXED(dir[0]), FLOAT2FIXED(dir[1]), FLOAT2FIXED(dir[2]),
		FLOAT2FIXED(dist - kStopShort),
		0,          // no actor blocks a mark: a monster standing in front of a wall is not the wall
		0, NULL, res, TRACE_NoSky);
	return hit && res.HitType != TRACE_HitNone;
}

// ---------------------------------------------------------------------------------------------
// Turning a clipped polygon into mesh vertices
// ---------------------------------------------------------------------------------------------

// [rc4l] How far the piece is lifted off the surface it was cut from.
//
// Zero, and deliberately: the pieces are already routed to the depth-biased pipeline, and lifting
// them as well moves each one along ITS OWN normal -- so at a corner the wall's piece steps one way
// and the floor's steps another, and the two no longer meet. That showed up as a thin dark line
// down the corner where the two overlapped and blended twice, which reads as a seam in exactly the
// place this whole system exists to remove one.
//
// It is a named constant rather than deleted arithmetic because a backend without depth bias would
// need it back, and this is where to say so.
const float kLift = 0.0f;

void EmitPatch(const float *local, int n, const DecalBox &box, const float normal[3],
               bool flipX, bool flipY, FMaterial *mat, DecalPatch &patch)
{
	FFlatVertex fan[64];
	if (n > 64) n = 64;

	for (int i = 0; i < n; i++)
	{
		const float u0 = local[i*3 + 0], v0 = local[i*3 + 1], w0 = local[i*3 + 2];
		float wx = box.origin[0] + box.right[0]*u0 + box.up[0]*v0 + box.axis[0]*w0 + normal[0]*kLift;
		float wy = box.origin[1] + box.right[1]*u0 + box.up[1]*v0 + box.axis[1]*w0 + normal[1]*kLift;
		float wz = box.origin[2] + box.right[2]*u0 + box.up[2]*v0 + box.axis[2]*w0 + normal[2]*kLift;

		float u, v;
		DecalUV(&local[i*3], box, u, v);
		DecalFlipUV(flipX, flipY, u, v);

		// The mesh is (x, z-up, y), and the material's own coordinate range is not always 0..1.
		fan[i].Set(wx, wz, wy, mat->GetU(u * mat->TextureWidth()), mat->GetV(v * mat->TextureHeight()));
	}

	// Fan-triangulate: the clip result is convex, so a fan from the first vertex is valid.
	for (int i = 1; i + 1 < n; i++)
	{
		if (patch.tris.Size() + 3 > kMaxVertsPerPatch) break;
		patch.tris.Push(fan[0]);
		patch.tris.Push(fan[i]);
		patch.tris.Push(fan[i + 1]);
	}
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The public surface
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

namespace {

// [rc4l] Cut a mark out of the world and keep it. The one place a projection is built.
//
// `surfN` is the surface that stopped the projectile, which answers the two questions the direction
// of travel cannot: which way to face when there is no usable velocity, and which way to tilt a
// grazing hit back towards square-on.
// [rc4l] `advance` is how far short of the surface the caller's point is.
//
// A projectile is an axis-aligned BOX in Doom, so a missile stopped by a wall has its centre a
// radius short of it and the box has to be pushed forward to sit on the geometry. A hit on a FLOOR
// is not like that at all: the missile comes to rest exactly on the plane, and the caller passes the
// plane's own height, so there is nothing to close -- pushing anyway drives the box a full radius
// THROUGH the floor, and the floor then falls outside its own mark's box.
//
// That is not a subtle error at small sizes. A plasma ball has a radius of 13 and its scorch is 15
// units across, so the whole picture ended up underground: the marks were built, every surface was
// clipped away, and the count came back zero -- plasma left nothing on floors while rockets, whose
// mark is big enough to survive being shoved, still did.
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

	DecalBox box;
	const float pos[3] = { FIXED2FLOAT(x), FIXED2FLOAT(y), FIXED2FLOAT(z) };
	DecalOriginFromImpact(pos, axis, advance, box.origin);
	for (int i = 0; i < 3; i++) { box.right[i] = right[i]; box.up[i] = up[i]; box.axis[i] = axis[i]; }
	box.halfW = halfW;
	box.halfH = halfH;

	// [rc4l] The depth is measured from the picture's CORNER, not from its half-width.
	//
	// A square picture reaches sqrt(2) further at its corners than along its axes, and the depth a
	// tilted projection needs is proportional to how far out the picture goes. Using the half-width
	// left the corners a third short of the depth they needed, so the box sliced them off -- a
	// straight edge cutting across a scorch on the floor, which is not a shading fault and not the
	// texture running out, it is the mark being cut off. Reported twice before the numbers were
	// printed and the slant turned out to be 18 units where the corners wanted 25.
	//
	// tan(theta) times the corner radius is an upper bound on how far the picture's own footprint can
	// travel through the depth of the box, so nothing inside the picture can be cut by it.
	const float cornerRadius = sqrtf(halfW*halfW + halfH*halfH);
	const float cosTheta = -(axis[0]*surfN[0] + axis[1]*surfN[1] + axis[2]*surfN[2]);
	ComputeDecalBoxDepth(cornerRadius, cosTheta, (float)fua_projdecal_depth, box.near_, box.far_);

	TArray<Candidate> candidates;
	GatherCandidates(box, candidates);
	if (candidates.Size() == 0) return;
	if (candidates.Size() >= kMaxPatchesPerDecal) g_capBit++;

	if (fua_projdecal_debug)
	{
		Printf("projdecal: half %.1f x %.1f  depth -%.1f..+%.1f  axis (%.2f, %.2f, %.2f)  vel %s  %d candidates\n",
			box.halfW, box.halfH, box.near_, box.far_, axis[0], axis[1], axis[2],
			g_impact.valid ? "yes" : "none", candidates.Size());
	}

	const bool flipX = !!(style.renderFlags & RF_XFLIP);
	const bool flipY = !!(style.renderFlags & RF_YFLIP);

	ProjDecal decal;
	decal.owner = owner;
	decal.spawnTic = gametic;
	decal.fadeStart = fadeStart;
	decal.fadeTime = fadeTime;
	decal.baseAlpha = style.alpha;
	decal.pic = style.pic;
	decal.translation = style.translation;
	decal.alphaColor = style.alphaColor;
	decal.redToAlpha = !!(style.style.Flags & STYLEF_RedIsAlpha);
	decal.additive = (style.style.BlendOp == STYLEOP_Add && style.style.DestAlpha == STYLEALPHA_One);
	decal.shadow = (style.style.BlendOp == STYLEOP_Shadow);
	decal.fullbright = !!(style.renderFlags & RF_FULLBRIGHT);
	decal.sortX = box.origin[0];
	decal.sortY = box.origin[2];
	decal.sortZ = box.origin[1];

	// [rc4l] Where the picture stops being the picture and starts being reach.
	//
	// Inside its own radius a mark is exactly what the decal says it is. Beyond that it is spilling
	// onto geometry the picture never covered -- the floor running away from a wall, the far face of
	// a corner -- and that is the part that has to run out rather than end.
	const float pictureRadius = cornerRadius;
	const float outerRadius = pictureRadius + box.near_;
	int bandCount = (int)fua_projdecal_bands;
	if (bandCount < 1) bandCount = 1;
	if (bandCount > 8) bandCount = 8;

	float localBuf[64 * 3];
	float bandBuf[72 * 3];
	for (unsigned i = 0; i < candidates.Size(); i++)
	{
		const Candidate &c = candidates[i];
		if (!AcceptSurfaceForDecal(c.normal, axis, fua_projdecal_facing)) continue;

		const int n = ClipPolygonToDecalBox(c.poly, c.count, box, localBuf, 64);
		if (n < 3) continue;

		if (fua_projdecal_occlude && BlockedFromImpact(box, pos, localBuf, n)) continue;

		// How squarely the blast met this surface. 1 is head-on, and the surface that was actually
		// hit is normally at or near it; a wall the projection merely grazes is near 0.
		float facing = -Dot3(c.normal, axis);
		if (facing > 1.f) facing = 1.f;
		if (facing < 0.f) facing = 0.f;
		// Blended towards 1 by the cvar, and OFF by default: a per-surface fade is constant across
		// each face, so at a corner the two halves take different constants and a step appears down
		// the join. The radial run-out below does this job continuously instead.
		float surfaceStrength = 1.f - (float)fua_projdecal_falloff * (1.f - facing);
		if (surfaceStrength < 0.f) surfaceStrength = 0.f;

		// How far this piece runs through the depth of the box. A surface facing the projection
		// squarely barely moves through it and needs one slice; one lying along it crosses the whole
		// range and is where the ramp is actually needed.
		float wLo = 1e9f, wHi = -1e9f;
		for (int k = 0; k < n; k++)
		{
			if (localBuf[k*3 + 2] < wLo) wLo = localBuf[k*3 + 2];
			if (localBuf[k*3 + 2] > wHi) wHi = localBuf[k*3 + 2];
		}
		const float span = wHi - wLo;
		int slices = 1;
		if (span > 1.f && outerRadius > pictureRadius)
		{
			slices = (int)(span / ((outerRadius - pictureRadius) / (float)bandCount)) + 1;
			if (slices > bandCount) slices = bandCount;
			if (slices < 1) slices = 1;
		}

		for (int b = 0; b < slices; b++)
		{
			const float lo = wLo + span * (float)b / (float)slices;
			const float hi = (b + 1 == slices) ? wHi : (wLo + span * (float)(b + 1) / (float)slices);

			const float *piece = localBuf;
			int pieceCount = n;
			if (slices > 1)
			{
				pieceCount = ClipLocalPolygonToDepthBand(localBuf, n, lo - 0.001f, hi + 0.001f, bandBuf, 72);
				if (pieceCount < 3) continue;
				piece = bandBuf;
			}

			// The strength of a slice is the strength at its middle. Sampling one point per slice is
			// what makes this affordable, and the slices are small enough that the ramp reads as a
			// ramp rather than as steps.
			float mid[3] = { 0.f, 0.f, 0.f };
			for (int k = 0; k < pieceCount; k++)
			{
				mid[0] += piece[k*3 + 0]; mid[1] += piece[k*3 + 1]; mid[2] += piece[k*3 + 2];
			}
			mid[0] /= (float)pieceCount; mid[1] /= (float)pieceCount; mid[2] /= (float)pieceCount;

			const float reach = DecalRadialFade(mid, pictureRadius, outerRadius);
			const float strength = surfaceStrength * reach;
			if (strength <= 0.01f) continue;

			DecalPatch patch;
			patch.lightLevel = decal.fullbright ? 255 : (c.lightFrom ? c.lightFrom->lightlevel : 192);
			patch.strength = strength;
			patch.colormap.Clear();
			if (c.lightFrom != NULL && c.lightFrom->ColorMap != NULL) patch.colormap = c.lightFrom->ColorMap;

			EmitPatch(piece, pieceCount, box, c.normal, flipX, flipY, mat, patch);
			if (patch.tris.Size() >= 3) decal.patches.Push(patch);

			if (fua_projdecal_debug)
			{
				Printf("  surface n (%.2f, %.2f, %.2f)  facing %.2f  slice %d/%d  w %.1f..%.1f  strength %.2f  %d verts\n",
					c.normal[0], c.normal[1], c.normal[2], facing, b + 1, slices, lo, hi, strength, pieceCount);
			}
		}
	}

	if (decal.patches.Size() == 0) return;
	g_decals.Push(decal);

	// [rc4l] Retire the oldest ownerless mark once there are too many.
	//
	// The ones with an owner are already limited: cl_maxdecals recycles the engine's decals and each
	// takes its projection with it. A floor scorch has no owner and nothing to recycle it, so without
	// this a long session accumulates them until the level ends -- permanent geometry, growing, that
	// the frame has to walk every time. The same cvar sets the limit, because it is the same
	// question the player was answering when they set it.
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

} // namespace

void SpawnProjectedDecal(DBaseDecal *owner, const FDecalTemplate *tpl,
                         fixed_t x, fixed_t y, fixed_t z, line_t *hitLine)
{
	(void)tpl;
	if (!fua_projdecals || owner == NULL) return;

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

namespace {

void SpawnFromTemplate(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                       const float surfaceNormal[3], float advance)
{
	if (!fua_projdecals || tpl == NULL) return;

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

	// [rc4l] The flips, done here because nothing else will do them for this mark.
	//
	// ApplyToDecal rolls randomflipx/randomflipy into the engine's decal; a mark with no engine decal
	// behind it never passes through there, so without this every floor scorch in the level is the
	// same graphic in the same orientation.
	if (tpl->RenderFlags & FDecalTemplate::DECAL_RandomFlipX) { if (pr_projdecal() & 1) style.renderFlags |= RF_XFLIP; }
	if (tpl->RenderFlags & FDecalTemplate::DECAL_RandomFlipY) { if (pr_projdecal() & 1) style.renderFlags |= RF_YFLIP; }

	int fadeStart = -1, fadeTime = 0;
	if (!GetDecalFadeTiming(tpl->Animator, fadeStart, fadeTime)) fadeStart = -1;

	BuildProjection(style, NULL, fadeStart, fadeTime, x, y, z, surfaceNormal, advance);
}

} // namespace

void SpawnProjectedDecalHere(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                             const float surfaceNormal[3])
{
	// [rc4l] A mark is often TWO decals, and this path has to spawn both itself.
	//
	// DECALDEF's `lowerdecal` puts one graphic underneath another: the BFG's mark is a green glow
	// with a black scorch beneath it, and a rocket's is a scorch with its own darker underlay.
	// DImpactDecal::StaticCreate walks that chain, so a mark on a WALL gets both -- it recurses, and
	// the hook that mirrors each decal into a projection is inside the recursion.
	//
	// A mark on a FLOOR never goes near StaticCreate: Doom does not decal floors at all, so this
	// path is handed the generator's template directly and has to do the walk. Without it exactly
	// one of the two graphics appears -- which showed up as a BFG leaving its glow on the ground
	// with no scorch under it.
	//
	// Lowest first, so it is emitted first and drawn underneath, which is the order StaticCreate
	// uses. The depth limit is a cycle guard: a template chain is map data and can be malformed.
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

	for (int i = depth - 1; i >= 0; i--)
	{
		// Zero: the caller passes the plane's own height, which IS the contact point.
		SpawnFromTemplate(chain[i], x, y, z, surfaceNormal, 0.f);
	}
}

void SpawnProjectedDecalOnLine(const FDecalTemplate *tpl, fixed_t x, fixed_t y, fixed_t z,
                               line_t *hitLine)
{
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

void RegisterProjectedDecals()
{
	if (!fua_projdecals) return;

	for (unsigned i = 0; i < g_decals.Size(); )
	{
		ProjDecal &d = g_decals[i];
		float alpha;

		if (d.owner != NULL)
		{
			// [rc4l] The alpha is READ, never reproduced. The engine's own thinker fades the decal,
			// and an earlier version that copied the fade curve instead ran a glow at two thirds
			// brightness the instant it appeared and had it gone while the engine still had a
			// second of it left. Beside GL that reads as "the glow is dimmer in Vulkan".
			if (d.owner->RenderFlags & RF_INVISIBLE) { i++; continue; }
			alpha = FIXED2FLOAT(d.owner->Alpha);
		}
		else
		{
			// Nothing else is going to fade this one. Full alpha until the decay starts, then down
			// to nothing over the decay time -- the fader's own curve, from its own numbers.
			alpha = d.baseAlpha;
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
		}

		i++;
		if (alpha <= 0.f) continue;

		FTexture *texture = TexMan[d.pic];
		if (texture == NULL) continue;
		FMaterial *mat = FMaterial::ValidateTexture(texture, true);
		if (mat == NULL) continue;

		for (unsigned p = 0; p < d.patches.Size(); p++)
		{
			const DecalPatch &patch = d.patches[p];
			if (patch.tris.Size() < 3) continue;
			const float a = alpha * patch.strength;
			if (a <= 0.004f) continue;   // below one step of an 8-bit alpha: nothing would be drawn
			RegisterDecalTriangles(&patch.tris[0], (int)patch.tris.Size(), mat, d.translation,
				d.shadow, d.additive, a, patch.lightLevel, 0, patch.colormap,
				d.redToAlpha, d.alphaColor, d.sortX, d.sortY, d.sortZ);
		}
	}
}

void ClearProjectedDecals()
{
	g_decals.Clear();
	g_capBit = 0;
	ClearImpactContext();
}

int GetProjectedDecalTruncations() { return g_capBit; }

void GetProjectedDecalStats(int &decals, int &triangles)
{
	decals = (int)g_decals.Size();
	triangles = 0;
	for (unsigned i = 0; i < g_decals.Size(); i++)
		for (unsigned p = 0; p < g_decals[i].patches.Size(); p++)
			triangles += (int)g_decals[i].patches[p].tris.Size() / 3;
}

}} // namespace zx::levelmesh

CCMD(fua_projdecals_stats)
{
	int decals = 0, tris = 0;
	zx::levelmesh::GetProjectedDecalStats(decals, tris);
	Printf("projected decals: %d live, %d triangles, %d truncated by the surface cap\n",
		decals, tris, zx::levelmesh::GetProjectedDecalTruncations());
}
