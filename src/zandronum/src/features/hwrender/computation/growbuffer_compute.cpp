// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/growbuffer_compute.h"

namespace zx { namespace hwrender {

unsigned int ComputeGrownCapacity(unsigned int needed, unsigned int pad)
{
	return needed + needed / 2 + pad;
}

bool ComputeBufferFits(bool made, unsigned int capacity, unsigned int count)
{
	return made && capacity >= count;
}

unsigned int ComputePieceCapacity(unsigned int levelPieces, unsigned int dynamicPieces)
{
	const unsigned int dynamic = ComputeGrownCapacity(dynamicPieces, 0);
	return ComputeGrownCapacity(levelPieces, dynamic > kDynamicRecordsAtLeast ? dynamic : kDynamicRecordsAtLeast);
}

}} // namespace zx::hwrender
