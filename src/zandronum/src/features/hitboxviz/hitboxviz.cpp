// [MGOOOOOO] See hitboxviz.h.
//
// Cross-reference note: Q-Zandronum's gl_show_hitbox (src/gl/scene/gl_sprite.cpp) is the only prior
// art for this in the Zandronum family, but its drawing code cannot be reused -- it is immediate
// mode (glBegin/glVertex3f/glColor3f, one draw call per edge) against a GZDoom-1.8-era renderer,
// and this engine's GL path is core-profile safe. UZDoom has nothing comparable at all: no
// world-space line primitive exists there, only a 2D automap radius square behind am_cheat 3.
// So the geometry is rebuilt here against the flat VBO, and three defects in the reference are
// deliberately not carried over: colours fed to glColor3f as 0-255 bytes (which clamp every channel
// to saturated), box coordinates taken un-interpolated (visibly lagging the sprite above 35fps),
// and one draw call per edge.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

// [rc4l] The overlay's geometry, its gate and the server's side, which every build has; GL's drawing is hitboxviz_gl.cpp, built only where GL is.

#include "actor.h"
#include "features/gamecopy/actors.h"   // [rc4l] CopyTic
#include "c_cvars.h"
#include "c_dispatch.h"   // CCMD
#include "c_console.h"    // Printf
#include "d_player.h"     // players, consoleplayer
#include "doomstat.h"     // gametic
#include "r_defs.h"       // sector_t
#include "r_state.h"      // sectors
#include "r_utility.h"    // r_TicFrac
#include "templates.h"

#include "features/hitboxviz/hitboxviz.h"
#include "features/hitboxviz/hitboxviz_internal.h"
#include "features/hitboxviz/computation/blastrecords_compute.h"
#include "features/hitboxviz/computation/boxedges_compute.h"
#include "features/hitboxviz/computation/vizgate_compute.h"
#include "features/handoff/effects.h"   // [rc4l] Deferred
#include "features/net-session/netrole.h"

EXTERN_CVAR(Bool, sv_cheats)

