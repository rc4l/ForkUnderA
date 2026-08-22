// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/spriteview.h"

namespace zx { namespace hwrender {

namespace {

AActor *NullViewActor() { return NULL; }
void    NullViewVector(float &x, float &y) { x = 0.f; y = 0.f; }
int     NullSpriteIndex() { return 0; }
bool    NullPortalRejects(fixed_t, fixed_t) { return false; }
bool    NullPortalIsMirror() { return false; }
int     NullDynamicLightCount() { return 0; }
void    NullViewAngles(float &yaw, float &pitch) { yaw = 0.f; pitch = 0.f; }
FTexture *NullParticleTexture(int) { return NULL; }

// [rc4l] The default answers "there is no camera and no portal", which is safe rather than merely
// convenient: a sprite is dropped when it IS the view actor and when a portal rejects it, so a
// provider that has not been registered yet keeps every sprite instead of losing all of them.
const SpriteViewProvider g_null =
{
	NullViewActor, NullViewVector, NullSpriteIndex, NullPortalRejects, NullPortalIsMirror,
	NullDynamicLightCount, NullViewAngles, NullParticleTexture
};

const SpriteViewProvider *g_provider = &g_null;

} // namespace

void SetSpriteViewProvider(const SpriteViewProvider *p)
{
	g_provider = (p != NULL) ? p : &g_null;
}

const SpriteViewProvider &SpriteView() { return *g_provider; }

}} // namespace zx::hwrender
