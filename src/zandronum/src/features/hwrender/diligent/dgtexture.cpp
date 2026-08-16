// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Milestone 6: materials. The engine's textures, uploaded into Diligent.
//
// This is the seam where the port stops being a demo. Geometry was easy -- interleaved floats are
// interleaved floats. Textures are where the engine's own subsystem has to be met: FMaterial owns
// composition (multipatch), translation, hires replacement and expansion, and it hands back a plain
// RGBA buffer only through CreateTexBuffer.
//
// So that is exactly the seam used: ask FMaterial for RGBA bytes, upload them to a Diligent texture,
// and cache by FMaterial* identity. Everything Doom-specific about how those bytes were produced
// stays on the engine side, and the backend sees a texture. It is also what makes this portable --
// the same call would feed a Metal or D3D12 backend unchanged.
//
// Deliberately NOT ported here: animation (the material for a frame is whatever the mesh recorded),
// translations beyond the default, warp and brightmap shaders. Those are shading features, and the
// milestone is "textured world", not "pixel-identical world".

#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#ifndef PLATFORM_WIN32
#define PLATFORM_WIN32 1
#endif

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"

#include "gl/textures/gl_material.h"
#include "tarray.h"

namespace zx { namespace hwrender {

Diligent::IRenderDevice *GetDevice();
Diligent::IDeviceContext *GetContext();

// [rc4l] Keyed on (material, TRANSLATION), not material alone.
//
// ZDoom draws coloured text by handing DrawTexture a palette remap, so the same font glyph is a
// different image in gold, red or grey. Ignoring the translation uploaded every glyph with the base
// palette, which left the menu tabs and the console dim and muddy while graphic menu items -- which
// carry no translation -- looked perfect. That asymmetry is the tell.
struct TexEntry
{
	const void *material;
	int         translation;
	Diligent::RefCntAutoPtr<Diligent::ITexture> tex;
	Diligent::ITextureView *srv;
};

static TArray<TexEntry> g_textures;
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_white;
static Diligent::ITextureView *g_whiteSRV = NULL;

// [rc4l] Stand-in for a material that will not upload, so a failed texture shows as flat white
// rather than as missing geometry -- an important distinction when eyeballing a port.
static Diligent::ITextureView *EnsureWhite()
{
	if (g_whiteSRV) return g_whiteSRV;
	auto *dev = GetDevice();
	if (!dev) return NULL;

	static const unsigned int px[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
	Diligent::TextureDesc td;
	td.Name = "fua white";
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = 2; td.Height = 2;
	td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	td.Usage = Diligent::USAGE_IMMUTABLE;
	td.MipLevels = 1;

	Diligent::TextureSubResData sub;
	sub.pData = px;
	sub.Stride = 2 * 4;
	Diligent::TextureData data;
	data.pSubResources = &sub;
	data.NumSubresources = 1;

	dev->CreateTexture(td, &data, &g_white);
	if (g_white) g_whiteSRV = g_white->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
	return g_whiteSRV;
}

// Upload one FMaterial into a Diligent texture, or return the cached view.
Diligent::ITextureView *GetMaterialSRV(const void *materialPtr, int translation)
{
	if (materialPtr == NULL) return EnsureWhite();

	for (unsigned i = 0; i < g_textures.Size(); i++)
		if (g_textures[i].material == materialPtr && g_textures[i].translation == translation)
			return g_textures[i].srv;

	auto *dev = GetDevice();
	if (!dev) return EnsureWhite();

	FMaterial *mat = (FMaterial *)materialPtr;
	int w = 0, h = 0;
	// [rc4l] createexpanded=false: the expanded border exists for GL's clamp behaviour on sprites and
	// would shift the UVs the mesh already baked.
	unsigned char *buf = mat->CreateTexBuffer(translation, w, h, true, false);
	if (buf == NULL || w <= 0 || h <= 0)
	{
		if (buf) delete[] buf;
		TexEntry e; e.material = materialPtr; e.translation = translation; e.srv = EnsureWhite();
		g_textures.Push(e);
		return e.srv;
	}

	Diligent::TextureDesc td;
	td.Name = "fua material";
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = (Diligent::Uint32)w;
	td.Height = (Diligent::Uint32)h;
	td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	// [rc4l] BIND_RENDER_TARGET is required alongside SHADER_RESOURCE for GenerateMips -- Diligent
	// builds the chain by rendering into each level. Without it CreateTexture fails outright and
	// every material silently fell back to the white placeholder (a convincingly blank world).
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET;
	// [rc4l] Mipmapped. Point-sampling a 64x64 Doom texture across a Sunder-sized vista aliases
	// badly at distance -- the GL renderer has always mipmapped, so a port that does not is not
	// comparable to look at. MipLevels 0 means "full chain"; GENERATE_MIPS lets the GPU build it.
	td.Usage = Diligent::USAGE_DEFAULT;
	td.MipLevels = 0;
	td.MiscFlags = Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS;

	// [rc4l] Create EMPTY, then write mip 0 and let the GPU build the rest. Passing initial data here
	// is what a single-mip texture does; with a full chain requested Diligent demands init data for
	// every level ("7 expected, while 1 provided") and refuses to create the texture at all. Since
	// levels 1..n are exactly what GenerateMips is for, the only honest way to ask is to hand over
	// level 0 separately.
	TexEntry e;
	e.material = materialPtr;
	e.translation = translation;
	dev->CreateTexture(td, NULL, &e.tex);

	if (e.tex)
	{
		Diligent::Box box;
		box.MinX = 0; box.MaxX = (Diligent::Uint32)w;
		box.MinY = 0; box.MaxY = (Diligent::Uint32)h;
		Diligent::TextureSubResData sub;
		sub.pData = buf;
		sub.Stride = (Diligent::Uint64)w * 4;
		GetContext()->UpdateTexture(e.tex, 0, 0, box, sub,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	delete[] buf;

	e.srv = e.tex ? e.tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : EnsureWhite();
	if (e.tex && e.srv) GetContext()->GenerateMips(e.srv);
	g_textures.Push(e);
	return e.srv;
}

void ReleaseMaterials()
{
	g_textures.Clear();
	g_whiteSRV = NULL;
	g_white.Release();
}

int MaterialCount() { return (int)g_textures.Size(); }

// [rc4l] Whether this material needs alpha testing. It decides which pipeline draws it, and that
// choice is worth real time: a `discard` in the fragment shader defeats early-Z, so an alpha-tested
// pipeline shades every overdrawn fragment. Sunder MAP10 drew its whole visible wall set through the
// masked pipeline at first and paid 1.78 ms for it.
bool MaterialIsMasked(const void *materialPtr)
{
	if (materialPtr == NULL) return false;
	return ((FMaterial *)materialPtr)->isMasked();
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
