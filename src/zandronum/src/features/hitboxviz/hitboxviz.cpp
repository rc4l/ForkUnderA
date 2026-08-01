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

// [MGOOOOOO] gl_system.h must come first: it defines __GL_PCH_H, which is what exposes
// FFlatVertexBuffer's drawing methods (they need the system GL includes and the header cannot pull
// those in itself without #define clashes). Same ordering the gl/ translation units use.
#include "gl/system/gl_system.h"

#include "actor.h"
#include "c_cvars.h"
#include "c_dispatch.h"   // CCMD
#include "c_console.h"    // Printf
#include "d_player.h"     // players, consoleplayer
#include "doomstat.h"     // gametic
#include "network.h"      // NETWORK_GetState, NETWORK_InClientMode
#include "r_utility.h"    // r_TicFrac
#include "templates.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/data/gl_vertexbuffer.h"

#include "features/hitboxviz/hitboxviz.h"
#include "features/hitboxviz/computation/blastrecords_compute.h"
#include "features/hitboxviz/computation/boxedges_compute.h"
#include "features/hitboxviz/computation/vizgate_compute.h"

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
	// A single GL_LINES batch is written straight into the flat VBO's streaming region. GetBuffer()
	// is unchecked and the buffer only keeps 500 vertices of slack past its wrap threshold, so
	// batches are chunked well under that. 480 == 20 whole boxes, and is even, so no chunk boundary
	// ever splits an edge in half.
	const unsigned int VERTS_PER_DRAW = 480;

	// Ceiling on per-frame geometry. A scene dense enough to hit this is already unreadable; drop
	// the surplus rather than grow without bound.
	const unsigned int MAX_FRAME_VERTS = 200 * 1024;

	TArray<Vertex3> s_physicalVerts;
	TArray<Vertex3> s_attackVerts;
	TArray<Vertex3> s_blastVerts;

	// [MGOOOOOO] Scenes nest: DrawScene runs CreateScene, then GLPortal::EndFrame -- which re-enters
	// DrawScene once per visible portal (gl_portal.cpp:696/790/845/957) -- and only then
	// RenderTranslucent. So collection for the outer view is already done when an inner view starts
	// collecting, and a single flat buffer would let the outer view's boxes be drawn inside the
	// portal (and then leave nothing for the outer view itself). Each scene instead marks where its
	// own geometry starts and truncates back to that mark once drawn, which makes the pairing
	// correct at any nesting depth.
	struct SceneMark
	{
		unsigned int physical, attack, blast;
	};
	TArray<SceneMark> s_marks;

	BlastRecordStore s_blasts;

	// Re-evaluated once per scene rather than per actor: a value that changed mid-frame would draw
	// an inconsistent overlay.
	bool s_frameActive = false;

	// "Nobody else is in this game": not a client of someone's server, and not a server ourselves.
	// This is the same condition CheckCheatmode uses to permit iddqd offline, and it deliberately
	// includes NETSTATE_SINGLE_MULTIPLAYER -- an offline game merely emulating multiplayer (bots,
	// an offline deathmatch) is still one machine with no rules to subvert.
	//
	// NETWORK_InClientMode() is also true while a client demo plays back, which is what we want:
	// a recorded netgame keeps the netgame's gate rather than inheriting the viewer's offline
	// status.
	inline bool IsOfflineGame()
	{
		return ( NETWORK_InClientMode( ) == false ) && ( NETWORK_GetState( ) != NETSTATE_SERVER );
	}

	// GL_ALIASED_LINE_WIDTH_RANGE, queried lazily on first draw.
	bool  s_lineRangeKnown = false;
	float s_lineWidthMin = 1.f;
	float s_lineWidthMax = 1.f;

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

	// Emits verts[begin .. end) as GL_LINES, chunked to stay inside the VBO's slack.
	void DrawBatch(const TArray<Vertex3> &verts, unsigned int begin, PalEntry colour)
	{
		if (begin >= verts.Size())
			return;

		gl_RenderState.SetColorAlpha(colour, 1.f);
		gl_RenderState.Apply();

		unsigned int emitted = begin;
		while (emitted < verts.Size())
		{
			const unsigned int remaining = verts.Size() - emitted;
			const unsigned int chunk = remaining < VERTS_PER_DRAW ? remaining : VERTS_PER_DRAW;

			FFlatVertex *ptr = GLRenderer->mVBO->GetBuffer();
			for (unsigned int i = 0; i < chunk; ++i)
			{
				const Vertex3 &v = verts[emitted + i];
				// FFlatVertex::Set takes (x, vertical, y) -- the middle argument is world z.
				ptr->Set(v.x, v.z, v.y, 0.f, 0.f);
				++ptr;
			}
			GLRenderer->mVBO->RenderCurrent(ptr, GL_LINES);

			emitted += chunk;
		}
	}

	// Turns the surviving blast records into wireframe: the outer region actually tested by
	// P_RadiusAttack, the inner full-damage region, and a cross marking the blast's own height.
	void BuildBlastGeometry()
	{
		for (unsigned int i = 0; i < s_blasts.Count(); ++i)
		{
			const BlastRecord &record = s_blasts.Get(i);

			const BlastPrism outer = ComputeBlastPrism(record.x, record.y, record.z,
				static_cast<float>(record.distance));
			AppendBox(s_blastVerts, outer.centerX, outer.centerY, outer.bottomZ, outer.radius, outer.height);

			const int full = ClampFullDamageDistance(record.distance, record.fulldamagedistance);
			if (full > 0)
			{
				const BlastPrism inner = ComputeBlastPrism(record.x, record.y, record.z,
					static_cast<float>(full));
				AppendBox(s_blastVerts, inner.centerX, inner.centerY, inner.bottomZ, inner.radius, inner.height);
			}

			Vertex3 marker[PLANE_MARKER_VERTS];
			AppendVerts(s_blastVerts, marker, BuildPlaneMarker(record.x, record.y, record.z,
				outer.radius, marker));
		}
	}
}

