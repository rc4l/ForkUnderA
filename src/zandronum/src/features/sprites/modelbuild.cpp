// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/modelbuild.h"

#include "features/sprites/computation/modelpose_compute.h"
#include "features/levelmesh/modelmesh.h"      // [rc4l] CaptureModelSurface
#include "features/materials/materials.h"

#include "c_console.h"
#include "c_cvars.h"
#include "doomstat.h"
#include "g_level.h"
#include "i_system.h"
#include "r_utility.h"
#include "actor.h"
#include "info.h"

// [rc4l] GL's model data and settings the derivation still reads, until model data leaves gl/ (P5).
#include "features/models/models.h"   // [rc4l] gl_FindModelFrame and the models
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads

EXTERN_CVAR(Bool, gl_interpolate_model_frames)

namespace zx { namespace sprites {

void ModelPoseFor(const AActor *actor, const FSpriteModelFrame *smf, float x, float y, float z, ModelPose &p)
{
	const float angle = ANGLE2FLOAT(actor->angle);
	float pitch = 0, roll = 0;
	if (smf->flags & MDL_PITCHFROMMOMENTUM)
		pitch = ComputeMomentumPitch(static_cast<double>(actor->velx), static_cast<double>(actor->vely), static_cast<double>(actor->velz), angle);
	// [rc4l] With both flags the actor's pitch adds to the momentum's.
	if (smf->flags & MDL_INHERITACTORPITCH) pitch += ComputeInheritedDegrees(actor->pitch.Raw());
	if (smf->flags & MDL_INHERITACTORROLL) roll += ComputeInheritedDegrees((long long)actor->roll);

	// [rc4l] Model space is GL's (x, up, y).
	p.position[0] = x; p.position[1] = z; p.position[2] = y;
	p.yaw = (smf->flags & MDL_ALIGNANGLE) ? -ANGLE2FLOAT(R_PointToAngle(actor->x, actor->y)) : -angle;
	if (smf->flags & MDL_ALIGNPITCH)
		p.pitch = ComputeAlignPitch(FIXED2FLOAT(actor->z - viewz), FIXED2FLOAT(R_PointToDist2(actor->x - viewx, actor->y - viewy)));
	else p.pitch = pitch;
	p.roll = (smf->flags & MDL_ROLLAGAINSTANGLE)
		? ComputeRollAgainstAngle(ANGLE2FLOAT(R_PointToAngle(actor->x, actor->y)), angle, actor->z > viewz) : -roll;

	p.rotating = (smf->flags & MDL_ROTATING) != 0;
	p.spin = p.rotating ? ComputeModelSpin(smf->rotationSpeed, (float)I_MSTime() * (float)TICRATE / 1000.0f) : 0.f;
	p.spinCenter[0] = smf->rotationCenterX; p.spinCenter[1] = smf->rotationCenterY; p.spinCenter[2] = smf->rotationCenterZ;
	p.spinAxis[0] = smf->xrotate; p.spinAxis[1] = smf->yrotate; p.spinAxis[2] = smf->zrotate;

	// [rc4l] A sprite's y scale is its height, so the model's up takes the actor's scaleY and both flat axes its scaleX.
	p.scale[0] = FIXED2FLOAT(actor->scaleX) * smf->xscale;
	p.scale[1] = FIXED2FLOAT(actor->scaleY) * smf->zscale;
	p.scale[2] = FIXED2FLOAT(actor->scaleX) * smf->yscale;
	p.offset[0] = smf->xoffset / smf->xscale;
	p.offset[1] = smf->zoffset / smf->zscale;
	p.offset[2] = smf->yoffset / smf->yscale;
	p.yawOffset = -ANGLE2FLOAT(smf->angleoffset);
	p.pitchOffset = smf->pitchoffset;
	p.rollOffset = -smf->rolloffset;
	// [rc4l] Voxels carry the pixel stretch in their aspect factor; every other model has it taken out.
	p.stretch = (smf->models[0] != NULL ? smf->models[0]->getAspectFactor() : 1.f) / glset.pixelstretch;
}

FSpriteModelFrame *NextModelFrame(const FSpriteModelFrame *smf, const FState *curState, int curTics, const PClass *ti, double &inter)
{
	inter = 0.;
	if (!gl_interpolate_model_frames || (smf->flags & MDL_NOINTERPOLATION)) return NULL;
	FState *nextState = curState->GetNextState();
	if (curState == nextState || !nextState) return NULL;
	// [rc4l] [BB]/[JM] Tic fractions blend past 35 fps, but not while the tic counter is frozen.
	float ticFraction = 0.;
	if (ConsoleState == c_up && menuactive != MENU_On && !(level.flags2 & LEVEL2_FROZEN)) ticFraction = FIXED2FLOAT(I_GetTimeFrac(NULL));
	// [rc4l] [BB] Most Doom monsters show a frame twice in a row in their see state.
	bool before = false, after = false;
	if (smf->flags & MDL_INTERPOLATEDOUBLEDFRAMES)
	{
		const FState *prevState = curState - 1;
		before = ti->ActorInfo->OwnsState(prevState) && curState->sprite == prevState->sprite && curState->Frame == prevState->Frame;
		after = curState->sprite == nextState->sprite && curState->Frame == nextState->Frame;
	}
	inter = ComputeModelInter(curState->Tics, curTics, ticFraction, before, after);
	if (inter == 0.0) return NULL;
	if (after) nextState = nextState->GetNextState();
	return gl_FindModelFrame(ti, nextState->sprite, nextState->GetFrame(), false);
}

int ModelSlotSurfaces(const FSpriteModelFrame *smf, int i, const FSpriteModelFrame *smfNext, double inter, FModelSurface *surfs, float &blend)
{
	FModel *mdl = smf->models[i];
	const bool differ = smfNext != NULL && smf->modelframes[i] != smfNext->modelframes[i];
	blend = ComputeModelSlotBlend(differ, mdl->Interpolates(), inter);
	return mdl->FrameSurfaces(smf->skins[i], smf->modelframes[i], differ ? smfNext->modelframes[i] : smf->modelframes[i], surfs);
}

void CaptureModelFrames(const FSpriteModelFrame *smf, const FState *curState, int curTics, const PClass *ti, int translation)
{
	double inter = 0.;
	const FSpriteModelFrame *smfNext = NextModelFrame(smf, curState, curTics, ti, inter);
	for (int i = 0; i < MAX_MODELS_PER_FRAME; i++)
	{
		FModel *mdl = smf->models[i];
		if (mdl == NULL) continue;
		mdl->PushSpriteMDLFrame(smf, i);
		mdl->BuildGeometry();
		FModelSurface surfs[MD3_MAX_SURFACES];
		float blend;
		const int n = ModelSlotSurfaces(smf, i, smfNext, inter, surfs, blend);
		for (int k = 0; k < n; k++)
			zx::levelmesh::CaptureModelSurface(&mdl->mGeometry, surfs[k].base1, surfs[k].base2, blend,
				surfs[k].firstIndex, surfs[k].count, zx::materials::MaterialForTexture(surfs[k].skin), translation);
	}
}

}} // namespace zx::sprites
