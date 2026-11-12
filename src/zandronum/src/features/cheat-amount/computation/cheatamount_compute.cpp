// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/cheat-amount/computation/cheatamount_compute.h"

#include <climits>
#include <cstdint>
#include <cstdlib>

namespace zx { namespace cheatamount {

// [Doruk-Aksoy] Clamps a 64-bit result into int's range.
static int ClampToInt(int64_t v)
{
	if (v > INT_MAX)
		return INT_MAX;
	if (v < INT_MIN)
		return INT_MIN;
	return static_cast<int>(v);
}

int ComputeCheatAmountArg(const char *arg)
{
	if (arg == nullptr)
		return 0;
	// [Doruk-Aksoy] strtoll saturates at LLONG_MIN/LLONG_MAX where atoi's overflow is undefined.
	const long long v = std::strtoll(arg, nullptr, 10);
	if (v < 1)
		return 1;
	if (v > INT_MAX)
		return INT_MAX;
	return static_cast<int>(v);
}

int ComputeCheatAmountFromWire(int raw)
{
	return raw < 0 ? 0 : raw;
}

int ComputeCheatAmountWireBytes(bool demoPlayback, int demoVersion)
{
	return (demoPlayback && demoVersion < kLongAmountDemoVersion) ? 2 : 4;
}

int ComputeSaturatingAdd(int a, int b)
{
	return ClampToInt(static_cast<int64_t>(a) + b);
}

int ComputeSaturatingMul(int a, int b)
{
	return ClampToInt(static_cast<int64_t>(a) * b);
}

}} // namespace zx::cheatamount
