// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The view state the sprite derivation needs, without naming a renderer.
//
// GLSprite::Process and ProcessParticle reach into GLRenderer for four things: the camera actor, the
// view vector, a running sprite index, and the portal being drawn through. Those four references are
// the whole of what ties sprite DERIVATION -- deciding what a sprite looks like -- to OpenGL. The
// lighting it depends on does not: gl_SetColor, gl_SetFog and gl_SetDynSpriteLight contain no GL
// calls at all, and FRenderState only reaches the API inside Apply().
//
// So this is an interface rather than a copy of the state. Mirroring the fields into a struct and
// filling it each frame was the obvious shape and the wrong one: mViewActor alone is assigned in six
// places, most of them inside portal recursion, and a second copy would drift the first time someone
// added a seventh. Reading through to whoever is drawing cannot.
//
// GL registers its implementation at renderer startup. A backend without GL registers its own, and
// the derivation moves without being rewritten.

#ifndef ZX_SPRITEVIEW_H
#define ZX_SPRITEVIEW_H

#include "doomtype.h"

class AActor;
class FTexture;

namespace zx { namespace hwrender {

struct SpriteViewProvider
{
	// The actor the view is attached to -- not drawn, because you are inside it.
	AActor *(*ViewActor)();
	// Where the camera faces, in the plane, for turning a wall sprite side-on.
	void    (*ViewVector)(float &x, float &y);
	// A running count, which sprites use to keep a stable order among themselves.
	int     (*NextSpriteIndex)();
	// Portal questions. Both answer harmlessly when no portal is being drawn, which is every frame
	// the standalone path renders today -- portals are not ported yet, and this is where they attach.
	bool    (*PortalRejectsPoint)(fixed_t x, fixed_t y);
	bool    (*PortalIsMirror)();
	// How many dynamic lights the frame has, which decides whether a sprite is lit per-pixel at all.
	int     (*DynamicLightCount)();
	// The camera's yaw and pitch in degrees, for billboarding.
	void    (*ViewAngles)(float &yaw, float &pitch);
	// The two particle textures, chosen by gl_particles_style. Either may be NULL.
	FTexture *(*ParticleTexture)(int style);
};

// Register whoever is drawing. Passing NULL restores the do-nothing provider.
void SetSpriteViewProvider(const SpriteViewProvider *p);

// Never NULL: before anyone registers, this answers "no camera actor, no portal".
const SpriteViewProvider &SpriteView();

}} // namespace zx::hwrender

#endif // ZX_SPRITEVIEW_H
