// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
//
// [rc4l] GL's half of the hitbox overlay (hitboxviz.h): the boxes each GL scene collects, drawn as lines through the flat VBO.

// [MGOOOOOO] gl_system.h must come first: it defines __GL_PCH_H, which is what exposes
// FFlatVertexBuffer's drawing methods (they need the system GL includes and the header cannot pull
// those in itself without #define clashes). Same ordering the gl/ translation units use.
#include "gl/system/gl_system.h"

#include "actor.h"
#include "c_cvars.h"
#include "doomstat.h"     // gametic
#include "templates.h"

#include "gl/renderer/gl_renderer.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/data/gl_vertexbuffer.h"

#include "features/hitboxviz/hitboxviz.h"
#include "features/hitboxviz/hitboxviz_internal.h"
#include "features/hitboxviz/computation/vizgate_compute.h"

EXTERN_CVAR(Bool, sv_cheats)
EXTERN_CVAR(Bool, cl_fua_hitbox)
EXTERN_CVAR(Bool, cl_fua_hitbox_xray)
EXTERN_CVAR(Float, cl_fua_hitbox_linewidth)
EXTERN_CVAR(Color, cl_fua_hitbox_color)
EXTERN_CVAR(Color, cl_fua_hitbox_attackcolor)
EXTERN_CVAR(Color, cl_fua_hitbox_blastcolor)

namespace zx { namespace hitboxviz {

namespace
{
	// A single GL_LINES batch is written straight into the flat VBO's streaming region. GetBuffer()
	// is unchecked and the buffer only keeps 500 vertices of slack past its wrap threshold, so
	// batches are chunked well under that. 480 == 20 whole boxes, and is even, so no chunk boundary
	// ever splits an edge in half.
	const unsigned int VERTS_PER_DRAW = 480;

	TArray<Vertex3> s_physicalVerts;
	TArray<Vertex3> s_attackVerts;
	TArray<Vertex3> s_blastVerts;

	// [MGOOOOOO] Scenes nest: DrawScene runs CreateScene, then GLPortal::EndFrame -- which re-enters
	// DrawScene once per visible portal (gl_portal.cpp) -- and only then
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

	// Re-evaluated once per scene rather than per actor: a value that changed mid-frame would draw
	// an inconsistent overlay.
	bool s_frameActive = false;

	// GL_ALIASED_LINE_WIDTH_RANGE, queried lazily on first draw.
	bool  s_lineRangeKnown = false;
	float s_lineWidthMin = 1.f;
	float s_lineWidthMax = 1.f;

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
}	// anonymous namespace

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
	zx::gamecopy::ActorRecord record;
	zx::gamecopy::TakeActor(*thing, record);
	AppendActor(record, GLRenderer->mViewActor, s_physicalVerts, s_attackVerts);
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

	CollectBlasts(s_blastVerts);

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

}} // namespace zx::hitboxviz
