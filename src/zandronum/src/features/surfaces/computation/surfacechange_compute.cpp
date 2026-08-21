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
	// A surface that has moved to a different texture is in the wrong batch, and no amount of
	// rewriting its vertices where they lie will fix that -- which is exactly the mistake the switch
	// bug was: the piece was patched in place and went on being drawn with the texture its batch
	// still named.
	//
	// The BASE texture, though, not the frame showing right now. An animated texture resolves to a
	// different FMaterial every few tics while remaining the same surface in the same batch -- the
	// backend re-resolves each batch from its base texture every frame, which is the whole reason
	// baseTex exists. Reading a new frame as a new batch made every animated wall on a level with
	// moving sectors force a full scene rebuild, 5126 of them on dbab04 in under a minute, so the
	// material is compared further down where a repaint answers it for sixty-four bytes.
	if (was.baseTex != now.baseTex) return kSurfaceRebatch;
	if (was.blendMode != now.blendMode) return kSurfaceRebatch;
	if (was.translation != now.translation) return kSurfaceRebatch;
	// A range that moved takes the surface out of whatever contiguous run it was part of, which is
	// the same problem wearing different clothes.
	if (was.rangeOffset != now.rangeOffset) return kSurfaceRebatch;
	// [rc4l] A count of zero at the SAME offset means it was squashed, not resized.
	//
	// Squashing is how a surface that has temporarily stopped existing is held: the vertices are
	// zeroed so they rasterise to nothing and the range is KEPT so the geometry can come back into
	// it. MeshSquash records that by zeroing the stored piece's count. Coming back into the same
	// range is therefore a repaint -- the batch never stopped covering those vertices -- and reading
	// it as a resize made every door and every lift rebatch on the way back: 320,544 of them on
	// dbab04 in under a minute, each one a full scene rebuild.
	if (was.rangeCount != now.rangeCount && was.rangeCount != 0) return kSurfaceRebatch;

	// Everything below is shading a backend folded into vertices it has already uploaded. Wrong, but
	// wrong in place.
	if (was.material != now.material) return kSurfaceRepaint;   // an animation frame; see above
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
