# telemetry

One registry every feature reports its performance through. Counters are named `group/name` and read or fed once a frame. Timed regions work on any thread. A flight recorder keeps the last 2048 frames and the last 600 seconds. Console commands, files and the bridge read it, on every platform the engine builds for.

- `telemetry.h`/`.cpp`, the registry:
  - Registering: `Register` takes a `CounterSpec`. A counter is a gauge (a level read when sampled), a total (summed over a frame) or a timing (milliseconds). `RegisterBreakdown` adds readings made on request, such as memory by tag.
  - Feeding: `Add`, `Set`, `ZX_COUNT`, `ZX_ZONE` (a scope timed into a timing counter, and into the trace while one is captured) and `Region` (a span timed by hand).
  - Frames: `FrameMark` ends a frame and `TicMark` counts a game tic. `NameThisThread` names a thread in traces.
  - Hitches: a frame over twice the recent median and over `fua_telemetry_hitch_ms` (8) is kept with the regions it spent most in.
  - Turning it off: `fua_telemetry 0` stops the timed regions. The counters stay. An A/B on MAP01 showed no difference in frame time either way.
  - Console commands:
    - `fua_stats [json|watch] [prefix]`: the counters as a table, as JSON, or on screen through the stat overlay (`ADD_STAT(telemetry)`). `fua_stats watch off` hides them.
    - `fua_capture start [name] | stop`: every counter each frame and every timed region on every thread, written as `telemetry/<name>.csv` and `<name>-trace.json`. Perfetto and `chrome://tracing` open the trace.
    - `fua_flightdump [name]`: the flight recorder as `-frames.csv`, `-seconds.csv` and `-hitches.txt`, plus its summary.
    - `fua_hitches [n]`: the last slow frames.
    - `fua_memsnap [name]` and `fua_memdiff [a] [b]`: every gauge and breakdown kept under a name, and what changed between two snapshots or between one and now.
    - `fua_soak begin | mark | verdict [warmup] [json]`: a mark after each repetition of an action. The verdict is whether any count of bytes, objects, handles or threads kept growing past its warm-up, judged by `ComputeGrowthVerdict`. A peak and the OS's working set are not judged (`CounterSpec::soaked`).
  - Output: `SetFileOfferer` hands a written file to the player, which a browser does as a download. `CrashSummary` and `SetSummaryListener` give a crash report the last minute's frame times and memory.
- `allocwatch.cpp` replaces the global `operator new`/`delete` (still `malloc` and `free` underneath):
  - It counts `mem/allocations_live` and `mem/allocations_per_frame`.
  - `fua_allocsites start | report [n] | stop` records each new allocation's call stack from then on. A report lists the stacks still holding the most, named through the platform's symbols. That is how the userinfo leak below was found.
- `census.cpp`: the level's actors, thinkers, sectors, lines and subsectors, the textures and sounds loaded, the audio channels in use, and `mem/engine_heap_bytes` (`GC::AllocBytes`).
- `osprobe.h`, one file per platform that CMake picks (`FUA_TELEMETRY_PROBE`). A reading a platform cannot take stays negative and shows as `n/a`:
  - Windows: memory counters, handles, the process heap's summary, and DXGI's per-process video memory. Stacks come from `RtlCaptureStackBackTrace` and names from dbghelp.
  - Linux: `/proc/self/status`, open descriptors and glibc's `mallinfo2`.
  - macOS: the task's VM info, threads, descriptors and the malloc zones.
  - Linux and macOS share `osprobe_stack_posix.cpp`: stacks from `backtrace` and names from `dladdr`.
  - The browser:
    - wasm memory and the heap in use;
    - the JavaScript heap where the browser reports one;
    - every WebGPU buffer and texture the page made and has neither destroyed nor collected (`web/gpu.js` `track`), in all as `gpu/process_bytes` and by label as `gpu/page/`.
- `computation/telemetry_compute.h`, tested:
  - the frame ring and its column statistics;
  - the stats table and JSON, the hitch rule, and the CSV and Chrome trace formats;
  - snapshot diffs, the growth verdict, and the tag ledger memory is attributed with;
  - texture sizes, `/proc` field parsing, tab-separated readings, the flight summary, and per-key timers.
- `computation/allocsites_compute.h`: the allocation table `fua_allocsites` records into. It is fixed in size when made, so recording never allocates. Its stack text drops the allocator's own frames. Tested.

