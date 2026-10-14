// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/app-icon/appicon.h"
#include "features/app-icon/computation/appicon_compute.h"

#include "w_wad.h"
#include "textures/textures.h"
#include "textures/bitmap.h"

namespace zx { namespace appicon {

bool LoadAppIcon(int size, std::vector<unsigned char> &out)
{
	const int lump = Wads.CheckNumForFullName("fua_appicon.png");
	if (lump < 0) return false;
	FTexture *tex = FTexture::CreateTexture(lump, FTexture::TEX_MiscPatch);
	if (tex == NULL) return false;
	const int w = tex->GetWidth(), h = tex->GetHeight();
	std::vector<unsigned char> full((size_t)w * h * 4, 0);
	FBitmap bmp(full.data(), w * 4, w, h);
	tex->CopyTrueColorPixels(&bmp, 0, 0);
	delete tex;
	out.resize((size_t)size * size * 4);
	return ComputeIconDownscale(full.data(), w, h, size, out.data());
}

}} // namespace zx::appicon
