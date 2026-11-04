// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A model's pose and frame blend read from the actor and its MODELDEF, shared by gl_RenderModel and the backend's derived models.

#ifndef ZX_MODELBUILD_H
#define ZX_MODELBUILD_H

#include <vector>

namespace zx { namespace gamecopy { struct ActorRecord; } }
namespace zx { namespace levelmesh { struct MeshVertex; } }
struct PClass;
struct FState;
struct FSpriteModelFrame;

struct FModelSurface;

namespace zx { namespace sprites {

struct ModelPose;

// [rc4l] What gl_RenderModel turns into the model's matrix, for an actor standing at (x, y, z) after interpolation and bob.
void ModelPoseFor(const zx::gamecopy::ActorRecord &actor, const FSpriteModelFrame *smf, float x, float y, float z, ModelPose &pose);

// [rc4l] The frame a model blends toward and how far, as gl_RenderFrameModels finds them; NULL when it does not blend.
FSpriteModelFrame *NextModelFrame(const FSpriteModelFrame *smf, const FState *curState, int curTics, const PClass *ti, double &inter);

// [rc4l] Model slot `i` of frame `smf` as GL draws it: its surfaces toward `smfNext` where their frames differ, how many, and in `blend` how far between.
int ModelSlotSurfaces(const FSpriteModelFrame *smf, int i, const FSpriteModelFrame *smfNext, double inter, FModelSurface *surfs, float &blend);

// [rc4l] Every surface of a model's frame into the model capture already begun, blended toward the next frame: gl_RenderFrameModels without GL.
void CaptureModelFrames(const FSpriteModelFrame *smf, const FState *curState, int curTics, const PClass *ti, int translation);

// [Doruk-Aksoy] A surface ResolveModelFrames resolved: its material and translation, and its corners in the caller's list, none for one whose corners were refused.
struct ResolvedModelSurface
{
	const void *material;
	int translation;
	unsigned first, count;
};

// [Doruk-Aksoy] CaptureModelFrames without the capture, so a lane may run it: each surface's corners under modelToWorld appended to verts and listed in surfaces; false, with neither grown, when a slot's geometry or a skin's material is not made yet, which only the main thread may do.
bool ResolveModelFrames(const FSpriteModelFrame *smf, const FState *curState, int curTics, const PClass *ti, int translation, const float *modelToWorld,
	std::vector<ResolvedModelSurface> &surfaces, std::vector<zx::levelmesh::MeshVertex> &verts);

}} // namespace zx::sprites

#endif // ZX_MODELBUILD_H
