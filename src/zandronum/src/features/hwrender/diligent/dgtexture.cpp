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
// [rc4l] Animation (slotremap_compute), warps and brightmaps are the scene shader's, and each translation is its own cache entry.

#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"
#include "features/hwrender/diligent/dgdevice.h"   // [rc4l] CreateDeviceTexture

#include "features/materials/materials.h"
#include "features/hwrender/computation/texfilter_compute.h"   // [rc4l] ComputeMaterialMips
#include "tarray.h"
#include "c_cvars.h"



// [rc4l] Mip chains for materials. On by default: point-sampling a 64x64 Doom texture across a
// long vista aliases badly, and GL has always mipmapped. Off is the A/B for a cut-out texture that
// has lost its shape at distance -- see the note at the CreateTexture below.
CVAR(Bool, fua_dg_mips, true, CVAR_ARCHIVE)
EXTERN_CVAR(Int, r_texture_filter)

namespace zx { namespace hwrender {

Diligent::ITextureView *GetCameraSRV(const void *material);

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

	CreateDeviceTexture(td, &data, &g_white);
	if (g_white) g_whiteSRV = g_white->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
	return g_whiteSRV;
}

// Upload one FMaterial into a Diligent texture, or return the cached view.
Diligent::ITextureView *GetMaterialSRV(const void *materialPtr, int translation);
bool MaterialsWithMips();

// [rc4l] A 1x1 BLACK texture, which is what "this material has no brightmap" looks like.
//
// The brightmap shader ADDS its texel to the lit colour, so black is the identity and every
// material can be given a brightmap unconditionally. That removes the alternative -- a per-batch
// "has brightmap" flag, a branch in the shader, and a second pipeline permutation -- in exchange
// for one 4-byte texture.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_black;
static Diligent::ITextureView *EnsureBlack()
{
	if (!g_black)
	{
		auto *dev = GetDevice();
		if (!dev) return NULL;
		const unsigned char px[4] = { 0, 0, 0, 0 };
		Diligent::TextureDesc td;
		td.Name = "fua brightmap none";
		td.Type = Diligent::RESOURCE_DIM_TEX_2D;
		td.Width = 1; td.Height = 1; td.MipLevels = 1;
		td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
		td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		td.Usage = Diligent::USAGE_IMMUTABLE;
		Diligent::TextureSubResData sub;
		sub.pData = px;
		sub.Stride = 4;
		Diligent::TextureData data;
		data.pSubResources = &sub;
		data.NumSubresources = 1;
		CreateDeviceTexture(td, &data, &g_black);
		if (!g_black) return NULL;
	}
	return g_black->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
}

// [rc4l] The brightmap layer of a material, or black when it has none.
//
// GZDoom attaches one in FMaterial's constructor: CreateDefaultBrightmap fills gl_info.Brightmap
// from the texture's fullbright-marked texels or from a GLDEFS `brightmap` lump, and the material
// then takes shader index 3. Reading gl_info.Brightmap is reading the decision the engine already
// made rather than re-deriving which texels ought to glow -- see the divergence-triage skill.
//
// The brightmap is uploaded by handing it back through the ordinary material path, so it shares the
// cache, the format handling and the mip generation, and there is no second upload path to keep in
// step with the first.
Diligent::ITextureView *GetBrightmapSRV(const void *materialPtr)
{
	if (materialPtr == NULL) return EnsureBlack();
	const void *bm = zx::materials::MaterialBrightmap(materialPtr);
	if (bm == NULL || bm == materialPtr) return EnsureBlack();
	Diligent::ITextureView *srv = GetMaterialSRV(bm, 0);
	// [rc4l] GetMaterialSRV falls back to WHITE when it cannot produce a texture -- a sane default
	// for a base layer, and catastrophic for a brightmap, which is ADDED to the lit fragment. A white
	// brightmap saturates the surface to solid white, which is what a warped or canvas-backed
	// brightmap did here. Failure has to mean black, so the fallback is checked for by identity
	// rather than trusted to be harmless.
	if (srv == NULL || srv == EnsureWhite()) return EnsureBlack();
	return srv;
}

Diligent::ITextureView *GetMaterialSRV(const void *materialPtr, int translation)
{
	// [rc4l] Normalise the translation exactly as GL binding a material does, because the number arrives
	// in one of two different namespaces and they are indistinguishable by sign alone at the call
	// site that consumes it.
	//
	// Negative means "already a FTranslationPalette index, negate it". Positive means "a game
	// translation" -- an actor's thing->Translation -- which has to be looked up to get the internal
	// index. Feeding a game translation straight to CreateTexBuffer indexes the palette array with a
	// number from the wrong namespace, and the sprite comes out as scattered rainbow pixels, which is
	// what capturing GLSprite::translation without this did.
	translation = zx::materials::MaterialTranslation(translation);

	if (materialPtr == NULL) return EnsureWhite();

	// [rc4l] Canvas textures are resolved BEFORE the cache, and never enter it.
	//
	// This check used to sit after the cache lookup, which made it useless: on the first frame the
	// camera has not been rendered yet, so the lookup missed, execution fell through to
	// CreateTexBuffer, and the noise it returned was cached forever. Every later call hit that entry
	// and never reached the camera at all. A camera's view changes every frame, so it has no business
	// in a cache keyed on identity in the first place.
	{
		zx::materials::MaterialInfo info;
		if (zx::materials::DescribeMaterial(materialPtr, info) && info.canvas)
		{
			Diligent::ITextureView *cam = GetCameraSRV(materialPtr);
			return cam ? cam : EnsureWhite();
		}
	}

	for (unsigned i = 0; i < g_textures.Size(); i++)
		if (g_textures[i].material == materialPtr && g_textures[i].translation == translation)
			return g_textures[i].srv;

	auto *dev = GetDevice();
	if (!dev) return EnsureWhite();

	int w = 0, h = 0;
	// [rc4l] A warp texture's pixels are its unwarped source, so it uploads as is and ripples in the scene shader.
	unsigned char *buf = zx::materials::MaterialPixels(materialPtr, translation, w, h);
	if (buf == NULL)
	{
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
	const bool mips = MaterialsWithMips();
	td.BindFlags = mips ? Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET : Diligent::BIND_SHADER_RESOURCE;
	// [rc4l] Mipmapped. Point-sampling a 64x64 Doom texture across a Sunder-sized vista aliases
	// badly at distance -- the GL renderer has always mipmapped, so a port that does not is not
	// comparable to look at. MipLevels 0 means "full chain"; GENERATE_MIPS lets the GPU build it.
	td.Usage = Diligent::USAGE_DEFAULT;
	// [rc4l] Switchable, because mipmapping and ALPHA TESTING do not compose.
	//
	// A cut-out texture stores its shape in alpha, and averaging four texels averages the shape
	// with it: a few levels down the mask is grey everywhere and the discard threshold either
	// keeps the lot or drops the lot. The RGB it averages in comes from the TRANSPARENT texels
	// too, which carry whatever colour the palette left behind them and were never meant to be
	// seen. See docs/mapbake-invalidation.md for the case that found this.
	td.MipLevels = mips ? 0 : 1;
	td.MiscFlags = mips ? Diligent::MISC_TEXTURE_FLAG_GENERATE_MIPS : Diligent::MISC_TEXTURE_FLAG_NONE;

	// [rc4l] Create EMPTY, then write mip 0 and let the GPU build the rest. Passing initial data here
	// is what a single-mip texture does; with a full chain requested Diligent demands init data for
	// every level ("7 expected, while 1 provided") and refuses to create the texture at all. Since
	// levels 1..n are exactly what GenerateMips is for, the only honest way to ask is to hand over
	// level 0 separately.
	TexEntry e;
	e.material = materialPtr;
	e.translation = translation;
	CreateDeviceTexture(td, NULL, &e.tex);

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
	if (e.tex && e.srv && mips) GetContext()->GenerateMips(e.srv);
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

bool MaterialsWithMips()
{
	return zx::hwrender::ComputeMaterialMips(fua_dg_mips, zx::hwrender::ComputeTextureFilter((int)r_texture_filter));
}

// [rc4l] Whether this material needs alpha testing. It decides which pipeline draws it, and that
// choice is worth real time: a `discard` in the fragment shader defeats early-Z, so an alpha-tested
// pipeline shades every overdrawn fragment. Sunder MAP10 drew its whole visible wall set through the
// masked pipeline at first and paid 1.78 ms for it.
bool MaterialIsMasked(const void *materialPtr)
{
	return zx::materials::MaterialIsMasked(materialPtr);
}

}} // namespace zx::hwrender

// [rc4l] What the ALPHA of a material looks like by the time it is uploaded.
//
// A cut-out texture that draws as a solid rectangle has lost its alpha somewhere between the lump
// and the sampler, and every stage in between looks fine from outside. This asks the upload's own
// question -- CreateTexBuffer, with the arguments the upload uses -- and counts what came back.
CCMD(fua_dg_texalpha)
{
	if (argv.argc() < 2) { Printf("usage: fua_dg_texalpha [texture name]\n"); return; }
	const void *mat = zx::materials::MaterialForName(argv[1]);
	if (mat == NULL) { Printf("no material called %s\n", argv[1]); return; }
	int w = 0, h = 0;
	unsigned char *buf = zx::materials::MaterialPixels(mat, 0, w, h);
	if (buf == NULL) { Printf("%s produced no buffer\n", argv[1]); return; }
	int zero = 0, low = 0, full = 0;
	const int n = w * h;
	for (int i = 0; i < n; i++)
	{
		const unsigned char a = buf[i * 4 + 3];
		if (a == 0) zero++;
		else if (a < 128) low++;
		else if (a == 255) full++;
	}
	Printf("%s: %dx%d, alpha 0 in %.1f%%, under half in %.1f%%, fully opaque in %.1f%%\n",
		argv[1], w, h, 100.0 * zero / n, 100.0 * low / n, 100.0 * full / n);
	Printf("  isMasked %s, mIsTransparent %d\n",
		zx::materials::MaterialIsMasked(mat) ? "yes" : "no", zx::materials::MaterialTransparency(mat));
	delete[] buf;
}

#endif // FUA_DILIGENT
