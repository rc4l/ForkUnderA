// [rc4l] See zx_playerclassfallback.h. Engine glue around the pure decision in
// computation/playerclasspick_compute.*
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "d_player.h"
#include "gi.h"
#include "team.h"

#include "features/playerclass-fallback/zx_playerclassfallback.h"
#include "features/playerclass-fallback/computation/playerclasspick_compute.h"

int ZX_PlayerClassFallback( int storedClass, bool bOnTeam, unsigned int ulTeam )
{
	std::vector<zx::playerclass::ClassCandidate> candidates;
	candidates.reserve( PlayerClasses.Size( ));

	for ( unsigned int i = 0; i < PlayerClasses.Size( ); ++i )
	{
		zx::playerclass::ClassCandidate candidate;
		candidate.hiddenFromMenu = !!( PlayerClasses[i].Flags & PCF_NOMENU );
		candidate.allowedForTeam = ( bOnTeam == false ) || TEAM_IsClassAllowedForTeam( i, ulTeam );
		candidates.push_back( candidate );
	}

	const zx::playerclass::Pick pick = zx::playerclass::ComputePlayerClassPick( storedClass,
		gameinfo.norandomplayerclass, candidates );

	// Only a fallback overrides the caller. Stored choices and permitted rolls are left to the
	// existing code so this cannot change behaviour anywhere it wasn't already broken.
	return ( pick.kind == zx::playerclass::PickKind::FirstEligible ) ? pick.index : -1;
}

