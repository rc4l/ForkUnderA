// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The render copy of the level's own state: its clocks, flags, sky scroll and fog, and the skies ChangeSky sets, taken between tics.

#ifndef ZX_GAMECOPY_LEVEL_H
#define ZX_GAMECOPY_LEVEL_H

#include "g_level.h"
#include "textures/textures.h"

namespace zx { namespace gamecopy {

// [rc4l] The level as the renderer reads it, its members declared once in fields.h.
struct LevelRecord
{
#define ZX_LEVEL_FIELD(member) decltype(FLevelLocals::member) member;
#define ZX_LEVEL_DERIVED(type, member) type member;
#include "features/gamecopy/fields.h"
#undef ZX_LEVEL_FIELD
#undef ZX_LEVEL_DERIVED
};

// [rc4l] The level as the last fill took it.
const LevelRecord &Level();

// [Doruk-Aksoy] The level's post-process shader state as the last fill took it, copied only when it changed.
const zx::postprocess::ShaderTable &LevelShaders();

}} // namespace zx::gamecopy

#endif // ZX_GAMECOPY_LEVEL_H
