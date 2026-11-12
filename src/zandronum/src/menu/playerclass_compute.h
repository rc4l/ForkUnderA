// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player setup menu's class list holds only the classes the local player may use (zandronum@469e5f569641); these map its rows to classes.

#ifndef ZX_PLAYERCLASS_COMPUTE_H
#define ZX_PLAYERCLASS_COMPUTE_H

namespace zx { namespace playerclass {

// [rc4l] What a row maps to when it names no class at all, which leaves the player's class alone.
const int NO_CLASS = -2;

// [rc4l] The class after `current` the random preview shows, skipping forbidden ones; with none allowed it steps on as before, so it never spins.
template <class Allowed> int ComputeNextPreviewClass(int current, int count, Allowed allowed)
{
	if (count <= 0)
		return 0;
	if (current < 0 || current >= count)
		current = count - 1;
	for (int step = 1; step <= count; step++)
	{
		const int c = (current + step) % count;
		if (allowed(c))
			return c;
	}
	return (current + 1) % count;
}

// [rc4l] The class a menu row picks, -1 for Random, NO_CLASS for a row past the allowed classes.
template <class Allowed> int ComputeClassOfMenuRow(int row, bool noRandom, int count, Allowed allowed)
{
	if (!noRandom && row == 0)
		return -1;
	int want = noRandom ? row : row - 1;
	for (int c = 0; c < count; c++)
	{
		if (!allowed(c))
			continue;
		if (want-- == 0)
			return c;
	}
	return NO_CLASS;
}

// [rc4l] The row that shows a class among the allowed ones; Random, and a class the list leaves out, show the first row.
template <class Allowed> int ComputeMenuRowOfClass(int playerClass, bool noRandom, int count, Allowed allowed)
{
	if (playerClass < 0 || playerClass >= count || !allowed(playerClass))
		return 0;
	int row = noRandom ? 0 : 1;
	for (int c = 0; c < playerClass; c++)
	{
		if (allowed(c))
			row++;
	}
	return row;
}

// [rc4l] Whether opening the menu must move the player off their class: a forbidden class, or Random where the game offers none.
template <class Allowed> bool ComputeClassNeedsReplacing(int playerClass, bool noRandom, int count, Allowed allowed)
{
	if (playerClass < 0)
		return noRandom;
	return playerClass >= count || !allowed(playerClass);
}

}} // namespace zx::playerclass

#endif // ZX_PLAYERCLASS_COMPUTE_H
