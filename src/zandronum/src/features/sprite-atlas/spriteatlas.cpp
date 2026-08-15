//-----------------------------------------------------------------------------
//
// [ForkUnderA] sprite-atlas implementation. See spriteatlas.h for the why.
//
// Each cell replicates the "expanded" sprite texture layout the GL renderer
// uses (a 1px transparent border around the art), plus 1px spacing between
// cells, so linear filtering at cell edges only ever reads transparent
// pixels. Pages are built without mipmaps (the page material binds with
// CLAMP_XY_NOMIP in gl_sprite.cpp) because mip reduction would blend
// neighboring cells together.
//
//-----------------------------------------------------------------------------

#ifndef NO_GL

#include "doomtype.h"
#include "doomstat.h"
#include "c_cvars.h"
#include "textures/textures.h"
#include "textures/bitmap.h"
#include "colormatcher.h"
#include "features/sprite-atlas/spriteatlas.h"
#include "gl/textures/gl_material.h"

CVAR(Bool, gl_sprite_atlas, true, CVAR_ARCHIVE)

static const int PAGE_SIZE = 2048;
static const int MAX_DIM = 128;   // only small effect-class sprites
static const int BORDER = 1;      // expanded-texture transparent border
static const int SPACING = 1;     // extra gap between cells

//==========================================================================
//
// The page texture: a plain FTexture over a session-owned RGBA bitmap.
// FMaterial/FGLTexture treat it like any other texture; CopyTrueColorPixels
// is what the GL upload path reads.
//
//==========================================================================

class FSpriteAtlasPage : public FTexture
{
public:
	FBitmap Pixels32;
	BYTE *Pixels8;

	FSpriteAtlasPage()
	{
		Width = PAGE_SIZE;
		Height = PAGE_SIZE;
		CalcBitSize();
		UseType = TEX_MiscPatch;
		bMasked = true;
		bNoDecals = true;
		Pixels32.Create(PAGE_SIZE, PAGE_SIZE);
		memset(Pixels32.GetPixels(), 0, PAGE_SIZE * PAGE_SIZE * 4);
		Pixels8 = NULL;
	}

	~FSpriteAtlasPage()
	{
		Unload();
	}

	int CopyTrueColorPixels(FBitmap *bmp, int x, int y, int rotate, FCopyInfo *inf)
	{
		// FBitmap::GetPitch() is already in BYTES (w*4).
		bmp->CopyPixelDataRGB(x, y, Pixels32.GetPixels(), Width, Height,
			4, Pixels32.GetPitch(), rotate, CF_BGRA, inf);
		return -1; // has transparent pixels
	}

	bool UseBasePalette() { return false; }

	// The paletted interface exists only to satisfy FTexture; the GL renderer
	// never reads it for this texture. Converted lazily if something does.
	const BYTE *GetPixels()
	{
		if (Pixels8 == NULL)
		{
			Pixels8 = new BYTE[Width * Height];
			const BYTE *src = Pixels32.GetPixels();
			for (int i = 0; i < Width * Height; i++)
			{
				// column-major like every FTexture pixel store
				int px = i / Height, py = i % Height;
				const BYTE *p = src + py * Pixels32.GetPitch() + px * 4;
				Pixels8[i] = p[3] < 128 ? 0 : ColorMatcher.Pick(p[2], p[1], p[0]);
			}
		}
		return Pixels8;
	}

	const BYTE *GetColumn(unsigned int column, const Span **spans_out)
	{
		const BYTE *pix = GetPixels();
		if (column >= (unsigned)Width) column = Width - 1;
		if (spans_out != NULL)
		{
			static Span dummy[2];
			dummy[0].TopOffset = 0;
			dummy[0].Length = Height;
			dummy[1].TopOffset = 0;
			dummy[1].Length = 0;
			*spans_out = dummy;
		}
		return pix + column * Height;
	}

	void Unload()
	{
		if (Pixels8 != NULL)
		{
			delete[] Pixels8;
			Pixels8 = NULL;
		}
	}
};

//==========================================================================
//
// Pages + a shelf packer + the texture -> cell registry
//
//==========================================================================

struct AtlasPageState
{
	FSpriteAtlasPage *tex;
	FMaterial *material;
	int shelfX, shelfY, shelfH;
};

static TArray<AtlasPageState> Pages;
static TMap<int, FSpriteAtlasEntry> Registry;   // FTextureID index -> cell
static TMap<int, BYTE> Rejected;                // don't re-evaluate misfits

static AtlasPageState *OpenPage()
{
	AtlasPageState p;
	p.tex = new FSpriteAtlasPage;
	p.material = FMaterial::ValidateTexture(p.tex, false);
	p.shelfX = SPACING;
	p.shelfY = SPACING;
	p.shelfH = 0;
	Pages.Push(p);
	return &Pages[Pages.Size() - 1];
}