void BeginFrame()
{
	SceneMark mark;
	mark.physical = s_physicalVerts.Size();
	mark.attack   = s_attackVerts.Size();
	mark.blast    = s_blastVerts.Size();
	s_marks.Push(mark);

	// Offline, the toggle alone is enough -- there is no server to protect and cheats are already
	// permitted (iddqd works there with sv_cheats off). Online, sv_cheats is the sole authority.
	// See vizgate_compute.h.
	s_frameActive = ShouldDraw(!!cl_fua_hitbox, !!sv_cheats, IsOfflineGame());
}

void CollectActor(AActor *thing)
{
	if (!s_frameActive || thing == NULL)
		return;

	// The actor the camera is attached to. Skipping it matches what the sprite path does, and in
	// plain first person it only hides a wireframe you are standing inside -- but it also hides the
	// box of whoever a spectator is following, since spectating attaches the camera to that
	// player's body. That is precisely the box you want when watching someone, so this is a toggle
	// rather than an unconditional skip.
	if (thing == GLRenderer->mViewActor && !cl_fua_hitbox_self)
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

	AppendBox(s_physicalVerts, cx, cy, cz,
		FIXED2FLOAT(thing->radius), FIXED2FLOAT(thing->height));

	// The attack box is only worth drawing where it actually differs from the movement box --
	// otherwise every actor would be outlined twice in two colours.
	if (cl_fua_hitbox_attackbox)
	{
		const fixed_t attackRadius = thing->GetAttackRadius();
		const fixed_t attackHeight = thing->GetAttackHeight();

		if (attackRadius != thing->radius || attackHeight != thing->height)
		{
			AppendBox(s_attackVerts, cx, cy, cz,
				FIXED2FLOAT(attackRadius), FIXED2FLOAT(attackHeight));
		}
	}
}

void Draw()
{
	// The mark must be popped on every path out of here, or a scene that draws nothing would leave
	// the stack unbalanced and desynchronise every subsequent scene.
	if (s_marks.Size() == 0)
		return;

	const SceneMark mark = s_marks[s_marks.Size() - 1];
	s_marks.Pop();

	struct Truncate
	{
		const SceneMark &m;
		~Truncate()
		{
			s_physicalVerts.Resize(m.physical);
			s_attackVerts.Resize(m.attack);
			s_blastVerts.Resize(m.blast);
		}
	} truncate = { mark };
	(void)truncate;

	if (!s_frameActive)
		return;

	s_blasts.ExpireBefore(gametic);
	if (cl_fua_hitbox_explosions)
		BuildBlastGeometry();

	if (s_physicalVerts.Size() == mark.physical &&
		s_attackVerts.Size() == mark.attack &&
		s_blastVerts.Size() == mark.blast)
	{
		return;
	}

	if (!s_lineRangeKnown)
	{
		// Wide lines are optional in a core profile; an out-of-range width raises GL_INVALID_VALUE
		// and is ignored, so clamp to what the driver admits to supporting.
		GLfloat range[2] = { 1.f, 1.f };
		glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, range);
		s_lineWidthMin = range[0];
		s_lineWidthMax = range[1];
		s_lineRangeKnown = true;
	}

	const bool xray = !!cl_fua_hitbox_xray;

	gl_RenderState.EnableTexture(false);
	if (xray)
		glDisable(GL_DEPTH_TEST);
	glLineWidth(ResolveLineWidth(cl_fua_hitbox_linewidth, s_lineWidthMin, s_lineWidthMax));

	DrawBatch(s_physicalVerts, mark.physical, PalEntry(uint32(cl_fua_hitbox_color)));
	DrawBatch(s_attackVerts,   mark.attack,   PalEntry(uint32(cl_fua_hitbox_attackcolor)));
	DrawBatch(s_blastVerts,    mark.blast,    PalEntry(uint32(cl_fua_hitbox_blastcolor)));

	glLineWidth(1.f);
	if (xray)
		glEnable(GL_DEPTH_TEST);
	gl_RenderState.EnableTexture(true);
	gl_RenderState.SetColorAlpha(PalEntry(0xffffffff), 1.f);
	gl_RenderState.Apply();
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

	s_blasts.Push(record);
}

void ClearBlasts()
{
	s_blasts.Clear();
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
