// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/playerskin_compute.h"

namespace zx { namespace skin {

bool ComputeClSkinsForbids(int clSkins, bool cheatSkin)
{
	return clSkins <= 0 || (clSkins >= 2 && cheatSkin);
}

int ComputeShownSkin(int userSkin, int numSkins, bool forbidden, int baseSkin)
{
	if (forbidden || userSkin < 0 || userSkin >= numSkins)
		return baseSkin;
	return userSkin;
}

bool ComputeSkinSpriteApplies(bool morphed, bool noSkin)
{
	return !morphed || !noSkin;
}

}} // namespace zx::skin
