// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/computation/decoratemath_compute.h"

#include <cctype>
#include <cmath>
#include <cstddef>

namespace zx { namespace decorateuzdoom {

namespace {

const double kPi = 3.14159265358979323846;
const double kToDegrees = 180.0 / kPi;
const double kToRadians = kPi / 180.0;

struct NamedSpec
{
	const char *name;
	MathSpec spec;
};

// [Doruk-Aksoy] uzdoom@62afe5f0a9 codegen.cpp: FxFlops take one argument, Min and Max two or more, Clamp three, ATan2 and VectorAngle two.
const NamedSpec kSpecs[] = {
	{ "Exp", { kMathExp, 1, 1 } },
	{ "Log", { kMathLog, 1, 1 } },
	{ "Log10", { kMathLog10, 1, 1 } },
	{ "Ceil", { kMathCeil, 1, 1 } },
	{ "Floor", { kMathFloor, 1, 1 } },
	{ "Round", { kMathRound, 1, 1 } },
	{ "ACos", { kMathACos, 1, 1 } },
	{ "ASin", { kMathASin, 1, 1 } },
	{ "ATan", { kMathATan, 1, 1 } },
	{ "Tan", { kMathTan, 1, 1 } },
	{ "CosH", { kMathCosH, 1, 1 } },
	{ "SinH", { kMathSinH, 1, 1 } },
	{ "TanH", { kMathTanH, 1, 1 } },
	{ "ATan2", { kMathATan2, 2, 2 } },
	{ "VectorAngle", { kMathVectorAngle, 2, 2 } },
	{ "Min", { kMathMin, 2, kAnyArgs } },
	{ "Max", { kMathMax, 2, kAnyArgs } },
	{ "Clamp", { kMathClamp, 3, 3 } },
};

bool SameName(const char *a, const char *b)
{
	for (; *a != 0 && *b != 0; a++, b++)
	{
		if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
			return false;
	}
	return *a == *b;
}

double Extreme(const double *args, size_t count, bool largest)
{
	double best = args[0];
	for (size_t i = 1; i < count; i++)
	{
		if (largest ? args[i] > best : args[i] < best)
			best = args[i];
	}
	return best;
}

} // namespace

MathSpec ComputeMathSpec(const char *name)
{
	if (name != nullptr)
	{
		for (const NamedSpec &s : kSpecs)
		{
			if (SameName(name, s.name))
				return s.spec;
		}
	}
	MathSpec none = { kMathNone, 0, 0 };
	return none;
}

bool ComputeMathResultIsInt(MathOp op, bool allArgsInt)
{
	return allArgsInt && (op == kMathMin || op == kMathMax || op == kMathClamp);
}

double ComputeMath(MathOp op, const double *args, size_t count)
{
	if (count == 0)
		return 0;
	const double v = args[0];
	switch (op)
	{
	case kMathExp: return std::exp(v);
	case kMathLog: return std::log(v);
	case kMathLog10: return std::log10(v);
	case kMathCeil: return std::ceil(v);
	case kMathFloor: return std::floor(v);
	case kMathRound: return std::round(v);
	case kMathACos: return std::acos(v) * kToDegrees;
	case kMathASin: return std::asin(v) * kToDegrees;
	case kMathATan: return std::atan(v) * kToDegrees;
	case kMathTan: return std::tan(v * kToRadians);
	case kMathCosH: return std::cosh(v);
	case kMathSinH: return std::sinh(v);
	case kMathTanH: return std::tanh(v);
	// [Doruk-Aksoy] ATan2 takes (y, x) and VectorAngle (x, y), both answering in degrees.
	case kMathATan2: return count < 2 ? 0 : std::atan2(args[0], args[1]) * kToDegrees;
	case kMathVectorAngle: return count < 2 ? 0 : std::atan2(args[1], args[0]) * kToDegrees;
	case kMathMin: return Extreme(args, count, false);
	case kMathMax: return Extreme(args, count, true);
	// [Doruk-Aksoy] UZDoom builds Clamp as Min(Max(v, lo), hi), so a lo above hi answers hi.
	case kMathClamp:
	{
		if (count < 3)
			return v;
		const double raised = v > args[1] ? v : args[1];
		return raised < args[2] ? raised : args[2];
	}
	default: return 0;
	}
}

double ComputeNormalize180(double degrees)
{
	double d = std::fmod(degrees, 360.0);
	if (d > 180.0)
		d -= 360.0;
	else if (d <= -180.0)
		d += 360.0;
	return d;
}

double ComputeGetDistance(double dx, double dy, double dz, double selfHeight, double targetHeight, bool checkz)
{
	const double z = checkz ? dz + (targetHeight - selfHeight) / 2 : 0;
	return std::sqrt(dx * dx + dy * dy + z * z);
}

double ComputeGetAngle(double fromX, double fromY, double toX, double toY, double lookerYaw, int flags)
{
	const double to = std::atan2(toY - fromY, toX - fromX) * kToDegrees;
	return (flags & kGafRelative) ? ComputeNormalize180(to - lookerYaw) : to;
}

void ComputeGetZAtPoint(double actorX, double actorY, double actorYaw, double px, double py, double angle, int flags,
	double *outX, double *outY)
{
	if (flags & kGzfAbsolutePos)
	{
		*outX = px;
		*outY = py;
		return;
	}
	if (!(flags & kGzfAbsoluteAng))
		angle += actorYaw;
	const double s = std::sin(angle * kToRadians), c = std::cos(angle * kToRadians);
	*outX = actorX + px * c + py * s;
	*outY = actorY + px * s - py * c;
}

double ComputeNextLowestFloor(double realFloor, const Plane3D *floors, size_t count, double z, double stepHeight, bool restrict3d)
{
	for (size_t i = 0; i < count; i++)
	{
		const Plane3D &ff = floors[i];
		if (!ff.solid)
			continue;
		if (ff.top > realFloor && (z >= ff.top || (!restrict3d && ff.bottom < z && ff.top < z + stepHeight)))
			return ff.top;
	}
	return realFloor;
}

double ComputeNextHighestCeiling(double realCeiling, const Plane3D *floors, size_t count, double bottomZ, double topZ)
{
	for (size_t i = count; i-- > 0;)
	{
		const Plane3D &ff = floors[i];
		if (!ff.solid)
			continue;
		const double middle = ff.bottom + (ff.top - ff.bottom) / 2;
		if (ff.bottom < realCeiling && std::fabs(bottomZ - middle) > std::fabs(topZ - middle))
			return ff.bottom;
	}
	return realCeiling;
}

}} // namespace zx::decorateuzdoom
