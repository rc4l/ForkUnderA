// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] gl_RenderModel's matrix and gl_RenderFrameModels' frame blend: where a model stands, how it turns, and how far it is into its next frame.

#ifndef ZX_MODELPOSE_COMPUTE_H
#define ZX_MODELPOSE_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] The arguments gl_RenderModel hands VSMatrix, in its order; vectors are GL's (x, up, y) and angles are degrees.
struct ModelPose
{
	float position[3];                // [rc4l] the actor's interpolated position
	float yaw, pitch, roll;           // [rc4l] about up, about the model's z and about its x, signs as GL passes them
	bool  rotating;                   // [rc4l] MDL_ROTATING
	float spin;                       // [rc4l] the spin's angle now
	float spinCenter[3], spinAxis[3];
	float scale[3];
	float offset[3];                  // [rc4l] MODELDEF's offsets, already divided by its scales
	float yawOffset, pitchOffset, rollOffset;
	float stretch;                    // [rc4l] the aspect factor over the pixel stretch, applied to up
};

// [rc4l] The model-to-world matrix, column-major, multiplied as VSMatrix multiplies.
void ComputeModelMatrix(const ModelPose &pose, float out[16]);

// [rc4l] MDL_PITCHFROMMOMENTUM: the climb of the velocity, flipped when the model moves backwards, from any consistent units.
float ComputeMomentumPitch(double velx, double vely, double velz, float angleDegrees);

// [rc4l] MDL_INHERITACTORPITCH and ROLL's degrees from a raw 16.16 angle.
float ComputeInheritedDegrees(long long raw);

// [rc4l] MDL_ROTATING's angle now, from its speed and the time in tics.
float ComputeModelSpin(float rotationSpeed, float timeTics);

// [rc4l] MDL_ROLLAGAINSTANGLE: the roll that keeps a beam model edge-on to the eye.
float ComputeRollAgainstAngle(float pointAngleDegrees, float actorAngleDegrees, bool aboveEye);

// [rc4l] MDL_ALIGNPITCH: the pitch from the eye to the actor.
float ComputeAlignPitch(float dz, float distance);

// [rc4l] How far into the next frame a model is; `doubledBefore`/`doubledAfter` are MDL_INTERPOLATEDOUBLEDFRAMES finding the state repeated behind or ahead.
double ComputeModelInter(int stateTics, int ticsLeft, float ticFraction, bool doubledBefore, bool doubledAfter);

}} // namespace zx::sprites

#endif // ZX_MODELPOSE_COMPUTE_H
