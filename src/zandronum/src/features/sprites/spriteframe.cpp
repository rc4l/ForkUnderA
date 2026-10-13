// [rc4l] GL's sprite frame lookup, its rotation from the computation the derived sprites share.

#include "features/sprites/spriteframe.h"
#include "features/sprites/computation/spriteframe_compute.h"   // [rc4l] ComputeSpriteRotation
#include "r_data/sprites.h"
#include "r_state.h"   // [rc4l] sprites

//===========================================================================
//
//  Gets the texture index for a sprite frame
//
//===========================================================================

FTextureID gl_GetSpriteFrame(unsigned sprite, int frame, int rot, angle_t ang, bool *mirror)
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
		if (rot==-1) rot = zx::sprites::ComputeSpriteRotation(ang, sprframe->Texture[0] == sprframe->Texture[1], sprframe->Flip, flip);
		else flip = !!(sprframe->Flip&(1<<rot));
		if (mirror) *mirror = flip;
		return sprframe->Texture[rot];
	}
}


