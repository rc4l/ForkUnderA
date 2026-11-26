// [rc4l] A sprite frame's texture for a rotation, and whether it is drawn mirrored: moved here from gl/data/gl_data.cpp, which GL's sprites and weapon call it from, as do the backend's and the light list.

#ifndef ZX_SPRITEFRAME_H
#define ZX_SPRITEFRAME_H

#include "doomtype.h"
#include "tables.h"
#include "textures/textures.h"

FTextureID gl_GetSpriteFrame(unsigned sprite, int frame, int rot, angle_t ang, bool *mirror, bool spriteFlip = false);

// [Doruk-Aksoy] An actor's rotation for a view angle relative to it, and whether it is drawn mirrored once its XFLIP and SPRITEFLIP render flags count, as UZDoom.
FTextureID gl_GetActorSpriteFrame(unsigned sprite, int frame, angle_t ang, DWORD renderflags, bool *mirror);

#endif // ZX_SPRITEFRAME_H
