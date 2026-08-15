//-----------------------------------------------------------------------------
//
// [ForkUnderA] sprite-atlas: pack small sprite textures into shared pages so
// consecutive translucent sprite draws bind the same material and the sprite
// batcher (gl_sprite.cpp) can merge them into single draw calls.
//
// Why: mass-death storms draw thousands of small effect sprites, depth-sorted,
// almost never two of the same texture in a row -- so per-draw batching never
// merges and GL-over-Metal pays full state-translation cost per sprite. With
// the storm's sprites on one page, the material stops breaking batch runs.
//
// Scope: face sprites up to 128x128, untranslated actors, no warp, no
// brightmap layers. Everything else draws the classic way -- the atlas only
// decides who gets the batching speedup, never how anything looks.
//
//-----------------------------------------------------------------------------

#ifndef __SPRITEATLAS_H__
#define __SPRITEATLAS_H__

#include "doomtype.h"

class FMaterial;
struct FSpriteAtlasEntry
{
	FMaterial *material;   // the shared page material
	float u0, v0;          // top-left of this texture's cell (expanded border included)
	float uscale, vscale;  // multiply original [0,1] UVs into the cell
};

// Pack every sprite-marked texture in the precache hitlist that meets the
// criteria and isn't packed yet. Pages persist for the session; a page that
// gains entries has its GPU copy invalidated for re-upload on next bind.
void SpriteAtlas_AddFromHitlist(const BYTE *hitlist, int count);

// The page cell for a sprite texture, or NULL if it isn't atlased.
FSpriteAtlasEntry *SpriteAtlas_Lookup(int textureindex);

// Lookup, packing the texture on first sight if it's eligible. Called from the
// sprite path, so the atlas grows to exactly the working set of drawn sprites;
// a grown page's GPU copy refreshes on its next bind.
FSpriteAtlasEntry *SpriteAtlas_GetOrPack(class FTexture *tex, int textureindex);

// Drop every page and mapping. MUST be called when the texture manager is rebuilt
// (wad_reload restarts the engine): entries are keyed on texture index and hold page
// materials, both of which the old texture manager owned.
void SpriteAtlas_Reset();

#endif
