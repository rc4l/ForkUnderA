// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The weapon and its overlays derived as DrawPlayerSprites and DrawPSprite draw them, into the 2D capture rather than recorded from GL.

#ifndef ZX_PSPRITEBUILD_H
#define ZX_PSPRITEBUILD_H

#include "basictypes.h"   // [rc4l] fixed_t

struct sector_t;
class AActor;

namespace zx { namespace hwrender { struct Quad2D; } }

namespace zx { namespace sprites {

// [rc4l] Whether the weapon is derived here, so GL's DrawPSprite draws without recording (fua_psprites_own, the check off).
bool PSpritesOwned();

// [rc4l] The weapon's layers below the targeter for a view in `viewsector`, given the frame's weapon bob, which P_BobWeapon smooths per call and so is computed once by the caller.
void BuildPlayerSprites(sector_t *viewsector, int viewArea, fixed_t bobX, fixed_t bobY);

// [rc4l] The same for a frame with no GL renderer to hand over its bob, which it works out itself.
void BuildPlayerSpritesBobbed(sector_t *viewsector, int viewArea);

// [rc4l] The targeter's three layers, drawn only from the player's own eye (`viewActor`).
void BuildTargeterSprites(AActor *viewActor);

// [rc4l] With fua_psprite_check on, a quad GL's DrawPSprite recorded, so the derivation can be compared against it.
void NoteGLPSprite(const zx::hwrender::Quad2D &q);

// [rc4l] Compare the frame's derived weapon against GL's; called once both are in.
void CheckFramePSprites();

}} // namespace zx::sprites

#endif // ZX_PSPRITEBUILD_H
