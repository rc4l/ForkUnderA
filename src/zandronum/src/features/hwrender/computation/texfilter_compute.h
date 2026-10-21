// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's texture filter modes (TexFilter[] in gl_texture.cpp) as the backend's samplers take them.

#ifndef ZX_TEXFILTER_COMPUTE_H
#define ZX_TEXFILTER_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] One mode as a sampler: linear or nearest when shrinking, when enlarging and between mips, and whether mips are used at all.
struct TextureFilter
{
	bool minLinear, magLinear, mipLinear, mipmapped;
};

// [rc4l] r_texture_filter's mode, entry for entry, with anything outside 0..5 read as 0 (nearest, no mips).
TextureFilter ComputeTextureFilter(int mode);

// [rc4l] r_texture_filter_anisotropic as a sampler's anisotropy: the setting rounded, at most 16, for a filter that is linear both ways and mipmapped, else 0 (off), since nearest filtering keeps its pixels.
unsigned ComputeAnisotropy(const TextureFilter &filter, float setting);

// [rc4l] Whether a material is made with a mip chain: when mips are allowed (fua_dg_mips) and the filter reads past the first level.
bool ComputeMaterialMips(bool allowed, const TextureFilter &filter);

}} // namespace zx::hwrender

#endif // ZX_TEXFILTER_COMPUTE_H
