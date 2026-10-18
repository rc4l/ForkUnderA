# features/jobs

The engine's one pool of worker threads, for work a frame splits across cores.

- `computation/jobpool_compute`: `JobPool`, a fork-join pool. `ParallelFor(count, fn)` runs `fn(index, lane)`
  once for each index. The main thread is lane 0 and each worker its own lane, so per-lane scratch needs no
  lock, and the call returns once every index has run. An idle worker polls for the next job for a short
  spin and then sleeps, so a job a frame later wakes a spinning worker at once. A worker that wakes after a
  job has closed leaves without touching it, so the caller never waits on a sleeper.
  `ParallelChunks(count, chunk, fn)` runs `fn(lo, hi, lane)` over `[0, count)` in chunks of at most `chunk`
  items, its bounds from `ComputeChunkCount` and `ComputeChunkBounds`, so a scan splits without working them
  out itself. `ComputeAutoWorkers` is the default count: half the logical processors less the caller, at most 7.
  A `JobObserver` (`SetJobObserver`) hears each worker start, each lane's run and the caller's wait, for
  `features/telemetry`.
- `computation/sharedarray_compute`: `SharedArray`, an array of relaxed atomics that a job's lanes mark at
  once (stamps, flags, the smallest key of a race with `LowerWithin`).
- `jobs.cpp`: `Pool()`, sized by `fua_jobs_threads` (-1 picks by the machine, 0 runs every job on the
  main thread). It is resized on the next call after the cvar changes and is never destroyed, so no worker
  is joined while the process exits. Its observer times `jobs/run` and `jobs/wait` into `features/telemetry`
  and names each worker "job N" in its traces.

## In-engine hooks

None: features call `zx::jobs::Pool()` from the main thread. `features/visibility` splits the player's
walk across it, and scans its marks into lists and walk bits on it; `features/sprites` derives the swept
actors' sprites on it.
