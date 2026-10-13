// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A frame cut into the phases it spent its time in, and what the slow frames spent more on than the rest.

#ifndef ZX_FRAMEPHASE_COMPUTE_H
#define ZX_FRAMEPHASE_COMPUTE_H

namespace zx { namespace framephase {

// [rc4l] Where a frame's time goes; Upload and Lights are inside Backend, which is the backend's whole scene draw.
enum EFramePhase { kPhaseSim, kPhaseWalk, kPhaseRebake, kPhaseActors, kPhaseBackend, kPhaseUpload, kPhaseLights, kPhasePresent, kPhaseCount };

// [rc4l] The phase's short name, for reports and CSV headers.
const char *ComputePhaseName(int phase);

// [rc4l] Mean microseconds per phase over the frames longer than `slowUs` and over the rest, from `n` frames of `phases` (kPhaseCount each) and their lengths; how many were slow.
int ComputeSlowFramePhases(const float *phases, const double *frameUs, int n, double slowUs, double *slowMean, double *restMean);

}} // namespace zx::framephase

#endif // ZX_FRAMEPHASE_COMPUTE_H
