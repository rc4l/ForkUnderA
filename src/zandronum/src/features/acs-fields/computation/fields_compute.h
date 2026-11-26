// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSFIELDS_COMPUTE_H
#define ZX_ACSFIELDS_COMPUTE_H

#include <cstdint>

namespace zx { namespace acsfields {

// [Doruk-Aksoy] The ACS function numbers, fua_ block 24030-24031.
enum Function
{
	kGetField = 24030,
	kSetField = 24031,
};

// [Doruk-Aksoy] DECORATE's value types for a field (thingdef_type.h's ExpValType), mirrored so this stays free of the engine.
enum ValueType
{
	kValInt = 0,
	kValFloat = 1,
	kValArray = 3,
	kValFixed = 11,
	kValAngle = 12,
	kValBool = 13,
};

// [Doruk-Aksoy] How a field's elements are stored: a 32-bit int, a 64-bit 48.16 fixed_t, a 32-bit angle_t, a bool or a double.
enum FieldKind
{
	kFieldNone,
	kFieldInt,
	kFieldFixed,
	kFieldAngle,
	kFieldBool,
	kFieldFloat,
};

struct FieldShape
{
	FieldKind kind;
	int count;			// [Doruk-Aksoy] 1 for a plain field, the length of an array.
	bool writable;		// [Doruk-Aksoy] Only int user_ variables and arrays, as SetUserVariable allows.
};

// [Doruk-Aksoy] A field's shape from its DECORATE type, array base type and length, and whether it is a user_ variable; kFieldNone when ACS can't read it.
FieldShape ComputeFieldShape(int type, int baseType, int arrayLength, bool userVar);

// [Doruk-Aksoy] Whether `index` names an element of the field.
bool ComputeFieldIndexOk(const FieldShape &shape, int index);

// [Doruk-Aksoy] A 48.16 fixed_t as ACS fixed point, its high bits lost past ±32767 units as GetActorX loses them.
int32_t ComputeFixedToAcs(int64_t raw);

// [Doruk-Aksoy] An angle_t as an ACS angle, a full turn being 1.0, as GetActorAngle gives it.
int32_t ComputeAngleToAcs(uint32_t bam);

// [Doruk-Aksoy] A double as ACS fixed point, truncated toward zero and wrapped like a fixed_t.
int32_t ComputeFloatToAcs(double value);

// [Doruk-Aksoy] Element `index` of a field stored at `address`, as ACS reads it; 0 for a bad index or kind.
int32_t ComputeFieldRead(const FieldShape &shape, const void *address, int index);

// [Doruk-Aksoy] Writes `value` to element `index` of a writable field at `address`; false when it isn't writable or the index is bad.
bool ComputeFieldWrite(const FieldShape &shape, void *address, int index, int32_t value);

}} // namespace zx::acsfields

#endif
