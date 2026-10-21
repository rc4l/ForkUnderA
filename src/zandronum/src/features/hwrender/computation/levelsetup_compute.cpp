// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/levelsetup_compute.h"

namespace zx { namespace hwrender {

LevelSetup ComputeLevelSetupStart()
{
	LevelSetup s = { -1, kSetupArm, 0 };
	return s;
}

void ComputeLevelSetupForLevel(LevelSetup &s, int generation)
{
	if (generation == s.generation) return;
	s.generation = generation;
	s.state = kSetupArm;
	s.tries = 0;
}

void ComputeLevelSetupBaked(LevelSetup &s)
{
	s.state = kSetupBaked;
}

void ComputeLevelSetupUploaded(LevelSetup &s, bool ok)
{
	if (ok) s.state = kSetupReady;
	else s.state = ++s.tries < kSetupMaxTries ? kSetupArm : kSetupGaveUp;
}

}} // namespace zx::hwrender
