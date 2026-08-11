// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/global-header/computation/menuresume_compute.h"

namespace zx
{

MenuSection ComputeMenuToOpen(const MenuResumeIn &in)
{
	// A finished join outranks being in a game as well as everything else: it is the one case where
	// the player has already been told, in as many words, where opening the menu will take them.
	if (in.joinReady)
		return MenuSection::Browser;

	if (in.inGame)
		return MenuSection::MainMenu;

	return in.lastShown;
}

} // namespace zx
