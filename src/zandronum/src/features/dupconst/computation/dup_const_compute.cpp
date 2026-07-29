// [rc4l] See dup_const_compute.h for why a duplicate global constant is tolerated.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "dup_const_compute.h"

namespace zx
{

DuplicateConstantAction ComputeDuplicateConstantAction(bool isGlobalScope, bool existingIsConstant,
                                                       bool sameValue)
{
	// [rc4l] Only an engine-vs-mod collision between two GLOBAL constants is tolerated. A class-scoped
	// duplicate is a mod contradicting itself, and a collision with a non-constant is a real conflict
	// -- silently dropping either would hide an authoring bug.
	if (!isGlobalScope || !existingIsConstant)
		return DupConst_Error;

	return sameValue ? DupConst_WarnSameValue : DupConst_WarnValueChanged;
}

} // namespace zx
