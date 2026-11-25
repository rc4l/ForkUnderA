// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acsvec/acsvec.h"

#include "p_acs.h"

namespace zx { namespace acsvec {

int CallVecFunction(FBehavior* module, const int32_t* args, int argCount)
{
	const VecCall call = ComputeVecCall(args, argCount);
	if (call.count > 1 && module != nullptr && call.channel >= 0 && call.channel < NUM_MAPVARS && module->MapVars[call.channel] != nullptr)
	{
		const int array = *module->MapVars[call.channel];
		for (int i = 1; i < call.count; ++i)
			module->SetArrayVal(array, i - 1, call.value[i]);
	}
	return call.count > 0 ? call.value[0] : 0;
}

}} // namespace zx::acsvec
