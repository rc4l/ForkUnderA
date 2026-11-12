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

// [rc4l] Ported from zandronum@4ab1128e5b738dd57134d283dd78d0adebb90a5e: an unmorphed body always takes its skin's sprite, a morphed one unless its class has NOSKIN.
bool ComputeSkinSpriteApplies(bool morphed, bool noSkin);

}} // namespace zx::skin

#endif // ZX_PLAYERSKIN_COMPUTE_H
