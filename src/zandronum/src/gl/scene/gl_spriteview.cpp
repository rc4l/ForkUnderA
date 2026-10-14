// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's answers to its own sprite code's questions about the view, in one place so gl_sprite.cpp never references GLRenderer.

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

const zx::hwrender::SpriteViewProvider g_glProvider =
{
	GLViewActor, GLViewVector, GLNextSpriteIndex, GLPortalRejectsPoint, GLPortalIsMirror,
	GLDynamicLightCount, GLViewAngles
};

} // namespace

const zx::hwrender::SpriteViewProvider &gl_SpriteView() { return g_glProvider; }
