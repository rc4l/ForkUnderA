// [rc4l] GL's FGLBitmap, moved from gl/textures/gl_bitmap.h: a true-colour bitmap that pixels are composed into under a translation, for any renderer.

#ifndef ZX_TEXTUREBITMAP_H
#define ZX_TEXTUREBITMAP_H

#include "textures/bitmap.h"


class FTextureBitmap : public FBitmap
{
	int translation;
public:

	FTextureBitmap()
	{
		translation = 0;
	}
	FTextureBitmap(BYTE *buffer, int pitch, int width, int height)
		: FBitmap(buffer, pitch, width, height)
	{
		translation = 0;
	}

	void SetTranslationInfo(int _trans)
	{
		if (_trans != -1337) translation = _trans;

	}

	virtual void CopyPixelDataRGB(int originx, int originy, const BYTE *patch, int srcwidth, 
								int srcheight, int step_x, int step_y, int rotate, int ct, FCopyInfo *inf = NULL);
	virtual void CopyPixelData(int originx, int originy, const BYTE * patch, int srcwidth, int srcheight, 
								int step_x, int step_y, int rotate, PalEntry * palette, FCopyInfo *inf = NULL);
};

#endif
