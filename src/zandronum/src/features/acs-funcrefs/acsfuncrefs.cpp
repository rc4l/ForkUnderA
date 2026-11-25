// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-funcrefs/acsfuncrefs.h"
#include "features/acs-funcrefs/computation/funcref_compute.h"

#include "p_acs.h"
#include "w_wad.h"
#include "c_console.h"

namespace zx { namespace acsfuncrefs {

static_assert(kLibraryShift == LIBRARYID_SHIFT, "a stable reference keeps its library where a load-position one keeps its module");

static bool IsLibraryLump(int lump)
{
	return Wads.GetLumpNamespace(lump) == ns_acslibrary;
}

int LibraryOrdinalOf(int lump)
{
	return ComputeLibraryOrdinal(lump, IsLibraryLump);
}

int32_t PushFunctionRef(FBehavior* module, int funcnum)
{
	FBehavior* home = module;
	const ScriptFunction* func = module->GetFunction(funcnum, home);
	if (func != nullptr)
	{
		const int32_t ref = ComputeFuncRefStable(home->GetFunctionIndex(func), home->GetLibraryOrdinal());
		if (ref != 0)
			return ref;
	}
	return funcnum | module->GetLibraryID();
}

FBehavior* ResolveFunctionRef(int32_t ref, int& funcnum)
{
	const FuncRef decoded = ComputeFuncRefDecode(ref);
	funcnum = decoded.index;
	if (!decoded.stable)
		return FBehavior::StaticGetModule(decoded.library);
	const int module = ComputeModuleOfLibrary(FBehavior::StaticModuleCount(), decoded.library,
		[](int i) { return FBehavior::StaticGetModule(i)->GetLibraryOrdinal(); });
	return module < 0 ? nullptr : FBehavior::StaticGetModule(module);
}

void PrintMissingFunction(int32_t ref, const char* script)
{
	const FuncRef decoded = ComputeFuncRefDecode(ref);
	if (!decoded.stable)
	{
		Printf("Function %d in %s out of range\n", decoded.index, script);
		return;
	}
	const int lump = ComputeLibraryLump(decoded.library, Wads.GetNumLumps(), IsLibraryLump);
	const char* name = lump < 0 ? "?" : Wads.GetLumpFullName(lump);
	Printf("Function %d of ACS library %s, which this map does not load, called in %s\n", decoded.index, name, script);
}

}} // namespace zx::acsfuncrefs
