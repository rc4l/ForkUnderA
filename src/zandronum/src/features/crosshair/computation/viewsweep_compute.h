// [MobiusU9B] The view sweep that finds what the crosshair's line enters, kept engine-free so the actor name and telemeter are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_VIEWSWEEP_COMPUTE_H
#define ZX_VIEWSWEEP_COMPUTE_H

#include <cstdint>
#include <vector>

namespace zx { namespace crosshair {

// [MobiusU9B] How far the identify scan and the actor name reach in map units, the stock MISSILERANGE.
const int kIdentifyRange = 2048;

// [MobiusU9B] How far the telemeter reaches in map units, as far as hitscan weapons (PLAYERMISSILERANGE).
const int kTelemeterRange = 8192;

// [MobiusU9B] A line out of the eyes: where it starts and which way it goes.
struct SweepRay
{
	double x;
	double y;
	double z;
	double dx;
	double dy;
	double dz;
};

// [MobiusU9B] An actor's box: x/y is its centre and z its bottom, like the engine's.
struct SweepBox
{
	double x;
	double y;
	double z;
	double radius;
	double height;
};

// [MobiusU9B] Where a ray enters a box, in lengths of its direction, which are map units for a unit direction.
struct SweepEntry
{
	bool hit;
	double distance;
};

// [MobiusU9B] What the sweep needs to know about one actor; isPlayer is any player body, a leftover one included.
struct SweepActor
{
	bool isViewer;
	bool invisible;
	bool inBlockmap;
	bool isMissile;
	bool firedByViewer;
	bool isPlayer;
	bool isMonster;
	bool isItem;
};

// [MobiusU9B] What kind of thing an actor counts as, for the telemeter's toggles.
enum SweepKind
{
	kSweepDecoration,
	kSweepMonster,
	kSweepPlayer,
	kSweepProjectile,
	kSweepItem,
};

// [MobiusU9B] One actor the ray entered: how far away, and whether the telemeter may measure it.
struct SweepHit
{
	double distance;
	bool measurable;
};

// [MobiusU9B] A direction in the world: x and y across the map, z up.
struct SweepVector
{
	double x;
	double y;
	double z;
};

// [MobiusU9B] How a renderer maps screen pixels to the world: focal lengths in pixels per unit of slope at a level view, and whether it looks up and down by shearing (software) or by turning a world stretched by stretch (GL).
struct ReticleProjection
{
	double focalX;
	double focalY;
	double stretch;
	bool shear;
};

// [MobiusU9B] A BAM angle, where a full turn is 2^32, as radians in [-pi, pi).
double ComputeBamRadians(int64_t bam);

// [MobiusU9B] GL's main-view viewport height: the whole screen from screenblocks 10 up, which the renderer centres on the 3D view, else the shrunken window rounded down to 8.
int ComputeGLViewportHeight(int screenblocks, int screenHeight);

// [MobiusU9B] GL's main-view projection: fovDegrees across a 4:3 frame (or the screen, when taller than 4:3), aspect the screen's width over height, and its viewport and pixel stretch.
ReticleProjection ComputeGLReticleProjection(double fovDegrees, double aspect, int viewportWidth, int viewportHeight, double stretch);

// [MobiusU9B] The unit direction through the screen point offsetX/offsetY pixels right of and below the view's centre, for a view turned yaw and pitched pitchUp radians.
SweepVector ComputeReticleDirection(double yaw, double pitchUp, double offsetX, double offsetY, const ReticleProjection &projection);

// [MobiusU9B] Where ray first enters box within maxDistance; a box behind, around the eye or without size is missed.
SweepEntry ComputeRayBoxEntry(const SweepRay &ray, const SweepBox &box, double maxDistance);

// [MobiusU9B] How far ray goes before it leaves a box it starts inside, such as the viewer's own body; 0 when it starts outside.
double ComputeRayBoxExit(const SweepRay &ray, const SweepBox &box);

// [MobiusU9B] Whether the sweep looks at an actor: not the viewer, drawn, and in the blockmap unless it is someone else's projectile.
bool ComputeSweepConsiders(const SweepActor &actor);

// [MobiusU9B] The actor's kind, in order: player, monster, projectile, item, then decoration.
SweepKind ComputeSweepKind(const SweepActor &actor);

// [MobiusU9B] Index of the nearest hit within maxDistance, measurable ones only if asked, the first on a tie, or -1.
int ComputeNearestSweepHit(const std::vector<SweepHit> &hits, bool measurableOnly, double maxDistance);

// [MobiusU9B] Whether the sweep must run again: a new tic or a different viewer.
bool ComputeSweepDue(int lastTic, int nowTic, bool sameViewer);

} } // namespace zx::crosshair

#endif
