// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The sprite atlas: where every texture needs a binding of its own (WebGPU, which has no texture arrays), the frame's sprites draw from shared pages, so the sorted pass binds a page rather than a texture a sprite; one binding for every sprite was 9-12% more frames at Sunder MAP16's busiest view in Firefox (queue #59).

#include "c_cvars.h"

#ifdef FUA_DILIGENT

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureView.h"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/spriteatlas_compute.h"
#include "features/hwrender/computation/texfilter_compute.h"
#include "features/hwrender/computation/imageresize_compute.h"
#include "features/materials/materials.h"

#include <unordered_map>
#include <vector>

// [rc4l] Off draws every sprite with its own binding again, for an A/B in one session.
CVAR(Bool, fua_dg_spriteatlas, true, 0)
EXTERN_CVAR(Int, r_texture_filter)

namespace zx { namespace hwrender {

namespace {

const int kPageSize = 2048;   // [rc4l] 16 MB a page, and as much again for its brightmaps once one has any
const int kMaxPages = 2;
const int kMaxSide = 512;     // [rc4l] a larger sprite (a boss, a HUD weapon) keeps its own binding

struct Page
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, bright;
};

struct PageSRB { const void *pso; int page; Diligent::IShaderResourceBinding *srb; };

// [rc4l] Keyed on the texture manager's index, which outlives a level where an FMaterial's address does not, so the pages keep their images from one level to the next; uploading them again each level grew the browser's JS heap by 0.86 MB a level change.
std::unordered_map<int, AtlasEntry> g_entries;   // [rc4l] page -1 for one refused
std::vector<AtlasPage> g_pack;
std::vector<Page> g_pages;
std::vector<PageSRB> g_srbs;

void ReleaseSRBsOf(int page)
{
	for (size_t i = g_srbs.size(); i-- > 0;)
	{
		if (page >= 0 && g_srbs[i].page != page) continue;
		g_srbs[i].srb->Release();
		g_srbs.erase(g_srbs.begin() + i);
	}
}

// [rc4l] A page's texture, its texels written only where an image is placed: created with initial data, a page grew the browser's JS heap by 0.86 MB every level change.
void MakePage(Diligent::RefCntAutoPtr<Diligent::ITexture> &tex, const char *name)
{
	Diligent::TextureDesc td;
	td.Name = name;
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = td.Height = (Diligent::Uint32)kPageSize;
	td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	td.Usage = Diligent::USAGE_DEFAULT;
	td.MipLevels = 1;
	CreateDeviceTexture(td, NULL, &tex);
}

void Upload(Diligent::ITexture *tex, int x, int y, int w, int h, const unsigned char *rgba)
{
	Diligent::Box box;
	box.MinX = (Diligent::Uint32)x; box.MaxX = (Diligent::Uint32)(x + w);
	box.MinY = (Diligent::Uint32)y; box.MaxY = (Diligent::Uint32)(y + h);
	Diligent::TextureSubResData sub;
	sub.pData = rgba;
	sub.Stride = (Diligent::Uint64)w * 4;
	GetContext()->UpdateTexture(tex, 0, 0, box, sub, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

// [rc4l] Black over an image's rectangle and border on a brightmap page, since a brightmap adds to the lit colour.
void UploadBlack(Diligent::ITexture *tex, const AtlasEntry &e)
{
	const std::vector<unsigned char> black((size_t)(e.w + 2) * (e.h + 2) * 4, 0);
	Upload(tex, e.x - 1, e.y - 1, e.w + 2, e.h + 2, &black[0]);
}

// [rc4l] A material's brightmap over entry e on its page's brightmap texture, or black there once the page has one; the texture is made for the page's first brightmap, black first under every image placed before it.
void PlaceBrightmap(const void *material, const AtlasEntry &e)
{
	Page &pg = g_pages[e.page];
	const void *bm = zx::materials::MaterialBrightmap(material);
	int bw = 0, bh = 0;
	unsigned char *px = (bm != NULL && bm != material) ? zx::materials::MaterialPixels(bm, 0, bw, bh) : NULL;
	if (px == NULL)
	{
		if (pg.bright) UploadBlack(pg.bright, e);
		return;
	}
	if (!pg.bright)
	{
		MakePage(pg.bright, "fua sprite atlas brightmaps");
		if (pg.bright) for (const auto &other : g_entries) if (other.second.page == e.page) UploadBlack(pg.bright, other.second);
		ReleaseSRBsOf(e.page);   // [rc4l] they bound black for this page's brightmaps
	}
	if (pg.bright)
	{
		std::vector<unsigned char> sized;
		const unsigned char *src = px;
		if (bw != e.w || bh != e.h)
		{
			sized.resize((size_t)e.w * e.h * 4);
			ComputeNearestResize(bw, bh, px, e.w, e.h, &sized[0], 4);
			src = &sized[0];
		}
		std::vector<unsigned char> framed;
		ComputeAtlasFramed(src, e.w, e.h, framed);
		Upload(pg.bright, e.x - 1, e.y - 1, e.w + 2, e.h + 2, &framed[0]);
	}
	delete[] px;
}

} // namespace

bool AtlasActive()
{
	const TextureFilter f = ComputeTextureFilter((int)r_texture_filter);
	return fua_dg_spriteatlas && !BindlessActive() && !f.minLinear && !f.magLinear && !f.mipmapped;
}

int AtlasPageSize() { return kPageSize; }

const AtlasEntry *AtlasFor(const void *material, int translation, bool sprite)
{
	// [rc4l] A translated sprite keeps its own binding, since a translation's colours can change between levels while its number stays.
	zx::materials::MaterialInfo info;
	if (material == NULL || translation != 0 || !sprite || !zx::materials::DescribeMaterial(material, info)) return NULL;
	const auto found = g_entries.find(info.textureIndex);
	if (found != g_entries.end()) return found->second.page >= 0 ? &found->second : NULL;

	AtlasEntry e = { -1, 0, 0, 0, 0 };
	AtlasCandidate c = { true, true, sprite, !info.canvas && info.warp == 0 && info.userShader < 0, info.width, info.height };
	int w = 0, h = 0;
	unsigned char *px = ComputeAtlasEligible(c, kMaxSide) ? zx::materials::MaterialPixels(material, 0, w, h) : NULL;
	c.width = w;
	c.height = h;
	AtlasPlace at;
	if (px != NULL && ComputeAtlasEligible(c, kMaxSide) && ComputeAtlasPlace(g_pack, kMaxPages, kPageSize, kPageSize, w + 2, h + 2, at))
	{
		if (at.page >= (int)g_pages.size()) g_pages.resize(at.page + 1);
		Page &pg = g_pages[at.page];
		if (!pg.color) MakePage(pg.color, "fua sprite atlas");
		if (pg.color)
		{
			std::vector<unsigned char> framed;
			ComputeAtlasFramed(px, w, h, framed);
			Upload(pg.color, at.x, at.y, w + 2, h + 2, &framed[0]);
			e = { at.page, at.x + 1, at.y + 1, w, h };
			PlaceBrightmap(material, e);
		}
	}
	delete[] px;
	const AtlasEntry &kept = g_entries.emplace(info.textureIndex, e).first->second;
	return kept.page >= 0 ? &kept : NULL;
}

Diligent::IShaderResourceBinding *AtlasSRB(Diligent::IPipelineState *pso, int page)
{
	if (pso == NULL || page < 0 || page >= (int)g_pages.size() || !g_pages[page].color) return NULL;
	for (const PageSRB &s : g_srbs) if (s.pso == pso && s.page == page) return s.srb;
	PageSRB s = { pso, page, NULL };
	pso->CreateShaderResourceBinding(&s.srb, true);
	if (s.srb == NULL) return NULL;
	if (auto *v = s.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(g_pages[page].color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	if (auto *v = s.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap"))
		v->Set(g_pages[page].bright ? g_pages[page].bright->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : GetBrightmapSRV(NULL));
	g_srbs.push_back(s);
	return s.srb;
}

int AtlasEntries(int &pages)
{
	pages = (int)g_pages.size();
	int placed = 0;
	for (const auto &e : g_entries) if (e.second.page >= 0) placed++;
	return placed;
}

void ReleaseAtlasSRBs()
{
	ReleaseSRBsOf(-1);
}

void ResetAtlas()
{
	ReleaseAtlasSRBs();
	g_entries.clear();
	g_pack.clear();
	g_pages.clear();
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
