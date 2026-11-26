// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/computation/uservar_compute.h"

#include <cmath>
#include <cstdint>

#include "features/acs-parity/computation/acsparity_compute.h"

namespace zx { namespace decorateuzdoom {

UserVarMiss ComputeUserVarAccess(bool numericUserVar, bool isArray, unsigned count, UserVarShape shape, int index)
{
	if (!numericUserVar || (shape == kUserVarScalar && isArray) || (shape == kUserVarArray && !isArray))
		return kUserVarNotDeclared;
	if (index < 0 || static_cast<unsigned>(index) >= count)
		return kUserVarOutOfBounds;
	return kUserVarFound;
}

UserVarLayout ComputeUserVarLayout(unsigned classSize, bool isFloat, unsigned count)
{
	const unsigned element = isFloat ? 8u : 4u;
	const unsigned pad = (element - classSize % element) % element;
	UserVarLayout layout = { classSize + pad, pad + element * count };
	return layout;
}

int32_t ComputeUserVarIntFromDouble(double value)
{
	if (!(value > INT32_MIN))
		return INT32_MIN;
	if (value >= INT32_MAX)
		return INT32_MAX;
	return static_cast<int32_t>(std::trunc(value));
}

int32_t ComputeUserVarToAcs(double value)
{
	return zx::acsparity::ComputeFixedFromDouble(value);
}

double ComputeUserVarFromAcs(int32_t value)
{
	return value / 65536.0;
}

}} // namespace zx::decorateuzdoom
