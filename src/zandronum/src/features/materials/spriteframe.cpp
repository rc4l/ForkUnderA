// [rc4l] GL's sprite frame lookup, its rotation from the computation the derived sprites share.

#include "features/materials/spriteframe.h"
#include "features/sprites/computation/spriteframe_compute.h"   // [rc4l] ComputeSpriteRotation
#include "actor.h"   // [Doruk-Aksoy] RF_XFLIP, RF_SPRITEFLIP
#include "r_data/sprites.h"
#include "r_state.h"   // [rc4l] sprites

//===========================================================================
//
//  Gets the texture index for a sprite frame
//
//===========================================================================

FTextureID gl_GetSpriteFrame(unsigned sprite, int frame, int rot, angle_t ang, bool *mirror, bool spriteFlip)
{
	spritedef_t *sprdef = &sprites[sprite];
	if (frame >= sprdef->numframes)
	{
		// If there are no frames at all for this sprite, don't draw it.
		return FNullTextureID();
	}
	else
	{
		//picnum = SpriteFrames[sprdef->spriteframes + thing->frame].Texture[0];
		// choose a different rotation based on player view
		spriteframe_t *sprframe = &SpriteFrames[sprdef->spriteframes + frame];
		bool flip;
		// [rc4l] The rotation the viewer sees, by the rule the backend's sprites share.
		if (rot==-1) rot = zx::sprites::ComputeSpriteRotation(ang, sprframe->Texture[0] == sprframe->Texture[1], sprframe->Flip, flip, spriteFlip);
		else flip = !!(sprframe->Flip&(1<<rot));
		if (mirror) *mirror = flip;
		return sprframe->Texture[rot];
	}
}

FTextureID gl_GetActorSpriteFrame(unsigned sprite, int frame, angle_t ang, DWORD renderflags, bool *mirror)
{
	const bool spriteFlip = (renderflags & RF_SPRITEFLIP) != 0;
	bool rotationMirror = false;
	const FTextureID patch = gl_GetSpriteFrame(sprite, frame, -1, ang, &rotationMirror, spriteFlip);
	if (mirror) *mirror = zx::sprites::ComputeSpriteDrawnMirror(rotationMirror, (renderflags & RF_XFLIP) != 0, spriteFlip);
	return patch;
}
