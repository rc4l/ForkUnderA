// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#pragma once

// [Doruk-Aksoy] Cheaper chasing: far monsters move every other call, walkers remember blocked directions, and stuck monsters still reach their missile check.

#include <cstdint>

#include "features/chase-trim/computation/chasetrim_compute.h"

class AActor;

namespace zx { namespace chasetrim {

// [Doruk-Aksoy] Whether this chase call moves the monster and how far, from sv_fua_farchase and where every player views from; a skipped call counts movecount down itself.
FarStep ChaseStep(AActor *actor);

// [Doruk-Aksoy] The step multiple P_Move applies to Speed, 1 outside a chase call's movement.
int StepScale();

// [Doruk-Aksoy] One chase call's movement: sets StepScale, lets P_Move skip directions known blocked, and remembers this call's blocks if the walker stayed.
class ChaseMove
{
public:
	ChaseMove(AActor *actor, int scale);
	~ChaseMove();
private:
	int savedScale;
};

// [Doruk-Aksoy] A re-plan found no direction: with sv_fua_stuckattack the monster's missile check comes round again, and sv_fua_jamwait starts its wait.
void OnReplanFailed(AActor *actor);

// [Doruk-Aksoy] A sector moved, a line's blocking changed or a polyobject moved: remembered walls no longer hold.
void WorldChanged();

// [Doruk-Aksoy] Inside a ChaseMove, whether this call's movement waits out a jam (sv_fua_jamwait).
bool JamWaits(AActor *actor);

// [Doruk-Aksoy] Inside a ChaseMove, fails the re-plan outright when every direction is known blocked, as P_NewChaseDir would end; false when it must run.
bool SkipJammedReplan(AActor *actor);

// [Doruk-Aksoy] Inside a ChaseMove, fails the move in movedir at this step when that direction is known blocked; false when it must be tried.
bool FailKnownBlocked(AActor *actor, int speed);

// [Doruk-Aksoy] Inside a ChaseMove, after P_Move tried a move: notes a failure on one thing or on a wall, and forgets the memo once the walker moved.
void AfterMove(AActor *actor, int speed, bool moved, AActor *hardBlocker, bool wall, bool stayed);

} } // namespace zx::chasetrim
