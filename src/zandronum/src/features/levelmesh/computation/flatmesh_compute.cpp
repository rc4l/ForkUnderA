// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/flatmesh_compute.h"

#include <algorithm>

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

int ComputeWalkSlots(int stackViews)
{
	return kFirstStackWalkSlot + (stackViews > 0 ? stackViews : 0);
}

int ComputeStackWalkSlot(int stackSlot)
{
	return stackSlot >= 0 ? kFirstStackWalkSlot + stackSlot : -1;
}

int ComputeSkyCaptureLists(int stackViews)
{
	return kSkyCaptureStack + (stackViews > 0 ? stackViews : 0);
}

int ComputeSkyCaptureList(bool mainView, int sceneNesting, int stackSlot, bool mirrorWalk)
{
	if (sceneNesting <= 1) return mainView ? kSkyCaptureMain : kSkyCaptureCamera;
	if (sceneNesting == 2 && mainView && stackSlot >= 0) return kSkyCaptureStack + stackSlot;
	return (mainView && mirrorWalk) ? kSkyCaptureMirror : kSkyCaptureNone;
}

int ComputeSkyLookLists(bool cameraPass, int stackSlot, bool mirrorPass, int lists[2])
{
	if (cameraPass) { lists[0] = kSkyCaptureCamera; return 1; }
	if (stackSlot >= 0) { lists[0] = kSkyCaptureStack + stackSlot; return 1; }
	lists[0] = kSkyCaptureMain;
	if (!mirrorPass) return 1;
	lists[1] = kSkyCaptureMirror;
	return 2;
}

int ComputeStackCaptureSlot(bool mainView, int sceneNesting, int stackSlot)
{
	const int list = ComputeSkyCaptureList(mainView, sceneNesting, stackSlot);
	return list >= kSkyCaptureStack ? list - kSkyCaptureStack : -1;
}

int ComputeSkyDrawList(bool cameraPass, int stackSlot)
{
	if (cameraPass) return kSkyCaptureCamera;
	return stackSlot >= 0 ? kSkyCaptureStack + stackSlot : kSkyCaptureMain;
}

bool ComputeTakesSkyViewpoint(int list, bool haveOne)
{
	return list == kSkyCaptureMain || (list >= kSkyCaptureStack && !haveOne);
}

int ComputeStackVisWords(int ids, int walkSlots)
{
	return (ids * walkSlots + 31) / 32;
}

void ComputeClearStackVisible(unsigned int *bits, int id, int slot, int walkSlots)
{
	const int bit = id * walkSlots + slot;
	bits[bit >> 5] &= ~(1u << (bit & 31));
}

void ComputeMarkStackVisible(unsigned int *bits, int id, int slot, int walkSlots)
{
	const int bit = id * walkSlots + slot;
	bits[bit >> 5] |= 1u << (bit & 31);
}

int ComputeWalkSlot(bool skyboxWalk, bool stackWalk, int stackSlot, bool playerWalk)
{
	if (skyboxWalk) return kSkyboxWalkSlot;
	if (stackWalk) return ComputeStackWalkSlot(stackSlot);
	return playerWalk ? kMainWalkSlot : -1;
}

int ComputePassWalkSlot(bool bitsReady, int stackPassSlot, bool skyPortalPass, bool skyboxWalked, bool mainCull)
{
	if (!bitsReady) return -1;
	if (skyPortalPass) return skyboxWalked ? kSkyboxWalkSlot : -1;
	if (stackPassSlot < 0 && mainCull) return kMainWalkSlot;
	return ComputeStackWalkSlot(stackPassSlot);
}

bool ComputeWalkBitsNeeded(int stackViews, int skyHoles, bool skyboxWalked, bool mainWalked)
{
	return stackViews > 0 || skyHoles > 0 || skyboxWalked || mainWalked;
}

int ComputeViewArea(bool hasTransfer, float viewz, float fakeFloor, float fakeCeiling, bool floorOnly)
{
	if (!hasTransfer) return kViewAreaNormal;
	if (viewz <= fakeFloor) return kViewAreaBelow;
	return (viewz > fakeCeiling && !floorOnly) ? kViewAreaAbove : kViewAreaNormal;
}

void ComputeSkyHoleRecord(float x1, float y1, float x2, float y2, const float *zfloor, const float *zceil, float *out)
{
	out[0] = x1; out[1] = y1; out[2] = x2; out[3] = y2;
	out[4] = zfloor[0]; out[5] = zceil[0]; out[6] = zfloor[1]; out[7] = zceil[1];
}

bool ComputeSkyHoleToShader(int list, int held)
{
	return list == kSkyCaptureMain && held < kMaxSkyHoles;
}

bool ComputeBehindSkyHole(const float *eye, const float *p, const float *hole)
{
	// [rc4l] The scene shader's fuaBehindSkyHole line for line: where the map-plane line from the eye to p meets the hole's wall, as fractions along each.
	const float dx = p[0] - eye[0], dy = p[2] - eye[2];
	const float ex = hole[2] - hole[0], ey = hole[3] - hole[1];
	const float den = dx * ey - dy * ex;
	if (fabsf(den) < 1e-6f) return false;
	const float wx = hole[0] - eye[0], wy = hole[1] - eye[2];
	const float t = (wx * ey - wy * ex) / den;
	const float s = (wx * dy - wy * dx) / den;
	// [rc4l] Short of 1 by a hair, so what stands on the hole's own line is not behind it.
	if (s < 0.f || s > 1.f || t <= 0.f || t >= 0.9999f) return false;
	const float h = eye[1] + t * (p[1] - eye[1]);
	const float bottom = hole[4] + s * (hole[6] - hole[4]), top = hole[5] + s * (hole[7] - hole[5]);
	return h >= bottom && h <= top;
}

