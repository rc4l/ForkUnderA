// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] When a step Continue armed for after a restart may run: only once that restart has happened, since wad_reload now asks for it and D_DoomLoop throws it a frame later.

#ifndef ZX_CONTINUERESTART_COMPUTE_H
#define ZX_CONTINUERESTART_COMPUTE_H

namespace zx
{

// [Doruk-Aksoy] Armed, the engine has restarted since the arming (restartsNow past armedAtRestart) and is no longer starting up.
bool ComputeAfterRestartDue(bool armed, int armedAtRestart, int restartsNow, bool startingUp);

} // namespace zx

#endif // ZX_CONTINUERESTART_COMPUTE_H
