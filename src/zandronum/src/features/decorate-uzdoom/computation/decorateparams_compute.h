// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_DECORATEPARAMS_COMPUTE_H
#define ZX_DECORATEPARAMS_COMPUTE_H

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] A_Explode's XF_ flags, as UZDoom's constants.zs numbers them.
enum ExplodeFlags
{
	kXfHurtSource = 1,
	kXfNotMissile = 4,
	kXfExplicitDamageType = 8,
	kXfNoSplash = 16,
};

// [Doruk-Aksoy] P_RadiusAttack's RADF_ flags A_Explode's XF_ ones become, which share no bits past the first three.
enum RadiusFlags
{
	kRadfHurtSource = 1,
	kRadfSourceIsSpot = 4,
};

// [Doruk-Aksoy] The RADF_ flags for A_Explode's XF_ ones, as UZDoom maps them, so XF_EXPLICITDAMAGETYPE (8) never reads as RADF_NODAMAGE (8).
int ComputeExplodeRadiusFlags(int xfFlags);

// [Doruk-Aksoy] Whether A_Explode takes the actor's own damage type: when none is named and XF_EXPLICITDAMAGETYPE does not hold "none".
bool ComputeExplodeUsesOwnDamageType(bool damageTypeIsNone, int xfFlags);

// [Doruk-Aksoy] UZDoom's A_JumpIf*Closer test: nearer than distance across the ground and, unless noz, between the two bodies vertically.
bool ComputeCloserThan(double distance2D, double selfZ, double selfTop, double targetZ, double targetTop, double distance, bool noz);

// [Doruk-Aksoy] A_SetScale's vertical scale: scaley, or scalex for a 0 unless usezero keeps the 0.
double ComputeSetScaleY(double scaleX, double scaleY, bool useZero);

// [Doruk-Aksoy] Which label A_JumpIfHigherOrLower takes.
enum HigherOrLower
{
	kNeither,
	kHigher,
	kLower,
};

// [Doruk-Aksoy] UZDoom's A_JumpIfHigherOrLower: higher when the other's feet are above self's top (or feet) plus offsetHigh, else lower when its top (or feet) is below self's feet plus offsetLow.
HigherOrLower ComputeHigherOrLower(double selfZ, double selfHeight, double otherZ, double otherHeight, double offsetHigh, double offsetLow,
	bool includeHeight, bool hasHigh, bool hasLow);

// [Doruk-Aksoy] A_CheckBlock's CBF_ flags, as UZDoom numbers them.
enum CheckBlockFlags
{
	kCbfNoLines = 1 << 0,
	kCbfSetTarget = 1 << 1,
	kCbfSetMaster = 1 << 2,
	kCbfSetTracer = 1 << 3,
	kCbfSetOnPtr = 1 << 4,
	kCbfDropoff = 1 << 5,
	kCbfNoActors = 1 << 6,
	kCbfAbsolutePos = 1 << 7,
	kCbfAbsoluteAngle = 1 << 8,
};

// [Doruk-Aksoy] Whether A_CheckBlock finds the spot blocked: by an actor unless CBF_NOACTORS, a line unless CBF_NOLINES, or a failed dropoff check.
bool ComputeCheckBlockResult(bool movePassed, bool blockedByActor, bool blockedByLine, int flags);

}} // namespace zx::decorateuzdoom

#endif // ZX_DECORATEPARAMS_COMPUTE_H
