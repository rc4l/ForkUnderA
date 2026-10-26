// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] What ACS LineAttack fires from, and whether a script that cannot fire has been warned about yet.

#ifndef ZX_LINEATTACK_COMPUTE_H
#define ZX_LINEATTACK_COMPUTE_H

#include <vector>

namespace zx { namespace acs {

// [Doruk-Aksoy] LineAttack's shooter: the activator, every actor carrying the tid, or nothing.
enum { kLineAttackFromActivator = 0, kLineAttackFromTid = 1, kLineAttackNothing = 2 };

// [Doruk-Aksoy] Tid 0 means the activator, which a world script or one whose activator was removed does not have.
int ComputeLineAttackShooter(int tid, bool hasActivator);

// [Doruk-Aksoy] Whether this is `script`'s first warning, recording it in `warned` when it is.
bool ComputeFirstWarning(std::vector<int> &warned, int script);

}} // namespace zx::acs

#endif // ZX_LINEATTACK_COMPUTE_H
