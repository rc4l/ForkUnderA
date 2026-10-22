// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A piece's texFlags, the one number that tells the scene shaders how GL's texture modes, styles and passes apply to it.

#ifndef ZX_TEXFLAGS_COMPUTE_H
#define ZX_TEXFLAGS_COMPUTE_H

namespace zx { namespace hwrender {

// [rc4l] Its bits, which the scene shaders test by these same values: single flags, then the fuzz shader in bits 7-9, the culled faces in bits 11-12, a surface refusing marks in bit 14 and one lit from the sector light table in bit 15.
enum
{
	kTexFlagStencil = 1, kTexFlagAlphaTest = 2, kTexFlagHud = 4, kTexFlagLevel = 8, kTexFlagInverse = 16, kTexFlagColorBlend = 32,
	kTexFlagFogLayer = 64, kTexFlagFogBoundary = 1024, kTexFlagFogPass = 8192, kTexFlagNoMarks = 16384, kTexFlagSectorLit = 32768,
	kTexFlagFuzzShift = 7, kTexFlagFuzzMask = 7, kTexFlagCullShift = 11, kTexFlagCullMask = 3,
};

// [rc4l] What sets them, from a MeshPiece and whether it is a level surface.
struct PieceTexInputs
{
	bool stencil, alphaTest, hud, level, inverse, colorBlend, fogLayer;
	int fuzz;
	bool fogBoundary;
	int cullFace;
	bool fogPass;
	bool noMarks;   // [rc4l] features/decals' surface rule refuses marks on it, which the G-buffer carries for the decal pass
};

// [rc4l] The texFlags for those inputs, the fuzz shader and culled faces each kept to its own bits.
unsigned ComputePieceTexFlags(const PieceTexInputs &in);

}} // namespace zx::hwrender

#endif // ZX_TEXFLAGS_COMPUTE_H