`fuactl` drives all of it (`tools/fuactl/src/telemetry.mjs`):
- Views of the console commands: `console`, `stats`, `record`, `flight`, `hitches`, `memsnap` and `memdiff`.
- `soak`: repeats an action, `reload` or any console text.
- `compare`: two `fua_stats json` readings side by side.
- `perfgate`: the counters at a view against this machine's saved baseline, kept under `~/.forkundera/baselines/`.

The `soak` scenario and the browser check "reloading the level holds no more memory" both run the soak. `tools/zone-tripwire.sh` keeps every counter named `group/name` and every timed region's name to one site.

## Found with it

- **Userinfo CVars leaked on every new game.** `G_NewInit` constructs each player again, which makes its userinfo CVars. `TMap::TransferFrom` then replaced them without deleting them. That was about 1,400 allocations a level load, found with `fua_allocsites`. `userinfo_t::TransferFrom` now deletes its own CVars before taking the others.
- **WebGPU buffers left for the garbage collector (queue #48).** The backend kept textures' and buffers' handles in one set. A browser numbers each kind from one, so a buffer that shared a kept texture's number was never destroyed and waited for the collector. Sunder MAP11 held up to five 18 MB vertex buffers at once. Two more changes to that buffer:
  - It is now kept while the level still fits it, rather than made again at each rebuild.
  - The level's big uploads go in 1 MB pieces, because Diligent's WebGPU device keeps every upload page it ever made.
  - Result: a reload now peaks at 184 MB of page-held GPU memory, against 293 MB before.

## In-engine hooks

- `features/framephase/framephase.cpp`: each phase feeds `phase/<name>`, and `FrameBoundary` calls `FrameMark`.
- `d_net.cpp` `TryRunTics`: `sim/gticker` and `TicMark`. `g_game.cpp` `G_Ticker`: `sim/pticker`. `p_tick.cpp` `P_Ticker`: `sim/thinkers`, `sim/specials` and `sim/effects`.
- `dthinker.cpp` `TickThinkers`: with `fua_telemetry_thinkers 1`, each thinker's tick by class, as the breakdown `sim/thinker/`.
- `features/visibility/viswalk.cpp`: `vis/scan`, `vis/clip` and `vis/replay`.
- `features/sprites/spritebuild.cpp`: `scene/sprites`. `features/levelmesh/walldecals.cpp`: `scene/decals`.
- `features/jobs/jobs.cpp`: a `JobObserver` (`jobpool_compute.h`) times `jobs/run` and `jobs/wait` and names each worker "job N".
- `features/hwrender/rendererrate.cpp`: vid_fps's split, as `frame/gl_ms` and `frame/backend_ms`.
- `features/hwrender/diligent/`:
  - `dgscene.cpp`: `draw/scene`, `draw/world` and the `frame/vsync` gauge.
  - `dg2d.cpp`, `dgsky.cpp`, `dgviews.cpp` and `dgdynamic.cpp`: `draw/2d`, `draw/sky`, `draw/mirrors` and `scene/dynamic`.
  - `dgframe.cpp`: `load/bake`, `load/upload`, `frame/present_ms`, and `fua_frametimes`'s per-frame parts as `levelmesh/*`.
  - `dggputimer.cpp`: `gpu/frame_ms`, `gpu/world_ms` and `gpu/2d_ms`, from duration queries.
  - `dgmemory.cpp`: every texture and buffer made through `CreateDeviceTexture`/`CreateDeviceBuffer`, as `gpu/mem/total_bytes`, `gpu/mem/objects` and the breakdown `gpu/mem/tag/`. At shutdown it reports what is still held.
  - `dgdevice_vk.cpp`: `VK_EXT_memory_budget` as `gpu/vk_budget_used_bytes` and `gpu/vk_budget_bytes`.
- `g_level.cpp`, `p_setup.cpp`, `textures/texturemanager.cpp` and `s_sound.cpp`: `load/level`, `load/setup`, `load/precache_textures`, `load/precache_sounds` and `audio/update`.
- `network.cpp`: `net/bytes_in`, `net/packets_in`, `net/bytes_out` and `net/packets_out`. `files.cpp` `FileReader::Read`: `io/read_bytes`.
- `features/crashreport/zx_crashreport.cpp`: the summary goes into the Sentry extras every ten seconds. `features/web/webcrash.cpp` hands it to the page each second for `web/crashreport.js`.
- `features/web/webhost.cpp`: the file offerer downloads what the registry writes (`Module.fuaOfferFile`).
- `mcp_rpc.cpp` `perf.counters`: every counter, as `fua_stats json` gives it.
- `src/CMakeLists.txt`: `telemetry.cpp`, `allocwatch.cpp` and `census.cpp` in the engine's sources, and the probe in `SYSTEM_SOURCES`.
