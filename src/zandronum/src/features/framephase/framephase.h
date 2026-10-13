// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Per-frame phase timing any module marks its work with, read by fua_viewtrace; a scope costs two clock reads.

#ifndef ZX_FRAMEPHASE_H
#define ZX_FRAMEPHASE_H

#include "features/framephase/computation/framephase_compute.h"

namespace zx { namespace framephase {

// [rc4l] Microseconds now on the clock the phases are timed with.
long long NowUs();

// [rc4l] Add `us` to this frame's phase.
void AddPhase(int phase, long long us);

// [rc4l] The frame ends: its phases become the last frame's and the next frame starts from nothing.
void FrameBoundary();

// [rc4l] The last whole frame's microseconds per phase (kPhaseCount of them).
const float *LastFramePhases();

// [rc4l] Times the scope it lives in into a phase.
struct Scope
{
	int phase;
	long long start;
	explicit Scope(int p) : phase(p), start(NowUs()) {}
	~Scope() { AddPhase(phase, NowUs() - start); }
};

}} // namespace zx::framephase

#endif // ZX_FRAMEPHASE_H
