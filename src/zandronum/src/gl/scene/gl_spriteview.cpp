// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's answers to the sprite derivation's questions about the view.
//
// The five functions the derivation needs, in one place, so gl_sprite.cpp holds no reference to
// GLRenderer at all. See features/hwrender/spriteview.h for why this is an interface and not a
// snapshot of the state.

#include "gl/system/gl_system.h"
#include "features/hwrender/spriteview.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/scene/gl_portal.h"
#include "actor.h"

namespace {

AActor *GLViewActor() { return GLRenderer->mViewActor; }

void GLViewVector(float &x, float &y)
{
	x = GLRenderer->mViewVector.X;
	y = GLRenderer->mViewVector.Y;
}

int GLNextSpriteIndex() { return GLRenderer->gl_spriteindex++; }

bool GLPortalRejectsPoint(fixed_t x, fixed_t y)
{
	if (GLRenderer->mCurrentPortal == NULL) return false;
	return GLRenderer->mCurrentPortal->ClipPoint(x, y) == GLPortal::PClip_InFront;
}

bool GLPortalIsMirror()
{
	if (GLRenderer->mCurrentPortal == NULL) return false;
	return GLPortal::IsMirroring();
}

int GLDynamicLightCount() { return GLRenderer->mLightCount; }

void GLViewAngles(float &yaw, float &pitch)
{
	yaw = float(GLRenderer->mAngles.Yaw);
	pitch = float(GLRenderer->mAngles.Pitch);
}

FTexture *GLParticleTexture(int style)
{
	if (style == 1) return GLRenderer->glpart2;
	if (style == 2) return GLRenderer->glpart;
	return NULL;
}

const zx::hwrender::SpriteViewProvider g_glProvider =
{
	GLViewActor, GLViewVector, GLNextSpriteIndex, GLPortalRejectsPoint, GLPortalIsMirror,
	GLDynamicLightCount, GLViewAngles, GLParticleTexture
};

} // namespace

void gl_RegisterSpriteView()
{
	zx::hwrender::SetSpriteViewProvider(&g_glProvider);
}
