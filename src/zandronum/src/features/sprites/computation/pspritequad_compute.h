// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] DrawPSprite and DrawPlayerSprites' arithmetic: where a weapon layer lands on the screen, and the light the weapon is drawn in.

#ifndef ZX_PSPRITEQUAD_COMPUTE_H
#define ZX_PSPRITEQUAD_COMPUTE_H

namespace zx { namespace sprites {

// [rc4l] What DrawPSprite places a layer from: its offset, the frame's sprite rectangle, the layer's scale, the view window and the screen's shape.
struct PSpriteQuadIn
{
	float sx, sy;                                    // [rc4l] the layer's offset with the bob, in 320x200 units
	float rectLeft, rectTop, rectWidth, rectHeight;  // [rc4l] the material's sprite rectangle
	float scaleX, scaleY;                            // [rc4l] A_OverlayScale's
	float viewWidth, viewHeight;                     // [rc4l] viewwidth and viewheight
	float windowX, windowY;                          // [rc4l] viewwindowx and viewwindowy
	float widescreenRatio;
	bool  tallerThanWide;                            // [rc4l] AspectTallerThanWide
	float yAdjust;                                   // [rc4l] how far the weapon's YAdjust lowers it, 0 for none
	float screenWidth, screenHeight;                 // [rc4l] SCREENWIDTH and SCREENHEIGHT
	bool  mirror;                                    // [rc4l] PSPF_MIRROR, reflecting the layer about the view's middle
};

struct PSpriteQuad
{
	float x1, y1, x2, y2;                            // [rc4l] screen pixels, the window offset included
};

// [rc4l] The layer's screen rectangle, false when it lies wholly off the left or right of the view.
bool ComputePSpriteQuad(const PSpriteQuadIn &in, PSpriteQuad &out);

// [rc4l] Light mode 8's weapon light: the light the software renderer's curve gives a level gl_CalcLightLevel has already raised.
int ComputeSoftWeaponLight(int calcLevel);

// [rc4l] A fullbright layer showing brightens the whole weapon a third of the way to full, except in light mode 8.
int ComputeBrightenedWeaponLight(int lightLevel, bool anyBright, int lightMode);

}} // namespace zx::sprites

#endif // ZX_PSPRITEQUAD_COMPUTE_H
