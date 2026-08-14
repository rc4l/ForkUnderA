// [rc4l] See gamemodetable_compute.h. Pure logic only -- unit-tested at 100% coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/restart-reparse/computation/gamemodetable_compute.h"

namespace zx { namespace gamemodetable {

unsigned long ComputeGameModeFlags( unsigned long flags, bool add, unsigned long bit )
{
	return add ? ( flags | bit ) : ( flags & ~bit );
}

bool ComputeHasExactlyOneOf( unsigned long flags, unsigned long mask )
{
	const unsigned long bits = flags & mask;
	// Clearing the lowest set bit leaves zero only when there was exactly one.
	return ( bits != 0 ) && (( bits & ( bits - 1 )) == 0 );
}

GameModeDefect ComputeGameModeDefect( bool hasName, bool hasShortName, unsigned long flags,
                                      unsigned long gameTypeMask, unsigned long earnTypeMask )
{
	if ( hasName == false )
		return GameModeDefect::NoName;
	if ( hasShortName == false )
		return GameModeDefect::NoShortName;
	if ( ComputeHasExactlyOneOf( flags, gameTypeMask ) == false )
		return GameModeDefect::AmbiguousGameType;

	const unsigned long earnBits = flags & earnTypeMask;
	if ( earnBits == 0 )
		return GameModeDefect::NoEarnType;
	if (( earnBits & ( earnBits - 1 )) != 0 )
		return GameModeDefect::MultipleEarnTypes;

	return GameModeDefect::None;
}

}} // namespace zx::gamemodetable
