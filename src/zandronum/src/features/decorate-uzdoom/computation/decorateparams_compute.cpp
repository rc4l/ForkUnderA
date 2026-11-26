// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/computation/decorateparams_compute.h"

namespace zx { namespace decorateuzdoom {

int ComputeExplodeRadiusFlags(int xfFlags)
{
	int radf = 0;
	if (xfFlags & kXfHurtSource)
		radf |= kRadfHurtSource;
	if (xfFlags & kXfNotMissile)
		radf |= kRadfSourceIsSpot;
	return radf;
}

bool ComputeExplodeUsesOwnDamageType(bool damageTypeIsNone, int xfFlags)
{
	return damageTypeIsNone && !(xfFlags & kXfExplicitDamageType);
}

bool ComputeCloserThan(double distance2D, double selfZ, double selfTop, double targetZ, double targetTop, double distance, bool noz)
{
	if (!(distance2D < distance))
		return false;
	if (noz)
		return true;
	return selfZ > targetZ ? selfZ - targetTop < distance : targetZ - selfTop < distance;
}

double ComputeSetScaleY(double scaleX, double scaleY, bool useZero)
{
	return (scaleY != 0 || useZero) ? scaleY : scaleX;
}

HigherOrLower ComputeHigherOrLower(double selfZ, double selfHeight, double otherZ, double otherHeight, double offsetHigh, double offsetLow,
	bool includeHeight, bool hasHigh, bool hasLow)
{
	if (hasHigh && otherZ > (includeHeight ? selfHeight : 0) + selfZ + offsetHigh)
		return kHigher;
	if (hasLow && otherZ + (includeHeight ? otherHeight : 0) < selfZ + offsetLow)
		return kLower;
	return kNeither;
}

bool ComputeCheckBlockResult(bool movePassed, bool blockedByActor, bool blockedByLine, int flags)
{
	if (movePassed)
		return false;
	return (!(flags & kCbfNoActors) && blockedByActor) || (!(flags & kCbfNoLines) && blockedByLine) || (flags & kCbfDropoff) != 0;
}

}} // namespace zx::decorateuzdoom
