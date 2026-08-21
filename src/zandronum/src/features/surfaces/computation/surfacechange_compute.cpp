// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/surfacechange_compute.h"

namespace zx { namespace surfaces {

namespace {

// [rc4l] Exact comparison, deliberately.
//
// A tolerance here would be a second opinion about when two surfaces are the same, and the first
// opinion -- the code that produced these numbers -- is the one that matters. These are not measured
// quantities; they are values copied out of the engine, so two bakes of an unchanged surface produce
// bit-identical floats and anything else IS a change.
inline bool Same(float a, float b) { return a == b; }

} // namespace

SurfaceChange ComputeSurfaceChange(const SurfaceKey &was, const SurfaceKey &now)
{
	// [rc4l] Batching first, because it is the answer that costs the most and subsumes the other.
	//
	// A surface that has moved to a different material is in the wrong batch, and no amount of
	// rewriting its vertices where they lie will fix that -- which is exactly the mistake the switch
	// bug was: the piece was patched in place and went on being drawn with the texture its batch
	// still named.
	if (was.material != now.material) return kSurfaceRebatch;
	if (was.baseTex != now.baseTex) return kSurfaceRebatch;
	if (was.blendMode != now.blendMode) return kSurfaceRebatch;
	if (was.translation != now.translation) return kSurfaceRebatch;
	// A range that moved or changed size takes the surface out of whatever contiguous run it was
	// part of, which is the same problem wearing different clothes.
	if (was.rangeOffset != now.rangeOffset) return kSurfaceRebatch;
	if (was.rangeCount != now.rangeCount) return kSurfaceRebatch;

	// Everything below is shading a backend folded into vertices it has already uploaded. Wrong, but
	// wrong in place.
	if (!Same(was.alpha, now.alpha)) return kSurfaceRepaint;
	if (!Same(was.colorR, now.colorR) || !Same(was.colorG, now.colorG) ||
	    !Same(was.colorB, now.colorB)) return kSurfaceRepaint;
	if (!Same(was.softLight, now.softLight)) return kSurfaceRepaint;
	if (!Same(was.fogDensity, now.fogDensity)) return kSurfaceRepaint;
	if (was.fogColor != now.fogColor) return kSurfaceRepaint;
	if (was.fogMode != now.fogMode) return kSurfaceRepaint;
	// [rc4l] The normal decides which dynamic lights reach the surface, so a door that tilts or a
	// slope that moves changes how it is lit without moving a single vertex of anything else.
	if (!Same(was.normX, now.normX) || !Same(was.normY, now.normY) ||
	    !Same(was.normZ, now.normZ)) return kSurfaceRepaint;

	return kSurfaceUnchanged;
}

}} // namespace zx::surfaces
