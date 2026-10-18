// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Every rule about a mark worked out once for walls, floors and ceilings alike: whether one may be made, which plane it rides, how many may stay and which random stream picks it.

#ifndef ZX_MARKRULES_COMPUTE_H
#define ZX_MARKRULES_COMPUTE_H

namespace zx { namespace decals {

// [rc4l] What made a mark: a shot's, a rail's, a missile's or a hit's blood (the impacts), or one the map, a script or a player's spray placed.
enum MarkSource { kSourceHitscan, kSourceRail, kSourceMissile, kSourceBlood, kSourcePlaced, kSourceScript, kSourceSpray };

// [rc4l] Where a hit landed.
enum MarkSurfaceKind { kSurfaceWall, kSurfaceFloor, kSurfaceCeiling };

// [rc4l] What the glue read of the surface a hit landed on: a noautodecals side (scrolling ones included), a Line_Horizon line, a two-sided line's open span with no texture, a sky or terrain liquid flat, a plane that is a portal or a scrolling one, a translucent 3D floor's plane or side, and a texture refusing decals (animated, warped, a door, TEXTURES NoDecals).
struct MarkSurface
{
	int kind;
	bool textureRefuses, sky, liquid, horizon, sideOptOut;
	bool open, portal, translucent, scrolls;
};

// [rc4l] One request: who made it and whether it is permanent, a map-placed decal that is never recycled.
struct MarkRequest { int source; bool permanent; };

// [rc4l] The settings and the machine's role: cl_maxdecals, cl_missiledecals, fua_decal_flats and whether this is a server.
struct MarkSettings { int maxDecals; bool missileDecals, flatMarks, server; };

// [rc4l] A mark's verdict: made, or the first rule that refused it.
enum MarkVerdict
{
	kMarkAllowed, kMarkServer, kMarkDecalsOff, kMarkMissileDecalsOff, kMarkFlatsOff, kMarkSideOptOut, kMarkHorizon,
	kMarkOpenSpan, kMarkSky, kMarkLiquid, kMarkPortal, kMarkTranslucent, kMarkScrolls, kMarkTextureRefuses, kMarkVerdicts
};

// [rc4l] Whether the source is an impact, which servers never mark and which every refusal applies to.
bool ComputeIsImpact(int source);

// [rc4l] Whether a request is refused before its template is chosen, as stock checks a server, cl_missiledecals and, for blood and sprays it finds by name, cl_maxdecals before it draws a group's choice.
bool ComputeRefusedBeforeChoice(const MarkRequest &request, const MarkSettings &settings);

// [rc4l] The surface's own refusals, the same for every source that obeys them, which the world pass also writes per pixel.
int ComputeSurfaceVerdict(const MarkSurface &surface);

// [rc4l] The verdict for a request on a surface: a permanent decal keeps stock's leave to ignore the budget, noautodecals and refusing textures, and only impacts are refused on a server or on a horizon line.
int ComputeMarkAllowed(const MarkSurface &surface, const MarkRequest &request, const MarkSettings &settings);

// [rc4l] A verdict's name, as fua_decal_report prints it.
const char *ComputeMarkVerdictName(int verdict);

// [rc4l] Whether a missile at rest with its bottom at z touches the floor or its top the ceiling, each within slack, the ceiling winning when it touches both; kSurfaceWall for neither.
int ComputeMissilePlaneHit(long long z, long long height, long long floorz, long long ceilingz, long long slack);

// [rc4l] Which of count plane heights is within slack of z, the nearest; -1 when none is, so the sector's own plane is the one hit.
int ComputePlaneOwner(long long z, const long long *heights, int count, long long slack);

// [rc4l] How many of count marks to retire, oldest first, before one more is made under a budget of max.
int ComputeMarksToEvict(int count, int max);

// [rc4l] Whether a template group's choice is drawn from the game's random stream: always on a wall, as stock does, and on a floor or ceiling only for the shots and missiles shipped builds already drew it for, so a recorded game's sequence is unchanged.
bool ComputeChoiceFromGameStream(int source, int kind);

}} // namespace zx::decals

#endif // ZX_MARKRULES_COMPUTE_H
