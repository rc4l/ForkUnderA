// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] What a map-placed actor started with, kept off the actor for the map reset (g_game.cpp) that reads it back.
#ifndef ZX_MAPRESETSTATE_H
#define ZX_MAPRESETSTATE_H

#include "basictypes.h"
#include "tarray.h"
#include "r_data/renderstyle.h"

struct FStrifeDialogueNode;
class PSymbolVariable;

// [Doruk-Aksoy] Zeros, as every actor's saved fields read before they left the actor, for one P_SpawnMapThing never set.
struct ZxMapResetState
{
	BYTE SavedSpecial = 0;	// [Doruk-Aksoy] A byte, as it always was: a special past 255 resets truncated.
	int SavedArgs[5] = {};
	int SavedTID = 0;
	TMap<PSymbolVariable *, int> savedUserVars;
	fixed_t SavedPitch = 0, SavedRoll = 0;
	fixed_t SavedScaleX = 0, SavedScaleY = 0;
	FRenderStyle SavedRenderStyle = MakeZeroStyle();
	fixed_t SavedAlpha = 0;
	DWORD SavedFillColor = 0;
	fixed_t SavedGravity = 0;
	int SavedScore = 0;
	int SavedHealth = 0;
	FStrifeDialogueNode *SavedConversation = nullptr;
	BYTE SavedFloatBobPhase = 0;

	// [Doruk-Aksoy] A style of all zero bits, which the saved style was before any map spawn set it.
	static FRenderStyle MakeZeroStyle() { FRenderStyle s; s.AsDWORD = 0; return s; }
};

#endif
