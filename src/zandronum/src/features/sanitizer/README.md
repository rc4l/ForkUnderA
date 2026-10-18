# sanitizer

`sanitizer.h` defines `ZX_THREAD_SANITIZER` in a ThreadSanitizer build (`ZX_TSAN=1 ./linux_compile.sh`, the game thread's CI job). Code that would hide something from ThreadSanitizer steps aside for it there.

## Hooks

- `posix/sdl/i_fpslimit.cpp`: the frame cap's `SIGEV_THREAD` timer is left out, since ThreadSanitizer cannot follow the thread glibc starts for it, so the build runs uncapped.
- `features/telemetry/allocwatch.cpp`: the global `operator new`/`delete` are not replaced, so ThreadSanitizer's own stay in place and a library's allocations are judged by its suppressions in `tools/tsan.supp` rather than reported as the engine's. The allocation counters read 0 in that build.
