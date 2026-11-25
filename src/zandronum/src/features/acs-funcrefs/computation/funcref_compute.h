// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSFUNCREFS_COMPUTE_H
#define ZX_ACSFUNCREFS_COMPUTE_H

#include <cstdint>

namespace zx { namespace acsfuncrefs {

// [Doruk-Aksoy] A stable reference is library << 20 | bit 19 | function index, the library numbered by its place among the loaded ACS library lumps.
constexpr uint32_t kStableBit = uint32_t(1) << 19;
constexpr int kLibraryShift = 20;
constexpr int kIndexMask = 0xFFFF;
constexpr int kMaxLibrary = 4095;

struct FuncRef
{
	bool stable;
	int library;	// [Doruk-Aksoy] A library's number when stable, else a module's load position.
	int index;
};

// [Doruk-Aksoy] The stable reference to function `index` of library `ordinal`, or 0 when either does not fit.
int32_t ComputeFuncRefStable(int index, int ordinal);

// [Doruk-Aksoy] What a reference names, read in either form.
FuncRef ComputeFuncRefDecode(int32_t ref);

// [Doruk-Aksoy] A library lump's place among the library lumps before it, or -1 when `lump` is not one.
template <class IsLibrary>
int ComputeLibraryOrdinal(int lump, IsLibrary isLibrary)
{
	if (lump < 0 || !isLibrary(lump))
		return -1;
	int ordinal = 0;
	for (int i = 0; i < lump; ++i)
		ordinal += isLibrary(i) ? 1 : 0;
	return ordinal;
}

// [Doruk-Aksoy] The lump of library `ordinal` among `count` lumps, or -1 when there are not that many libraries.
template <class IsLibrary>
int ComputeLibraryLump(int ordinal, int count, IsLibrary isLibrary)
{
	for (int lump = 0, seen = 0; ordinal >= 0 && lump < count; ++lump)
		if (isLibrary(lump) && seen++ == ordinal)
			return lump;
	return -1;
}

// [Doruk-Aksoy] The loaded module holding library `ordinal`, or -1 when none of the `count` does.
template <class OrdinalOf>
int ComputeModuleOfLibrary(int count, int ordinal, OrdinalOf ordinalOf)
{
	if (ordinal < 0)
		return -1;
	for (int i = 0; i < count; ++i)
		if (ordinalOf(i) == ordinal)
			return i;
	return -1;
}

}} // namespace zx::acsfuncrefs

#endif
