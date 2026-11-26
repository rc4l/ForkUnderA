// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_DECORATEMATH_COMPUTE_H
#define ZX_DECORATEMATH_COMPUTE_H

#include <cstddef>

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] UZDoom's DECORATE math builtins that ours lacked (codegen.cpp FxFlops, FxMinMax, FxATan2).
enum MathOp
{
	kMathNone,
	kMathExp,
	kMathLog,
	kMathLog10,
	kMathCeil,
	kMathFloor,
	kMathRound,
	kMathACos,
	kMathASin,
	kMathATan,
	kMathTan,
	kMathCosH,
	kMathSinH,
	kMathTanH,
	kMathATan2,
	kMathVectorAngle,
	kMathMin,
	kMathMax,
	kMathClamp,
};

// [Doruk-Aksoy] A builtin's operation and how many arguments it takes, maxArgs kAnyArgs for Min and Max.
struct MathSpec
{
	MathOp op;
	unsigned minArgs;
	unsigned maxArgs;
};

const unsigned kAnyArgs = 0xFFFFFFFFu;

// [Doruk-Aksoy] The builtin a call's name names, case-insensitively, or kMathNone.
MathSpec ComputeMathSpec(const char *name);

// [Doruk-Aksoy] Whether the result is an int: Min, Max and Clamp of ints only, as UZDoom's FxMinMax keeps them.
bool ComputeMathResultIsInt(MathOp op, bool allArgsInt);

// [Doruk-Aksoy] The builtin's value for count arguments, angles in degrees as DECORATE takes and gives them.
double ComputeMath(MathOp op, const double *args, size_t count);

// [Doruk-Aksoy] An angle in degrees brought into -180..180, as UZDoom's deltaangle and AngleTo give it.
double ComputeNormalize180(double degrees);

// [Doruk-Aksoy] UZDoom's GetDistance: the 3D length from self to target, z between their middles under checkz, else the 2D one.
double ComputeGetDistance(double dx, double dy, double dz, double selfHeight, double targetHeight, bool checkz);

// [Doruk-Aksoy] UZDoom's GAF_ flags for GetAngle.
enum GetAngleFlags
{
	kGafRelative = 1,
	kGafSwitch = 2,
};

// [Doruk-Aksoy] UZDoom's GetAngle: the angle from one actor to the other in degrees, relative to the looker's yaw under GAF_RELATIVE.
double ComputeGetAngle(double fromX, double fromY, double toX, double toY, double lookerYaw, int flags);

// [Doruk-Aksoy] UZDoom's GZF_ flags for GetZAt.
enum GetZAtFlags
{
	kGzfAbsolutePos = 1,
	kGzfAbsoluteAng = 1 << 1,
	kGzfCeiling = 1 << 2,
	kGzf3DRestrict = 1 << 3,
	kGzfNoPortals = 1 << 4,
	kGzfNo3DFloor = 1 << 5,
};

// [Doruk-Aksoy] Where GetZAt looks: (px, py) itself under GZF_ABSOLUTEPOS, else that offset turned by angle (plus the actor's yaw unless GZF_ABSOLUTEANG) from the actor.
void ComputeGetZAtPoint(double actorX, double actorY, double actorYaw, double px, double py, double angle, int flags,
	double *outX, double *outY);

// [Doruk-Aksoy] One 3D floor's top and bottom at a point, in the sector's ffloors order, and whether it exists and is solid.
struct Plane3D
{
	double top, bottom;
	bool solid;
};

// [Doruk-Aksoy] UZDoom's NextLowestFloorAt without portals: the first solid 3D floor beneath z, or a step's height into it unless restrict3d, else the sector's floor.
double ComputeNextLowestFloor(double realFloor, const Plane3D *floors, size_t count, double z, double stepHeight, bool restrict3d);

// [Doruk-Aksoy] UZDoom's NextHighestCeilingAt without portals: the bottom of the last-listed solid 3D floor nearer the actor's top than its feet, else the sector's ceiling.
double ComputeNextHighestCeiling(double realCeiling, const Plane3D *floors, size_t count, double bottomZ, double topZ);

}} // namespace zx::decorateuzdoom

#endif // ZX_DECORATEMATH_COMPUTE_H
