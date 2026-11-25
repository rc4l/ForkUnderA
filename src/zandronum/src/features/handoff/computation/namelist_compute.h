// SPDX-License-Identifier: GPL-3.0-or-later
//
// [MGOOOOOO] Names compared without case, such as the font names the main thread found nothing for, so a tic asks about each once.

#ifndef ZX_NAMELIST_COMPUTE_H
#define ZX_NAMELIST_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace handoff {

// [MGOOOOOO] Whether names holds name, ignoring case; a null name is never held.
bool ComputeNameListHas(const std::vector<std::string> &names, const char *name);

// [MGOOOOOO] Adds name unless names holds it already or it is null.
void ComputeNameListAdd(std::vector<std::string> &names, const char *name);

}} // namespace zx::handoff

#endif
