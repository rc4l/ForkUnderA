// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for how a bouncing actor's bounce ends, which line it bounced off, and what a client leaves to the server.
#ifndef ZX_BOUNCE_COMPUTE_H
#define ZX_BOUNCE_COMPUTE_H

namespace zx { namespace bounce {

// [Doruk-Aksoy] Mirrors actor.h's BOUNCE_Walls, BOUNCE_Floors and BOUNCE_Ceilings, static_asserted where the engine includes this.
enum { ZX_BOUNCE_Walls = 1 << 0, ZX_BOUNCE_Floors = 1 << 1, ZX_BOUNCE_Ceilings = 1 << 2 };

// [Doruk-Aksoy] Takes one bounce off a limited count and says whether it was the last; a count of 0 is unlimited and stays so.
bool ComputeBounceSpent(int &bouncecount);

// [Doruk-Aksoy] Whether a blocked move tries a wall bounce at all: a wall, or a floor or ceiling it ran into with no line in the way.
bool ComputeTriesWallBounce(int bounceFlags);

// [Doruk-Aksoy] Whether these bounce flags bounce off a ceiling (ceiling true) or a floor.
bool ComputeBouncesOffPlane(int bounceFlags, bool ceiling);

// [Doruk-Aksoy] What an actor coming down on another does: it bounced, or a missile explodes on it, or anything else lands on it.
enum class OnTop { Bounced, Explode, Land };
OnTop ComputeOnTopResult(bool bounced, bool isMissile);

// [Doruk-Aksoy] Whether a client leaves a missile's actor collisions to the server: a bouncer it does not own, whose every bounce off and hit on an actor the server sends with its position.
bool ComputeClientLeavesActorsToServer(bool serverOwned, bool isMissile, int bounceFlags);

// [Doruk-Aksoy] How directly a velocity runs into a line of this direction: its speed across the line, 0 for a line of no length.
double ComputeHeadOn(double lineDx, double lineDy, double velX, double velY);

// [Doruk-Aksoy] Whether a refusing line with this head-on speed replaces the one kept so far, the first refusing line standing on a tie.
bool ComputeKeepsRefusedLine(bool haveOne, double keptHeadOn, double headOn);

// [Doruk-Aksoy] The line a blocked move ran into: the refusing line it ran into most directly, else the one that raised the floor it is below, else the one that lowered the ceiling its top is above, else the last the line walk named.
template <class Line>
Line *ComputeBlockedLine(Line *refused, Line *raisedFloor, Line *loweredCeiling, Line *lastNamed, bool belowFloor, bool aboveCeiling)
{
	if (refused != nullptr)
		return refused;
	if (belowFloor && raisedFloor != nullptr)
		return raisedFloor;
	if (aboveCeiling && loweredCeiling != nullptr)
		return loweredCeiling;
	return lastNamed;
}

}}

#endif