bool ComputeMidTextureTranslucent(float lineAlpha, bool additive, bool textureTransparent)
{
	if (textureTransparent) return true;
	return lineAlpha != 0.f && (additive || lineAlpha < 1.f);
}

MidTextureBlend ComputeMidTextureBlend(float lineAlpha, bool addTrans, bool textureTransparent)
{
	MidTextureBlend b;
	b.additive = lineAlpha != 0.f && addTrans;
	b.translucent = ComputeMidTextureTranslucent(lineAlpha, b.additive, textureTransparent);
	b.alpha = lineAlpha != 0.f ? lineAlpha : 1.f;
	b.blendMode = ComputeWallBlendMode(b.translucent, b.additive, b.alpha);
	return b;
}

float ComputePlaneAlpha(bool stacked, float stackAlpha, float reflect)
{
	return stacked ? stackAlpha : 1.f - reflect;
}

bool ComputePlaneTakesBandLight(bool ceiling, bool absolute, bool bandFromSector, bool ownBand)
{
	return !ownBand && (!absolute || (!ceiling && !bandFromSector));
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

bool ComputeStackVisible(const unsigned int *bits, int words, int id, int slot, int walkSlots)
{
	const int bit = id * walkSlots + slot;
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

bool ComputeMirrorCaptures(int depth, bool mainView, int sceneNesting)
{
	return depth >= 1 && mainView && sceneNesting == depth + 1;
}

int ComputeMirrorViewId(int depth, int line, int deeperIndex, int lineCount)
{
	return depth <= 1 ? line : lineCount + deeperIndex;
}

int ComputeFindMirrorPath(const std::vector<std::vector<int> > &paths, const int *path, int depth)
{
	for (size_t i = 0; i < paths.size(); i++)
		if ((int)paths[i].size() == depth && std::equal(paths[i].begin(), paths[i].end(), path)) return (int)i;
	return -1;
}

int ComputeAddMirrorPath(std::vector<std::vector<int> > &paths, const int *path, int depth)
{
	const int at = ComputeFindMirrorPath(paths, path, depth);
	if (at >= 0) return at;
	paths.push_back(std::vector<int>(path, path + depth));
	return (int)paths.size() - 1;
}

void ComputeMirrorSteps(const std::vector<std::vector<int> > &paths, int lineCount, std::vector<int> &out)
{
	out.clear();
	for (size_t i = 0; i < paths.size(); i++)
	{
		const std::vector<int> &p = paths[i];
		const int depth = (int)p.size();
		if (depth < 2) continue;
		const int outer = depth == 2 ? -1 : ComputeFindMirrorPath(paths, &p[0], depth - 1);
		if (depth > 2 && outer < 0) continue;
		out.push_back(ComputeMirrorViewId(depth - 1, p[0], outer, lineCount));
		out.push_back(p[depth - 1]);
		out.push_back(ComputeMirrorViewId(depth, p[0], (int)i, lineCount));
	}
}

bool ComputeDrawsDynamicPiece(int pieceMirrorLine, int pieceStackSlot, int pieceOnMirror, int drawingMirrorLine, int drawingStackSlot, int drawingMirrorFace)
{
	return pieceMirrorLine == drawingMirrorLine && pieceStackSlot == drawingStackSlot && pieceOnMirror == drawingMirrorFace;
}

bool ComputeRendersMirror(bool walkedByGL, bool standalone, bool onScreen)
{
	return walkedByGL || (standalone && onScreen);
}

bool ComputeOwnsFrameCapture(bool mainView, int sceneNesting)
{
	return ComputeSkyCaptureList(mainView, sceneNesting, -1) == kSkyCaptureMain;
}

FlatChange ComputeFlatChange(bool stored, const FlatStamp &baked, const FlatStamp &now, bool scrolled)
{
	if (!stored) return kFlatRebuild;
	const bool sameShape = baked.pa == now.pa && baked.pb == now.pb && baked.pc == now.pc && baked.pd == now.pd
		&& baked.dz == now.dz && baked.xscale == now.xscale && baked.yscale == now.yscale && baked.angle == now.angle
		&& baked.material == now.material && baked.baseTex == now.baseTex;
	if (!sameShape) return kFlatRebuild;
	if (scrolled || baked.xoffs != now.xoffs || baked.yoffs != now.yoffs) return kFlatRescroll;
	const bool sameShade = baked.lightLevel == now.lightLevel && baked.extraLight == now.extraLight
		&& baked.lightColor == now.lightColor && baked.fadeColor == now.fadeColor
		&& baked.desaturation == now.desaturation && baked.blendFactor == now.blendFactor
		&& baked.alpha == now.alpha && baked.renderStyle == now.renderStyle;
	return sameShade ? kFlatSame : kFlatReshade;
}

}} // namespace zx::levelmesh
