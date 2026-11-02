// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The engine's one job pool, for work a frame splits across threads.

#ifndef ZX_JOBS_H
#define ZX_JOBS_H

#include "features/jobs/computation/jobpool_compute.h"

namespace zx { namespace jobs {

// [rc4l] The pool, its workers set by fua_jobs_threads (-1 picks by the machine) and resized on the next call after it changes, except in a browser, where the main thread may not wait for them; call it only from the main thread, between jobs.
JobPool &Pool();

// [Doruk-Aksoy] The game thread's own pool, its workers set by fua_jobs_tic_threads (-1 picks by the machine, at most 3), for reads a tic splits beside the frames; call it only from the thread running tics, between jobs.
JobPool &TicPool();

}} // namespace zx::jobs

#endif // ZX_JOBS_H
