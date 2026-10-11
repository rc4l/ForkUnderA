// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/flatmesh_compute.h"

#include <math.h>

namespace zx { namespace levelmesh {

FFFaceLight ComputeFFFaceLight(bool top, bool viewedFromBelow, bool fog, bool fix)
{
	FFFaceLight l;
	l.lightBelow = top ? fog : !fog;
	l.modelLight = fix && !top && !viewedFromBelow;
	return l;
}

bool ComputeFlatWindingReversed(bool viewedFromBelow)
{
	return viewedFromBelow;
}

bool ComputeFlatNormalFlipped(bool viewedFromBelow, float planeUp)
{
	if (planeUp > 0.f) return viewedFromBelow;
	if (planeUp < 0.f) return !viewedFromBelow;
	return false;   // vertical plane: not a flat, and its sign means nothing
}

int ComputeSurfaceBlendMode(bool additive, float alpha)
{
	if (additive) return 2;
	// One 8-bit step: Doom stores translucency as 0..255, so 255/255 must classify as opaque while
	// 254/255 must not.
	return (alpha < 1.f - 1.f / 256.f) ? 1 : 0;
}

int ComputeWallBlendMode(bool inTranslucentList, bool additive, float alpha)
{
	if (additive) return 2;
	if (inTranslucentList) return 1;
	return ComputeSurfaceBlendMode(false, alpha);
}

int ComputeStyleBlendMode(bool shadow, bool additive, float alpha)
{
	if (shadow) return 3;
	if (additive) return 2;
	return ComputeSurfaceBlendMode(false, alpha);
}

float ComputeTriangleWindingZ(float ax, float ay, float bx, float by, float cx, float cy)
{
	return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

bool ComputeCoplanarOverlap(const MeshBox &a, const MeshBox &b, float eps)
{
	// Separated on any axis: not touching at all.
	if (a.x1 < b.x0 - eps || b.x1 < a.x0 - eps) return false;
	if (a.y1 < b.y0 - eps || b.y1 < a.y0 - eps) return false;
	if (a.z1 < b.z0 - eps || b.z1 < a.z0 - eps) return false;

	// Coplanar: both must be flat in the same axis. (The overlap test above has already established
	// they share a value on that axis, to within eps.)
	const bool flatX = (a.x1 - a.x0) < eps && (b.x1 - b.x0) < eps;
	const bool flatY = (a.y1 - a.y0) < eps && (b.y1 - b.y0) < eps;
	const bool flatZ = (a.z1 - a.z0) < eps && (b.z1 - b.z0) < eps;
	if (!flatX && !flatY && !flatZ) return false;

	// And the overlap must have area, not merely a shared edge.
	const float ox = (a.x1 < b.x1 ? a.x1 : b.x1) - (a.x0 > b.x0 ? a.x0 : b.x0);
	const float oy = (a.y1 < b.y1 ? a.y1 : b.y1) - (a.y0 > b.y0 ? a.y0 : b.y0);
	const float oz = (a.z1 < b.z1 ? a.z1 : b.z1) - (a.z0 > b.z0 ? a.z0 : b.z0);
	int wide = 0;
	if (ox > eps) wide++;
	if (oy > eps) wide++;
	if (oz > eps) wide++;
	return wide >= 2;
}

bool ComputeWindingConsistent(int fromAbovePositive, int fromAboveNegative,
                              int fromBelowPositive, int fromBelowNegative)
{
	// Each group must be unanimous.
	if (fromAbovePositive > 0 && fromAboveNegative > 0) return false;
	if (fromBelowPositive > 0 && fromBelowNegative > 0) return false;

	const bool haveAbove = (fromAbovePositive + fromAboveNegative) > 0;
	const bool haveBelow = (fromBelowPositive + fromBelowNegative) > 0;
	// An absent group cannot disagree with anything.
	if (!haveAbove || !haveBelow) return true;

	// And the two must be opposite, or one cull mode cannot keep both.
	return (fromAbovePositive > 0) != (fromBelowPositive > 0);
}

PlaneUVTransform ComputeIdentityPlaneUV()
{
	PlaneUVTransform t;
	t.xoffs = t.yoffs = 0.f;
	t.xscale = t.yscale = 1.f;
	t.angleDegrees = 0.f;
	t.texWidth = t.texHeight = 64.f;
	t.hasCanvas = false;
	return t;
}

void ComputePlaneUV(float px, float py, const PlaneUVTransform &t, float &u, float &v)
{
	// The untransformed mapping: one texture per 64 map units, V running the other way.
	float uu = px / 64.f;
	float vv = -py / 64.f;

	// A zero-sized texture would divide by zero; treat it as the 64x64 default rather than produce
	// infinities that propagate silently into the vertex buffer.
	const float tw = (t.texWidth  > 0.f) ? t.texWidth  : 64.f;
	const float th = (t.texHeight > 0.f) ? t.texHeight : 64.f;

	// 1. Rotate. GL negates the plane's angle before handing it to the matrix.
	if (t.angleDegrees != 0.f)
	{
		const float a = -t.angleDegrees * 3.14159265358979323846f / 180.f;
		const float ca = cosf(a), sa = sinf(a);
		const float ru = uu * ca - vv * sa;
		const float rv = uu * sa + vv * ca;
		uu = ru; vv = rv;
	}

	// 2. Correct for the texture's real size, which the /64 above assumed was 64.
	uu *= 64.f / tw;
	vv *= 64.f / th;

	// 3. Offset, in texture widths. This is the term a scrolling floor animates.
	uu += t.xoffs / tw;
	vv += t.yoffs / th;

	// 4. The plane's own scale. A canvas texture is stored upside down, so its V scale is negated.
	uu *= t.xscale;
	vv *= t.hasCanvas ? -t.yscale : t.yscale;

	u = uu;
	v = vv;
}

PlaneUVAffine ComputePlaneUVAffine(const PlaneUVTransform &t)
{
	// [rc4l] The slope from an unoffset transform, read straight off rather than subtracted, so a large scroll costs no precision.
	PlaneUVTransform linear = t;
	linear.xoffs = linear.yoffs = 0.f;
	PlaneUVAffine a;
	ComputePlaneUV(1.f, 0.f, linear, a.ux, a.vx);
	ComputePlaneUV(0.f, 1.f, linear, a.uy, a.vy);
	ComputePlaneUV(0.f, 0.f, t, a.u0, a.v0);
	return a;
}

int ComputeSkyCaptureList(bool mainView, int sceneNesting, int stackSlot)
{
	if (sceneNesting <= 1) return mainView ? kSkyCaptureMain : kSkyCaptureCamera;
	if (sceneNesting == 2 && mainView && stackSlot >= 0 && stackSlot < kMaxStackViews) return kSkyCaptureStack + stackSlot;
	return kSkyCaptureNone;
}

int ComputeStackCaptureSlot(bool mainView, int sceneNesting, int stackSlot)
{
	const int list = ComputeSkyCaptureList(mainView, sceneNesting, stackSlot);
	return list >= kSkyCaptureStack ? list - kSkyCaptureStack : -1;
}

int ComputeSkyDrawList(bool cameraPass, int stackSlot)
{
	if (cameraPass) return kSkyCaptureCamera;
	return (stackSlot >= 0 && stackSlot < kMaxStackViews) ? kSkyCaptureStack + stackSlot : kSkyCaptureMain;
}

bool ComputeTakesSkyViewpoint(int list, bool haveOne)
{
	return list == kSkyCaptureMain || (list >= kSkyCaptureStack && !haveOne);
}

int ComputeStackVisWords(int ids)
{
	return (ids * kMaxStackViews + 31) / 32;
}

void ComputeMarkStackVisible(unsigned int *bits, int id, int slot)
{
	const int bit = id * kMaxStackViews + slot;
	bits[bit >> 5] |= 1u << (bit & 31);
}

bool ComputeMidTextureTranslucent(float lineAlpha, bool additive, bool textureTransparent)
{
	if (textureTransparent) return true;
	return lineAlpha != 0.f && (additive || lineAlpha < 1.f);
}

float ComputePlaneAlpha(bool stacked, float stackAlpha, float reflect)
{
	return stacked ? stackAlpha : 1.f - reflect;
}

int ComputeFlatSector(int renderSector, int sector)
{
	return renderSector >= 0 ? renderSector : sector;
}

bool ComputeIsOwnFlat(int model, int sector, int renderSector)
{
	return model < 0 || model == sector || model == renderSector;
}

void ComputeStackCapCorner(bool ceiling, int corner, float &x, float &y, float &z)
{
	static const int kQuad[6] = { 0, 1, 2, 0, 2, 3 };
	const int c = kQuad[corner];
	x = (c == 0 || c == 1) ? -32767.f : 32767.f;
	y = (c == 0 || c == 3) ? -32767.f : 32767.f;
	z = ceiling ? 32767.f : -32767.f;
}

bool ComputeStackVisible(const unsigned int *bits, int words, int id, int slot)
{
	const int bit = id * kMaxStackViews + slot;
	if (bits == nullptr || id < 0 || (bit >> 5) >= words) return false;
	return (bits[bit >> 5] & (1u << (bit & 31))) != 0;
}

float ComputeSegmentDistance2(float px, float py, float x1, float y1, float x2, float y2)
{
	const float dx = x2 - x1, dy = y2 - y1;
	const float len2 = dx * dx + dy * dy;
	float t = (len2 > 0.f) ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0.f;
	if (t < 0.f) t = 0.f;
	if (t > 1.f) t = 1.f;
	const float ex = x1 + t * dx - px, ey = y1 + t * dy - py;
	return ex * ex + ey * ey;
}

bool ComputePieceProbe(float centreX, float centreY, float normX, float normUp, float normY, float &probeX, float &probeY)
{
	probeX = centreX; probeY = centreY;
	const float flat = sqrtf(normX * normX + normY * normY);
	// [rc4l] A floor or ceiling's centre is already inside its convex subsector, however steep; only a wall's sits on an edge.
	if (flat <= 0.f || fabsf(normUp) > 0.01f * flat) return false;
	probeX += normX / flat;
	probeY += normY / flat;
	return true;
}

int ComputeMirrorCaptureLine(int mirrorLine, bool mainView, int sceneNesting)
{
	return (mirrorLine >= 0 && mainView && sceneNesting == 2) ? mirrorLine : -1;
}

bool ComputeDrawsDynamicPiece(int pieceMirrorLine, int pieceStackSlot, int drawingMirrorLine, int drawingStackSlot)
{
	return pieceMirrorLine == drawingMirrorLine && pieceStackSlot == drawingStackSlot;
}

bool ComputeRendersMirror(bool walkedByGL, bool standalone, bool onScreen)
{
	return walkedByGL || (standalone && onScreen);
}

bool ComputeOwnsFrameCapture(bool mainView, int sceneNesting)
{
	return ComputeSkyCaptureList(mainView, sceneNesting, -1) == kSkyCaptureMain;
}

}} // namespace zx::levelmesh