// [MGOOOOOO] cl_fua_* to match the existing FUA feature cvars. These are CVAR_ARCHIVE on purpose,
// unlike am_cheat: the cheat check happens at draw time, so the cvar is a view preference that is
// simply inert while cheats are refused, not cheat state that must not be persisted.
CVAR(Bool,  cl_fua_hitbox,            false,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool,  cl_fua_hitbox_actors,     true,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool,  cl_fua_hitbox_missiles,   true,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool,  cl_fua_hitbox_attackbox,  true,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool,  cl_fua_hitbox_explosions, true,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// [MGOOOOOO] Include the actor the camera is attached to -- your own body in first person, or the
// player you are following as a spectator. On by default: the followed player's box is the whole
// point of watching them.
CVAR(Bool,  cl_fua_hitbox_self,       true,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool,  cl_fua_hitbox_xray,       false,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, cl_fua_hitbox_linewidth,  2.f,      CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// [MGOOOOOO] Blue = the actor's physical collision box, green = its hurtbox (the attack extent at
// which it can be hit), red = an explosion's damage region.
CVAR(Color, cl_fua_hitbox_color,       0x0000ff, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Color, cl_fua_hitbox_attackcolor, 0x00ff00, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Color, cl_fua_hitbox_blastcolor,  0xff0000, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// [MGOOOOOO] Server-side switch for the explosion feed. CVAR_SERVERINFO so a client can tell whether
// the server is feeding it anything; NOSETBYACS because a mod must not be able to turn a debug
// broadcast on.
//
// Defaults to ON. It only ever does anything while sv_cheats is also true (see ServerDebugActive),
// and a cheats-enabled server is a test server by definition -- so making an admin opt in twice
// just meant explosions silently never appeared online. This cvar remains as the bandwidth escape
// hatch for a busy cheats-on server, not as the thing that enables the feature.
CVAR(Bool, sv_debugexplosions, true, CVAR_SERVERINFO | CVAR_NOSETBYACS)

namespace zx { namespace hitboxviz {

namespace
{
	// Ceiling on per-frame geometry. A scene dense enough to hit this is already unreadable; drop
	// the surplus rather than grow without bound.
	const unsigned int MAX_FRAME_VERTS = 200 * 1024;

	BlastRecordStore s_blasts;
	// [rc4l] The overlay draws the store every frame, so a blast a tic on the game thread makes is stored in the handoff.
	zx::handoff::Deferred<BlastRecord> s_blastsMade([](const BlastRecord &r) { s_blasts.Push(r); });

	// [rc4l] The backend's frame (CollectBackendFrame), in BackendLines' order, and whether it may draw.
	TArray<Vertex3> s_backendVerts[kLineBatches];
	bool s_backendActive = false;
	// [rc4l] The widest line the backend draws, which has no driver range to ask since it draws quads.
	const float BACKEND_MAX_LINE_WIDTH = 64.f;

	inline void AppendVerts(TArray<Vertex3> &dest, const Vertex3 *verts, unsigned int count)
	{
		if (count == 0 || dest.Size() + count > MAX_FRAME_VERTS)
			return;

		for (unsigned int i = 0; i < count; ++i)
			dest.Push(verts[i]);
	}

	void AppendBox(TArray<Vertex3> &dest, float cx, float cy, float bottomZ, float radius, float height)
	{
		Vertex3 verts[BOX_EDGE_VERTS];
		AppendVerts(dest, verts, BuildBoxEdges(cx, cy, bottomZ, radius, height, verts));
	}

	// Turns the surviving blast records into wireframe: the outer region actually tested by
	// P_RadiusAttack, the inner full-damage region, and a cross marking the blast's own height.
	void BuildBlastGeometry(TArray<Vertex3> &dest)
	{
		for (unsigned int i = 0; i < s_blasts.Count(); ++i)
		{
			const BlastRecord &record = s_blasts.Get(i);

			const BlastPrism outer = ComputeBlastPrism(record.x, record.y, record.z,
				static_cast<float>(record.distance));
			AppendBox(dest, outer.centerX, outer.centerY, outer.bottomZ, outer.radius, outer.height);

			const int full = ClampFullDamageDistance(record.distance, record.fulldamagedistance);
			if (full > 0)
			{
				const BlastPrism inner = ComputeBlastPrism(record.x, record.y, record.z,
					static_cast<float>(full));
				AppendBox(dest, inner.centerX, inner.centerY, inner.bottomZ, inner.radius, inner.height);
			}

			Vertex3 marker[PLANE_MARKER_VERTS];
			AppendVerts(dest, marker, BuildPlaneMarker(record.x, record.y, record.z,
				outer.radius, marker));
		}
	}
}	// anonymous namespace

// "Nobody else is in this game": not a client of someone's server, and not a server ourselves.
// This is the same condition CheckCheatmode uses to permit iddqd offline, and it deliberately
// includes NETSTATE_SINGLE_MULTIPLAYER -- an offline game merely emulating multiplayer (bots,
// an offline deathmatch) is still one machine with no rules to subvert.
//
// zx::net::InClientMode() is also true while a client demo plays back, which is what we want:
// a recorded netgame keeps the netgame's gate rather than inheriting the viewer's offline
// status.
bool IsOfflineGame()
{
	return ( zx::net::InClientMode() == false ) && ( !zx::net::IsServer() );
}

void AppendActor(const zx::gamecopy::ActorRecord &record, const void *viewActor, TArray<Vertex3> &physical, TArray<Vertex3> &attack)
{
	// The actor the camera is attached to. Skipping it matches what the sprite path does, and in
	// plain first person it only hides a wireframe you are standing inside -- but it also hides the
	// box of whoever a spectator is following, since spectating attaches the camera to that
	// player's body. That is precisely the box you want when watching someone, so this is a toggle
	// rather than an unconditional skip.
	const zx::gamecopy::ActorRecord *thing = &record;
	if (thing->who == viewActor && !cl_fua_hitbox_self)
		return;

	const bool isMissile = !!(thing->flags & MF_MISSILE);
	if (isMissile ? !cl_fua_hitbox_missiles : !cl_fua_hitbox_actors)
		return;

	// Interpolated, so the box tracks its sprite instead of stepping at the 35Hz tic rate.
	const fixed_t thingx = thing->PrevX + FixedMul(r_TicFrac, thing->x - thing->PrevX);
	const fixed_t thingy = thing->PrevY + FixedMul(r_TicFrac, thing->y - thing->PrevY);
	const fixed_t thingz = thing->PrevZ + FixedMul(r_TicFrac, thing->z - thing->PrevZ);

	const float cx = FIXED2FLOAT(thingx);
	const float cy = FIXED2FLOAT(thingy);
	const float cz = FIXED2FLOAT(thingz);

	AppendBox(physical, cx, cy, cz,
		FIXED2FLOAT(thing->radius), FIXED2FLOAT(thing->height));

	// The attack box is only worth drawing where it actually differs from the movement box --
	// otherwise every actor would be outlined twice in two colours.
	if (cl_fua_hitbox_attackbox)
	{
		const fixed_t attackRadius = thing->attackRadius;
		const fixed_t attackHeight = thing->attackHeight;

		if (attackRadius != thing->radius || attackHeight != thing->height)
		{
			AppendBox(attack, cx, cy, cz,
				FIXED2FLOAT(attackRadius), FIXED2FLOAT(attackHeight));
		}
	}
}

void CollectBlasts(TArray<Vertex3> &dest)
{
	s_blasts.ExpireBefore(zx::gamecopy::CopyTic());   // [rc4l] the tic the frame shows
	if (cl_fua_hitbox_explosions)
		BuildBlastGeometry(dest);
}

void CollectBackendFrame(const void *viewActor, const int *sectorList, int count)
{
	for (int k = 0; k < kLineBatches; k++)
		s_backendVerts[k].Clear();
	s_backendActive = ShouldDraw(!!cl_fua_hitbox, !!sv_cheats, IsOfflineGame());
	if (!s_backendActive)
		return;
	int records;
	const zx::gamecopy::ActorRecord *actors = zx::gamecopy::Actors(records);
	for (int i = 0; i < count; i++)
		for (int r = zx::gamecopy::FirstActorIn(&sectors[sectorList[i]]); r >= 0; r = actors[r].nextInSector)
			AppendActor(actors[r], viewActor, s_backendVerts[0], s_backendVerts[1]);
	CollectBlasts(s_backendVerts[2]);
}

int BackendLines(LineBatch *out, bool &xray, float &widthPx)
{
	xray = !!cl_fua_hitbox_xray;
	widthPx = ResolveLineWidth(cl_fua_hitbox_linewidth, 1.f, BACKEND_MAX_LINE_WIDTH);
	if (!s_backendActive)
		return 0;
	const unsigned int colours[kLineBatches] = { uint32(cl_fua_hitbox_color), uint32(cl_fua_hitbox_attackcolor), uint32(cl_fua_hitbox_blastcolor) };
	int n = 0;
	for (int k = 0; k < kLineBatches; k++)
	{
		if (s_backendVerts[k].Size() == 0)
			continue;
		out[n].verts = &s_backendVerts[k][0];
		out[n].count = s_backendVerts[k].Size();
		out[n].rgb = colours[k] & 0xffffff;
		n++;
	}
	return n;
}

void PushBlast(fixed_t x, fixed_t y, fixed_t z, int distance, int fulldamagedistance)
{
	if (distance <= 0)
		return;

	BlastRecord record;
	record.x = FIXED2FLOAT(x);
	record.y = FIXED2FLOAT(y);
	record.z = FIXED2FLOAT(z);
	record.distance = distance;
	record.fulldamagedistance = fulldamagedistance;
	record.expiryTic = gametic + BLAST_RECORD_LIFETIME_TICS;

	s_blastsMade.Make(record);
}

void ClearBlasts()
{
	s_blasts.Clear();
	s_blastsMade.Clear();
}

bool ServerDebugActive()
{
	return !!sv_debugexplosions && !!sv_cheats;
}

}} // namespace zx::hitboxviz

// [MGOOOOOO] Bindable toggle, mirroring the replay feature's fua_clip. Flipping the overlay without
// opening the menu is most of its value in a live firefight.
CCMD(fua_hitbox)
{
	cl_fua_hitbox = !cl_fua_hitbox;

	// sv_cheats is CVAR_LATCH, so telling the user to set it is not enough -- a mid-game change
	// only takes effect on the next map. Say so, rather than leaving them staring at an empty view.
	// Only relevant online: offline the gate never consults sv_cheats, so there is nothing to warn
	// about and the plain confirmation is the honest message.
	if (cl_fua_hitbox && sv_cheats == false && !zx::hitboxviz::IsOfflineGame())
		Printf("Hitbox overlay enabled, but it will not draw until sv_cheats is true (latched: takes effect next map).\n");
	else
		Printf("Hitbox overlay %s.\n", *cl_fua_hitbox ? "on" : "off");
}

// [MGOOOOOO] The colour cvars are CVAR_ARCHIVE, so anyone who ran an earlier build has the old
// defaults saved in their config and would never see the new ones. Give them a one-liner rather
// than making them hand-edit the ini.
CCMD(fua_hitbox_resetcolors)
{
	UCVarValue value;

	value.Int = 0x0000ff; cl_fua_hitbox_color.SetGenericRep(value, CVAR_Int);
	value.Int = 0x00ff00; cl_fua_hitbox_attackcolor.SetGenericRep(value, CVAR_Int);
	value.Int = 0xff0000; cl_fua_hitbox_blastcolor.SetGenericRep(value, CVAR_Int);

	Printf("Hitbox colours reset: collision box blue, hurtbox green, explosions red.\n");
}
