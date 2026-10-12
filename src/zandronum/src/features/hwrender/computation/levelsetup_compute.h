// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's set-up of a level as steps: arm the bake, let it run for a few frames, upload, retry a failed upload, give up after enough; and the standalone bake's arming.

#ifndef ZX_LEVELSETUP_COMPUTE_H
#define ZX_LEVELSETUP_COMPUTE_H

namespace zx { namespace hwrender {

enum { kSetupArm = 0, kSetupWaiting = 1, kSetupReady = 2, kSetupGaveUp = 3 };

// [rc4l] Frames the bake gets before the upload is tried, and uploads tried before giving up for the level.
const int kSetupWaitFrames = 4;
const int kSetupMaxTries = 8;

struct LevelSetup
{
	int generation;   // [rc4l] the level the set-up is for
	int state;        // [rc4l] kSetup*
	int wait;         // [rc4l] frames waited for the bake
	int tries;        // [rc4l] uploads that failed
};

// [rc4l] No level set up yet.
LevelSetup ComputeLevelSetupStart();

// [rc4l] A new level (its generation changed) starts the set-up over.
void ComputeLevelSetupForLevel(LevelSetup &s, int generation);

// [rc4l] The bake was armed: wait from the next frame.
void ComputeLevelSetupArmed(LevelSetup &s);

// [rc4l] One frame of waiting; true once the bake has had kSetupWaitFrames and the upload should be tried.
bool ComputeLevelSetupWaited(LevelSetup &s);

// [rc4l] The upload's outcome: ready, armed again for another try, or given up after kSetupMaxTries.
void ComputeLevelSetupUploaded(LevelSetup &s, bool ok);

// [rc4l] The level's bake under standalone, which runs once per switch-on: armed, then counted to settled.
struct StandaloneBake
{
	bool baked;
	int armed;
};

// [rc4l] One frame of it: true when the full bake must be armed now; switching standalone off forgets it.
bool ComputeStandaloneBakeStep(StandaloneBake &b, bool standalone, bool levelReady);

}} // namespace zx::hwrender

#endif // ZX_LEVELSETUP_COMPUTE_H
