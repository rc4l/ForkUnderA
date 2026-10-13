// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A frame cut into the phases it spent its time in.

#ifndef ZX_FRAMEPHASE_COMPUTE_H
#define ZX_FRAMEPHASE_COMPUTE_H

namespace zx { namespace framephase {

// [rc4l] Where a frame's time goes; Upload and Lights are inside Backend, the backend's whole scene draw, and Display holds everything D_Display does, its Flip and the scene phases among it.
enum EFramePhase { kPhaseSim, kPhaseWalk, kPhaseRebake, kPhaseActors, kPhaseBackend, kPhaseUpload, kPhaseLights, kPhasePresent, kPhaseInput, kPhaseDisplay, kPhaseFlip, kPhaseCount };

// [rc4l] The phase's short name, for reports and CSV headers.
const char *ComputePhaseName(int phase);

}} // namespace zx::framephase

#endif // ZX_FRAMEPHASE_COMPUTE_H
