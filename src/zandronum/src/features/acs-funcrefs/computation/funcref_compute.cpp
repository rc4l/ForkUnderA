// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-funcrefs/computation/funcref_compute.h"

namespace zx { namespace acsfuncrefs {

int32_t ComputeFuncRefStable(int index, int ordinal)
{
	if (index < 0 || index > kIndexMask || ordinal < 0 || ordinal > kMaxLibrary)
		return 0;
	return static_cast<int32_t>((static_cast<uint32_t>(ordinal) << kLibraryShift) | kStableBit | static_cast<uint32_t>(index));
}

FuncRef ComputeFuncRefDecode(int32_t ref)
{
	const uint32_t bits = static_cast<uint32_t>(ref);
	return FuncRef { (bits & kStableBit) != 0, static_cast<int>(bits >> kLibraryShift), static_cast<int>(bits & kIndexMask) };
}

}} // namespace zx::acsfuncrefs