// Returns false if the texture doesn't fit the current page.
static bool PackInto(AtlasPageState *page, FTexture *tex, int texindex)
{
	const int cw = tex->GetWidth() + 2 * BORDER;
	const int ch = tex->GetHeight() + 2 * BORDER;

	if (page->shelfX + cw + SPACING > PAGE_SIZE)
	{
		page->shelfY += page->shelfH + SPACING;
		page->shelfX = SPACING;
		page->shelfH = 0;
	}
	if (page->shelfY + ch + SPACING > PAGE_SIZE)
		return false;

	const int cx = page->shelfX;
	const int cy = page->shelfY;
	page->shelfX += cw + SPACING;
	if (ch > page->shelfH) page->shelfH = ch;

	// Art goes inside the transparent border, exactly like an expanded texture.
	tex->CopyTrueColorPixels(&page->tex->Pixels32, cx + BORDER, cy + BORDER, 0, NULL);

	FSpriteAtlasEntry e;
	e.material = page->material;
	e.u0 = (float)cx / PAGE_SIZE;
	e.v0 = (float)cy / PAGE_SIZE;
	e.uscale = (float)cw / PAGE_SIZE;
	e.vscale = (float)ch / PAGE_SIZE;
	Registry[texindex] = e;
	return true;
}

void SpriteAtlas_AddFromHitlist(const BYTE *hitlist, int count)
{
	if (!gl_sprite_atlas)
		return;

	int added = 0;
	bool touched = false;

	for (int i = 0; i < count; i++)
	{
		if (!hitlist[i]) continue;                       // marked textures only; UseType filters to sprites
		if (Registry.CheckKey(i) || Rejected.CheckKey(i)) continue;

		FTexture *tex = TexMan.ByIndex(i);
		if (tex == NULL || tex->UseType != FTexture::TEX_Sprite || tex->bWarped ||
			tex->bHasCanvas || tex->GetWidth() > MAX_DIM || tex->GetHeight() > MAX_DIM ||
			tex->GetWidth() <= 0 || tex->GetHeight() <= 0)
		{
			Rejected[i] = 1;
			continue;
		}

		AtlasPageState *page = Pages.Size() ? &Pages[Pages.Size() - 1] : OpenPage();
		if (!PackInto(page, tex, i))
		{
			page = OpenPage();
			if (!PackInto(page, tex, i)) { Rejected[i] = 1; continue; }
		}
		added++;
		touched = true;
	}

	if (touched)
	{
		// Invalidate GPU copies of pages so the next bind re-uploads the grown bitmap.
		for (unsigned int p = 0; p < Pages.Size(); p++)
		{
			for (int j = 0; j < 2; j++)
			{
				if (Pages[p].tex->gl_info.Material[j] != NULL)
					Pages[p].tex->gl_info.Material[j]->Clean(true);
			}
		}
		Printf("sprite atlas: +%d cells, %u page(s)\n", added, Pages.Size());
	}
}

void SpriteAtlas_Reset()
{
	// The page FTextures and their materials belong to the outgoing texture manager;
	// drop every reference rather than reuse anything across the rebuild.
	Pages.Clear();
	Registry.Clear();
	Rejected.Clear();
}

FSpriteAtlasEntry *SpriteAtlas_Lookup(int textureindex)
{
	if (!gl_sprite_atlas)
		return NULL;
	return Registry.CheckKey(textureindex);
}

static void InvalidatePages()
{
	for (unsigned int p = 0; p < Pages.Size(); p++)
	{
		for (int j = 0; j < 2; j++)
		{
			if (Pages[p].tex->gl_info.Material[j] != NULL)
				Pages[p].tex->gl_info.Material[j]->Clean(true);
		}
	}
}

FSpriteAtlasEntry *SpriteAtlas_GetOrPack(FTexture *tex, int textureindex)
{
	if (!gl_sprite_atlas)
		return NULL;
	FSpriteAtlasEntry *e = Registry.CheckKey(textureindex);
	if (e != NULL)
		return e;
	if (Rejected.CheckKey(textureindex))
		return NULL;
	if (tex == NULL || tex->UseType != FTexture::TEX_Sprite || tex->bWarped ||
		tex->bHasCanvas || tex->GetWidth() > MAX_DIM || tex->GetHeight() > MAX_DIM ||
		tex->GetWidth() <= 0 || tex->GetHeight() <= 0)
	{
		Rejected[textureindex] = 1;
		return NULL;
	}
	AtlasPageState *page = Pages.Size() ? &Pages[Pages.Size() - 1] : OpenPage();
	if (!PackInto(page, tex, textureindex))
	{
		page = OpenPage();
		if (!PackInto(page, tex, textureindex)) { Rejected[textureindex] = 1; return NULL; }
	}
	// The grown page's GPU copy is stale; its next bind re-uploads.
	for (int j = 0; j < 2; j++)
	{
		if (page->tex->gl_info.Material[j] != NULL)
			page->tex->gl_info.Material[j]->Clean(true);
	}
	return Registry.CheckKey(textureindex);
}

#else // NO_GL

#include "features/sprite-atlas/spriteatlas.h"
void SpriteAtlas_AddFromHitlist(const BYTE *, int) {}
FSpriteAtlasEntry *SpriteAtlas_Lookup(int) { return NULL; }
FSpriteAtlasEntry *SpriteAtlas_GetOrPack(FTexture *, int) { return NULL; }
void SpriteAtlas_Reset() {}

#endif
