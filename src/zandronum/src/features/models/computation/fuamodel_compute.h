// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] FUAMODEL, ZandroX's own lump beside MODELDEF that other engines never read, so a mod can mark its models without losing them.

#ifndef ZX_FUAMODEL_COMPUTE_H
#define ZX_FUAMODEL_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx { namespace models {

// [Doruk-Aksoy] What a FUAMODEL lump says: the actor classes whose models are flat surfaces, and what it could not read, each with its line.
struct FuaModelDefs
{
	std::vector<std::string> flatSurfaces;
	std::vector<std::string> errors;
};

// [Doruk-Aksoy] Reads `FlatSurface Class [Class ...]` lines, keywords in any case, `//` comments to the line's end.
FuaModelDefs ComputeParseFuaModel(const char *text, size_t length);

}} // namespace zx::models

#endif // ZX_FUAMODEL_COMPUTE_H
