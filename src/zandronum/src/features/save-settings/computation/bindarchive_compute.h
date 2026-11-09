// [MobiusU9B] What a config save does with each key's binding, kept engine-free so it is unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_BINDARCHIVE_COMPUTE_H
#define ZX_BINDARCHIVE_COMPUTE_H

namespace zx {

// [MobiusU9B] Leave the key's ini entry as it is, remove it, or write the binding into it.
enum class EBindArchive { Skip, Clear, Write };

// [MobiusU9B] A matchcmd is a mod's KEYCONF action, which writes and claims the unclaimed keys bound to it; the general pass (NULL) clears empty keys, skips claimed ones and writes the rest.
EBindArchive ComputeBindArchive(const char *binding, const char *matchcmd, bool claimed);

} // namespace zx

#endif
