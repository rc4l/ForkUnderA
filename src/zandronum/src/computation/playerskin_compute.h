// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The skin a player's body shows under cl_skins, the rules behind PLAYER_SetSpriteToSkin and PLAYER_ShouldForceBaseSkin.

#ifndef ZX_PLAYERSKIN_COMPUTE_H
#define ZX_PLAYERSKIN_COMPUTE_H

namespace zx { namespace skin {

// [rc4l] Whether cl_skins keeps a player off their skin: 0 forbids every skin, 2 and up a cheat skin.
bool ComputeClSkinsForbids(int clSkins, bool cheatSkin);

// [rc4l] The skin a player shows: the base skin when cl_skins forbids theirs or theirs is out of range.
int ComputeShownSkin(int userSkin, int numSkins, bool forbidden, int baseSkin);

enum SkinSprite { SKIN_SPRITE_KEEP, SKIN_SPRITE_SKIN, SKIN_SPRITE_STATE };

// [rc4l] Ported from zandronum@ed337e36b30715fd381a82f0ca846adcae1b7c34: a body takes its skin's sprite only while it shows its spawn sprite, so a morph keeps its own, and NOSKIN puts back the state's sprite in place of a skin's.
SkinSprite ComputeSkinSprite(bool noSkin, bool showsSpawnSprite, bool showsTNT1, bool showsSkinSprite);

// [rc4l] Adapted from zandronum@e97ab4a28f1ab1c7a94404422da6bbf32d9c66c8: the sprite a skinned body is drawn with, its skin's while its state shows the spawn sprite and not TNT1, so a skin ACS gives a player standing still shows at once with its scale.
int ComputeDrawnSkinSprite(int drawnSprite, int skinSprite, bool stateShowsSpawnSprite, bool stateShowsTNT1);

}} // namespace zx::skin

#endif // ZX_PLAYERSKIN_COMPUTE_H
