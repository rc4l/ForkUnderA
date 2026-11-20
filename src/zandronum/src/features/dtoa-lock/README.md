# features/dtoa-lock

FString's `%g`/`%f` formatter (`zstrformat.cpp`) turns doubles into digits with gdtoa, which hands out its result
buffers from a global freelist and keeps a global list of powers of five. gdtoa is built with `MULTIPLE_THREADS`, but
its two lock macros were defined as nothing, so the lists had no lock. With `fua_gamethread` on, a tic formatting a
number (ACS `f:` in `StrParam`, `Print` or `Log`, a `Printf` with a float) and the main thread formatting one for the
frame took blocks from the freelist at once: the same block went to both, so a string got the other thread's digits or
garbage, and the freelist broke until an access violation. A script that formats a fixed-point number every tic hit it
in one of 24 games of about 90 seconds with bots on MM1CUT (MM8BDM v6c with CBM).

- `dtoalock.cpp`: `fua_dtoa_lock`/`fua_dtoa_unlock`, two spin locks (`std::atomic_flag`), which are constant
  initialised, since float CVar defaults are formatted during static initialisation.
- `dtoalock_test.cpp`: eight threads convert numbers through gdtoa and every result must equal the one-thread one
  (without the locks the test binary crashed 6 runs of 6); a second thread waits on a held lock.

UZDoom removed gdtoa instead, replacing the whole formatter with stb_sprintf
(uzdoom@fdad3186a6c15180c04e10d2da8f9ad3e3e8b3df); the locks are the smaller change and leave every printed number as it
was.

## Hooks

- `gdtoa/gdtoaimp.h`: `ACQUIRE_DTOA_LOCK(n)`/`FREE_DTOA_LOCK(n)` call `fua_dtoa_lock(n)`/`fua_dtoa_unlock(n)`.
- `src/CMakeLists.txt`: `dtoalock.cpp` in the engine's sources; `tests/CMakeLists.txt` builds gdtoa and the lock
  into the unit tests.
