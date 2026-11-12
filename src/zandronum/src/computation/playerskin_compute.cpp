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

SkinSprite ComputeSkinSprite(bool noSkin, bool showsSpawnSprite, bool showsTNT1, bool showsSkinSprite)
{
	if (!noSkin)
		return (showsSpawnSprite && !showsTNT1) ? SKIN_SPRITE_SKIN : SKIN_SPRITE_KEEP;
	return showsSkinSprite ? SKIN_SPRITE_STATE : SKIN_SPRITE_KEEP;
}

int ComputeDrawnSkinSprite(int drawnSprite, int skinSprite, bool stateShowsSpawnSprite, bool stateShowsTNT1)
{
	return (drawnSprite != skinSprite && stateShowsSpawnSprite && !stateShowsTNT1) ? skinSprite : drawnSprite;
}

}} // namespace zx::skin
