// [rc4l] GL's GLTranslationPalette, moved from gl/textures/gl_translate.h: the translations' palettes as the texture system numbers them, for any renderer.

#ifndef ZX_TRANSLATIONPALETTE_H
#define ZX_TRANSLATIONPALETTE_H

#include "doomtype.h"
#include "v_video.h"

enum
{
	TRANSLATION_ICE = -1,
	TRANSLATION_INTENSITY = -2,
	TRANSLATION_SHADE = -3,
};


class FTranslationPalette : public FNativePalette
{
	struct PalData
	{
		int crc32;
		PalEntry pe[256];
	};
	static TArray<PalData> AllPalettes;

	int Index;
	FRemapTable *remap;

	FTranslationPalette(FRemapTable *r) { remap=r; Index=-1; }

public:

	static FTranslationPalette *CreatePalette(FRemapTable *remap);
	static int GetInternalTranslation(int trans);
	static PalEntry *GetPalette(unsigned int index)
	{
		return index > 0 && index <= AllPalettes.Size()? AllPalettes[index-1].pe : NULL;
	}
	bool Update();
	int GetIndex() const { return Index; }
};


#endif
