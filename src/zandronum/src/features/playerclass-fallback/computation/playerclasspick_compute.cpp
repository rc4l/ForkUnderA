// [rc4l] See playerclasspick_compute.h. Pure logic only -- unit-tested at 100% coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/playerclass-fallback/computation/playerclasspick_compute.h"

#include <cstddef>

namespace zx { namespace playerclass {

Pick ComputePlayerClassPick( int storedClass, bool forbidRandom,
                             const std::vector<ClassCandidate> &candidates )
{
	const Pick roll = { PickKind::RollRandom, -1 };

	// No classes to choose from at all: leave the caller on its existing path rather than inventing
	// an index into an empty table.
	if ( candidates.empty( ))
		return roll;

	// A usable stored choice always wins, including one pointing at a hidden class -- that is an
	// explicit selection (ACS SetPlayerClass can make one), not a fallback.
	if (( storedClass >= 0 ) && ( storedClass < static_cast<int>( candidates.size( ))))
	{
		const Pick stored = { PickKind::Stored, storedClass };
		return stored;
	}

	if ( forbidRandom == false )
		return roll;

	for ( std::size_t i = 0; i < candidates.size( ); ++i )
	{
		if (( candidates[i].hiddenFromMenu == false ) && ( candidates[i].allowedForTeam ))
		{
			const Pick eligible = { PickKind::FirstEligible, static_cast<int>( i ) };
			return eligible;
		}
	}

	// Every class is hidden or forbidden to this team. Forcing one would put the player in a class
	// their team disallows, which is worse than the roll the engine already did here.
	return roll;
}


}} // namespace zx::playerclass
