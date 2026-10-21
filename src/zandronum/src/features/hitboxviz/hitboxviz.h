// [MGOOOOOO] Debug hitbox / explosion-region overlay. Draws each actor's collision box, its attack
// box (HitRadius/HitHeight) when that differs, and the region an explosion actually damaged --
// client-side, GL renderer only, and inert unless cheats are permitted.
//
// This header is the engine-facing surface; everything it needs is called from a handful of hooks
// (see features/hitboxviz/README.md). The geometry and bookkeeping live in computation/ so they are
// unit-tested off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_H
#define ZX_HITBOXVIZ_H

#include "doomtype.h"
#include "features/hitboxviz/computation/boxedges_compute.h"   // [rc4l] Vertex3

class AActor;

namespace zx { namespace hitboxviz {

// --- renderer hooks --------------------------------------------------------

// Resets the per-frame geometry and re-evaluates the cheat gate once for the whole frame.
// Called from FGLRenderer::CreateScene.
void BeginFrame();

// Offers one actor to the overlay. Called from RenderThings during BSP traversal, which reaches
// actors the sprite path culls (spriteless, +INVISIBLE, fully translucent) -- those still have
// collision boxes worth seeing. Cheap no-op when the overlay is off.
void CollectActor(AActor *thing);

// Emits everything collected this frame. Called at the tail of FGLRenderer::RenderTranslucent,
// where the 3D projection is still current and the depth buffer is writable again.
void Draw();

// [rc4l] What a renderer that collects its own frame (the Vulkan backend) reads.

// [rc4l] One colour's edges as world-space line pairs, and the colour as 0xRRGGBB.
struct LineBatch
{
	const Vertex3 *verts;
	unsigned int count;
	unsigned int rgb;
};
// [rc4l] The collision boxes, the attack boxes and the blasts, drawn in that order as GL draws them.
enum { kLineBatches = 3 };

// [rc4l] The player's view's boxes, kept apart from GL's nested scenes: every thing in the given sectors (the walk's, as GL's RenderThings offers the things of each sector its walk reached) seen from viewActor, and the live blasts.
void CollectBackendFrame(const void *viewActor, const int *sectorList, int count);

// [rc4l] What CollectBackendFrame gathered, the non-empty batches of it into out (kLineBatches long), whether they ignore depth, and the line width in pixels; how many.
int BackendLines(LineBatch *out, bool &xray, float &widthPx);

// --- explosion regions -----------------------------------------------------

// Records a blast for the next second or so. Fed by P_RadiusAttack directly when the local machine
// simulates the explosion, and by SVC2_DEBUGEXPLOSION when a client is being told about the
// server's authoritative one.
void PushBlast(fixed_t x, fixed_t y, fixed_t z, int distance, int fulldamagedistance);

// Drops every recorded blast. Called on level setup so regions cannot survive a map change,
// reconnect or demo seek and be drawn at coordinates belonging to a different level.
void ClearBlasts();

// True when the server should be broadcasting explosion debug info -- i.e. sv_debugexplosions is on
// and cheats are enabled. Also gates the debug-only replication of the attack extent, so neither
// costs a production server anything. Server-side test; do not use it to decide whether to draw.
bool ServerDebugActive();

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_H
