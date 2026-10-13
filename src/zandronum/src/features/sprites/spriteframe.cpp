// [rc4l] GL's sprite frame lookup, unchanged.

#include "features/sprites/spriteframe.h"
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
		if (rot==-1)
		{
			if (sprframe->Texture[0] == sprframe->Texture[1])
			{
				rot = (ang + (angle_t)(ANGLE_45/2)*9) >> 28;
			}
			else
			{
				rot = (ang + (angle_t)(ANGLE_45/2)*9-(angle_t)(ANGLE_180/16)) >> 28;
			}
		}
		if (mirror) *mirror = !!(sprframe->Flip&(1<<rot));
		return sprframe->Texture[rot];
	}
}


