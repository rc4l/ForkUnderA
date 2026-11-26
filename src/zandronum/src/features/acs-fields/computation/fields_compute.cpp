// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-fields/computation/fields_compute.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace zx { namespace acsfields {

namespace {

FieldKind KindOf(int type)
{
	switch (type)
	{
	case kValInt: return kFieldInt;
	case kValFixed: return kFieldFixed;
	case kValAngle: return kFieldAngle;
	case kValBool: return kFieldBool;
	case kValFloat: return kFieldFloat;
	default: return kFieldNone;
	}
}

size_t SizeOf(FieldKind kind)
{
	switch (kind)
	{
	case kFieldInt: return sizeof(int32_t);
	case kFieldFixed: return sizeof(int64_t);
	case kFieldAngle: return sizeof(uint32_t);
	case kFieldBool: return sizeof(bool);
	default: return sizeof(double);
	}
}

// [Doruk-Aksoy] The value of type T stored at `at`, which may be unaligned.
template <class T> T Load(const unsigned char *at)
{
	T value;
	std::memcpy(&value, at, sizeof value);
	return value;
}

} // namespace

FieldShape ComputeFieldShape(int type, int baseType, int arrayLength, bool userVar)
{
	if (type == kValArray)
	{
		const FieldKind kind = arrayLength > 0 ? KindOf(baseType) : kFieldNone;
		return FieldShape{ kind, kind == kFieldNone ? 0 : arrayLength, kind == kFieldInt && userVar };
	}
	const FieldKind kind = KindOf(type);
	return FieldShape{ kind, kind == kFieldNone ? 0 : 1, kind == kFieldInt && userVar };
}

bool ComputeFieldIndexOk(const FieldShape &shape, int index)
{
	return shape.kind != kFieldNone && index >= 0 && index < shape.count;
}

int32_t ComputeFixedToAcs(int64_t raw)
{
	return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(raw)));
}

int32_t ComputeAngleToAcs(uint32_t bam)
{
	return static_cast<int32_t>(bam >> 16);
}

int32_t ComputeFloatToAcs(double value)
{
	const double scaled = std::trunc(value * 65536.0);
	if (!(scaled == scaled) || scaled >= 9.2e18 || scaled <= -9.2e18)
		return 0;
	return ComputeFixedToAcs(static_cast<int64_t>(scaled));
}

int32_t ComputeFieldRead(const FieldShape &shape, const void *address, int index)
{
	if (address == nullptr || !ComputeFieldIndexOk(shape, index))
		return 0;
	const unsigned char *at = static_cast<const unsigned char *>(address) + SizeOf(shape.kind) * size_t(index);
	switch (shape.kind)
	{
	case kFieldInt: return Load<int32_t>(at);
	case kFieldFixed: return ComputeFixedToAcs(Load<int64_t>(at));
	case kFieldAngle: return ComputeAngleToAcs(Load<uint32_t>(at));
	case kFieldBool: return Load<bool>(at) ? 1 : 0;
	default: return ComputeFloatToAcs(Load<double>(at));
	}
}

bool ComputeFieldWrite(const FieldShape &shape, void *address, int index, int32_t value)
{
	if (address == nullptr || !shape.writable || !ComputeFieldIndexOk(shape, index))
		return false;
	std::memcpy(static_cast<unsigned char *>(address) + sizeof(int32_t) * size_t(index), &value, sizeof value);
	return true;
}

}} // namespace zx::acsfields
