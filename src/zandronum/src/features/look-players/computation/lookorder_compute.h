// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] A random order over the players in the game, drawn one player at a time, for a monster looking for a target.
#ifndef ZX_LOOKORDER_COMPUTE_H
#define ZX_LOOKORDER_COMPUTE_H

// [Doruk-Aksoy] The most slots an order holds, the engine's MAXPLAYERS, which p_enemy.cpp checks against.
constexpr int kZxLookSlots = 64;

// [Doruk-Aksoy] The players a look has not tried yet, in no particular order.
struct ZxLookOrder
{
	int left[kZxLookSlots];
	int count;
};

// [Doruk-Aksoy] A random number in [0, range), the engine's M_Random(range).
typedef int (*ZxLookDraw)(int range);

// [Doruk-Aksoy] Starts an order over every slot below slots (at most kZxLookSlots) whose inGame entry is true.
void ComputeLookOrderStart(ZxLookOrder &order, const bool *inGame, int slots);

// [Doruk-Aksoy] The next player to try, each untried one equally likely, or -1 once every one was tried; one draw per player, none for the last.
int ComputeLookOrderNext(ZxLookOrder &order, ZxLookDraw draw);

#endif
