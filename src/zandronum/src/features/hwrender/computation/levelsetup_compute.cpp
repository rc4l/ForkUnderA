// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/levelsetup_compute.h"

namespace zx { namespace hwrender {

LevelSetup ComputeLevelSetupStart()
{
	LevelSetup s = { -1, kSetupArm, 0, 0 };
	return s;
}

void ComputeLevelSetupForLevel(LevelSetup &s, int generation)
{
	if (generation == s.generation) return;
	s.generation = generation;
	s.state = kSetupArm;
	s.wait = 0;
	s.tries = 0;
}

void ComputeLevelSetupArmed(LevelSetup &s)
{
	s.state = kSetupWaiting;
	s.wait = 0;
}

bool ComputeLevelSetupWaited(LevelSetup &s)
{
	return ++s.wait >= kSetupWaitFrames;
}

void ComputeLevelSetupUploaded(LevelSetup &s, bool ok)
{
	if (ok) s.state = kSetupReady;
	else s.state = ++s.tries < kSetupMaxTries ? kSetupArm : kSetupGaveUp;
}

bool ComputeStandaloneBakeStep(StandaloneBake &b, bool standalone, bool levelReady, int settleFrames)
{
	if (!standalone)
	{
		b.baked = false;
		b.armed = 0;
		return false;
	}
	if (!levelReady || b.baked) return false;
	const bool arm = b.armed == 0;
	if (++b.armed > settleFrames) b.baked = true;
	return arm;
}

}} // namespace zx::hwrender
