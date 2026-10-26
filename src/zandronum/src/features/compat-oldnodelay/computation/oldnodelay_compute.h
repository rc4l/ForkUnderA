// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] compat_fua_oldnodelay's tic rule: a NoDelay spawn state timed as Zandronum 3.3 timed it.

#ifndef ZX_OLDNODELAY_COMPUTE_H
#define ZX_OLDNODELAY_COMPUTE_H

namespace zx { namespace compat {

// [Doruk-Aksoy] The tics after Zandronum 3.3 re-entered a NoDelay spawn state of `startTics`: one more than SetState left when both last, so the state still runs its whole duration after its action.
int ComputeOldNoDelayTics(int startTics, int tics);

}} // namespace zx::compat

#endif // ZX_OLDNODELAY_COMPUTE_H
