// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/texflags_compute.h"

namespace zx { namespace hwrender {

unsigned ComputePieceTexFlags(const PieceTexInputs &in)
{
	unsigned f = 0;
	if (in.stencil) f |= kTexFlagStencil;
	if (in.alphaTest) f |= kTexFlagAlphaTest;
	if (in.hud) f |= kTexFlagHud;
	if (in.level) f |= kTexFlagLevel;
	if (in.inverse) f |= kTexFlagInverse;
	if (in.colorBlend) f |= kTexFlagColorBlend;
	if (in.fogLayer) f |= kTexFlagFogLayer;
	if (in.fogBoundary) f |= kTexFlagFogBoundary;
	if (in.fogPass) f |= kTexFlagFogPass;
	if (in.noMarks) f |= kTexFlagNoMarks;
	f |= ((unsigned)in.fuzz & kTexFlagFuzzMask) << kTexFlagFuzzShift;
	f |= ((unsigned)in.cullFace & kTexFlagCullMask) << kTexFlagCullShift;
	return f;
}

}} // namespace zx::hwrender
