// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Code GL and the backend both run picks the map state it reads with one of these as a template's Sel: GL the live structures, the backend the render copy.

#ifndef ZX_GAMECOPY_SELECT_H
#define ZX_GAMECOPY_SELECT_H

#include "r_defs.h"

namespace zx { namespace gamecopy {

// [rc4l] The live structures, which GL reads, written through the setters whose dirty counters GL's caches key on.
struct LiveSel
{
	static sector_t &Of(sector_t *s) { return *s; }
	static side_t &Of(side_t *s) { return *s; }
	static void SetTexture(sector_t *s, int pos, FTextureID tex) { s->SetTexture(pos, tex, false); }
	static void SetPlaneTexZ(sector_t *s, int pos, fixed_t z) { s->SetPlaneTexZ(pos, z); }
};

// [rc4l] The render copy, which the backend reads, as a frame drawn beside a running tic may.
struct CopySel
{
	static SectorState &Of(sector_t *s) { return s->rc; }
	static SideState &Of(side_t *s) { return s->rc; }
	static void SetTexture(sector_t *s, int pos, FTextureID tex) { s->rc.planes[pos].Texture = tex; }
	static void SetPlaneTexZ(sector_t *s, int pos, fixed_t z) { s->rc.planes[pos].TexZ = z; }
};

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_SELECT_H
