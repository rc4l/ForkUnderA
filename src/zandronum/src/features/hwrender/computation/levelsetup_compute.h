// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's set-up of a level as steps: bake it from the map, upload, retry a failed upload, give up after enough.

#ifndef ZX_LEVELSETUP_COMPUTE_H
#define ZX_LEVELSETUP_COMPUTE_H

namespace zx { namespace hwrender {

enum { kSetupArm = 0, kSetupBaked = 1, kSetupReady = 2, kSetupGaveUp = 3 };

// [rc4l] Uploads tried before giving up for the level.
const int kSetupMaxTries = 8;

struct LevelSetup
{
	int generation;   // [rc4l] the level the set-up is for
	int state;        // [rc4l] kSetup*
	int tries;        // [rc4l] uploads that failed
};

// [rc4l] No level set up yet.
LevelSetup ComputeLevelSetupStart();

// [rc4l] A new level (its generation changed) starts the set-up over.
void ComputeLevelSetupForLevel(LevelSetup &s, int generation);

// [rc4l] The level is baked: upload it next.
void ComputeLevelSetupBaked(LevelSetup &s);

// [rc4l] The upload's outcome: ready, armed again for another try, or given up after kSetupMaxTries.
void ComputeLevelSetupUploaded(LevelSetup &s, bool ok);

}} // namespace zx::hwrender

#endif // ZX_LEVELSETUP_COMPUTE_H
