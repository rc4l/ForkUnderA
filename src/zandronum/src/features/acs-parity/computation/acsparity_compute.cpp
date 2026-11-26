// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-parity/computation/acsparity_compute.h"

#include <cctype>
#include <cstddef>

namespace zx { namespace acsparity {

namespace {

constexpr uint32_t kFracMask = 0xFFFF;
constexpr uint32_t kHalf = 0x8000;

// [Doruk-Aksoy] Unsigned so a value at the top of the range wraps, as UZDoom's int arithmetic does on every compiler it ships with.
int32_t Wrap(uint32_t x)
{
	return static_cast<int32_t>(x);
}

} // namespace

int32_t ComputeFixedFloor(int32_t x)
{
	return Wrap(static_cast<uint32_t>(x) & ~kFracMask);
}

int32_t ComputeFixedRound(int32_t x)
{
	return Wrap((static_cast<uint32_t>(x) + kHalf) & ~kFracMask);
}

int32_t ComputeFixedCeil(int32_t x)
{
	return Wrap((static_cast<uint32_t>(x) + kFracMask) & ~kFracMask);
}

int32_t ComputeFixedFromDouble(double units)
{
	const double scaled = std::nearbyint(units * 65536.0);
	if (!(scaled > INT32_MIN))
		return INT32_MIN;
	if (scaled >= INT32_MAX)
		return INT32_MAX;
	return static_cast<int32_t>(scaled);
}

double ComputeLineCoord(int axis, double v1x, double v1y, double dx, double dy, double along, double offset)
{
	const double start = axis == 0 ? v1x : v1y;
	double result = start + (axis == 0 ? dx : dy) * along;
	const double length = std::hypot(dx, dy);
	if (offset != 0 && length != 0)
		result += (axis == 0 ? dy : -dx) / length * offset;
	return result;
}

bool ComputeNetIdValid(int32_t id)
{
	return id > 0 && id <= 0xFFFF;
}

bool ComputeClientMayDamage(bool inClientMode, bool clientSideOnly)
{
	return !inClientMode || clientSideOnly;
}

bool ComputeProximityInRange(const ProximityPoint& ref, const ProximityPoint& mo, double distance, int flags)
{
	if (std::hypot(mo.x - ref.x, mo.y - ref.y) >= distance)
		return false;
	if (flags & kCpxNoZ)
		return true;
	return ref.z > mo.z ? ref.z - mo.top < distance : mo.z - ref.top < distance;
}

bool ComputeProximityLifeCounts(bool killed, int flags)
{
	return killed ? (flags & (kCpxCountDead | kCpxDeadOnly)) != 0 : !(flags & kCpxDeadOnly);
}

int ComputeProximityResult(int counter, int count, int flags, bool stoppedEarly)
{
	if (stoppedEarly)
		return (flags & (kCpxLessOrEqual | kCpxExact)) ? 0 : 1;
	if (counter == count)
		return 1;
	if (counter < count)
		return ((flags & kCpxLessOrEqual) && !(flags & kCpxExact)) ? 1 : 0;
	return 0;
}

InventoryKind ComputeInventoryKind(const char* type)
{
	if (type == nullptr)
		return kInventoryItem;
	const auto same = [type](const char* name) {
		size_t i = 0;
		for (; type[i] != 0 && name[i] != 0; ++i)
			if (std::tolower(static_cast<unsigned char>(type[i])) != std::tolower(static_cast<unsigned char>(name[i])))
				return false;
		return type[i] == name[i];
	};
	if (same("Armor"))
		return kInventoryArmor;
	if (same("Health"))
		return kInventoryHealth;
	return kInventoryItem;
}

int ComputeInventoryAmount(const InventoryState& state, bool max)
{
	if (max)
	{
		if (state.held)
			return state.maxAmount;
		if (state.isInventory)
			return state.defaultMax;
	}
	return state.held ? state.amount : 0;
}

}} // namespace zx::acsparity
