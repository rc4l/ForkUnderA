# telemetry

One registry every feature reports its performance through. Counters are named `group/name` and read or fed once a frame. Timed regions work on any thread. A flight recorder keeps the last frames and seconds (`kFrameRows` and `kSecondRows` in `telemetry.cpp`). Console commands, files and the bridge read it, on every platform the engine builds for. `fua_stats` lists every counter there is, so this file names groups and where they come from rather than each counter.

- `telemetry.h`/`.cpp`, the registry:
  - Registering: `Register` takes a `CounterSpec`. A counter is a gauge (a level read when sampled, at most once its interval), a total (summed over a frame) or a timing (milliseconds). `RegisterBreakdown` adds readings made on request, such as memory by tag; one registered with `soaked` false is listed but not judged by a soak.
  - Feeding: `Add`, `Set`, `ZX_COUNT`, `ZX_ZONE` (a scope timed into a timing counter, and into the trace while one is captured) and `Region` (a span timed by hand). `TimedNs` is a timing's running total, for a reader that keeps its own intervals.
  - Frames: `FrameMark` ends a frame and `TicMark` counts a game tic. `NameThisThread` names a thread in traces.
  - Hitches: a frame over twice the recent median and over `fua_telemetry_hitch_ms` is kept with the regions it spent most in.
  - Turning it off: `fua_telemetry 0` stops the timed regions; the counters stay.
  - Console commands:
    - `fua_stats [json|watch] [prefix]`: the counters as a table, as JSON, or on screen through the stat overlay (`ADD_STAT(telemetry)`). `fua_stats watch off` hides them.
    - `fua_capture start [name] | stop`: every counter each frame and every timed region on every thread, written as `telemetry/<name>.csv` and `<name>-trace.json`. Perfetto and `chrome://tracing` open the trace.
    - `fua_flightdump [name]`: the flight recorder as `-frames.csv`, `-seconds.csv` and `-hitches.txt`, plus its summary.
    - `fua_hitches [n]`: the last slow frames.
    - `fua_memsnap [name]` and `fua_memdiff [a] [b]`: every gauge and breakdown kept under a name, and what changed between two snapshots or between one and now.
    - `fua_soak begin | mark | verdict [warmup] [json] [-prefix ...]`: a mark after each repetition of an action, and whether any count of bytes, objects, handles or threads kept growing past the warm-up (`ComputeSoakVerdict`). A counter registered with `CounterSpec::soaked` false, such as a peak or the heaps' committed bytes, which settle rather than leak, is not judged, and neither is a reading under a `-prefix`. The JSON carries each growth's line and the whole report.
  - Output: `SetFileOfferer` hands a written file to the player, which a browser does as a download. `SetSummaryListener` hears the last minute's frame times and memory each second, for a crash report.
- `allocwatch.cpp` replaces the global `operator new`/`delete` (still `malloc` and `free` underneath):
  - It counts the allocations live and made each frame.
  - `fua_allocsites start | report [n] | stop` records each new allocation's call stack from then on. A report lists the stacks still holding the most, named through the platform's symbols.
- `census.cpp`: what the level and the engine's tables hold, the audio channels in use, and the engine's own heap (`GC::AllocBytes`).
- `osprobe.h`, one file per platform that CMake picks (`FUA_TELEMETRY_PROBE`). A reading a platform cannot take stays negative and shows as `n/a`:
  - Windows: memory counters, handles, the process heap's summary and every heap's (allocated and committed, so fragmentation shows as their gap), DXGI's per-process video memory, and the breakdown `mem/win/`, committed memory by kind (private read-write, executable, other, mapped), which places growth outside every heap. Stacks come from `RtlCaptureStackBackTrace` and names from dbghelp, whose session `SymbolNames` opens per call under `DbgHelpLock`, which the bridge's sampler holds too.
  - Linux: `/proc/self/status` (read through `features/fileio`), open descriptors and glibc's heap statistics.
  - macOS: the task's VM info, threads, descriptors and the malloc zones.
  - Linux and macOS share `osprobe_stack_posix.cpp`: stacks from `backtrace` and names from `dladdr`.
  - The browser: wasm memory and the heap in use, the JavaScript heap where the browser reports one, and every WebGPU buffer and texture the page made and has neither destroyed nor collected (`web/gpu.js` `track`), in all as `gpu/process_bytes` and by label as `gpu/page/`.
- `computation/telemetry_compute.h`, tested: everything above that is arithmetic or text, from the frame ring and its statistics to the soak verdict, the hitch report, the tag ledger, texture sizes and the throttle gauges are read under.
- `computation/allocsites_compute.h`: the allocation table `fua_allocsites` records into. It is fixed in size when made, so recording never allocates. Its stack text drops the allocator's own frames. Tested.

`fuactl` drives all of it (`tools/fuactl/src/telemetry.mjs`):
- Views of the console commands: `console`, `stats`, `record`, `flight`, `hitches`, `memsnap` and `memdiff`.
- `soak`: repeats an action, `reload` or console texts separated by `|`, each that starts a level waited on and `check: resize WxH` sizing the window; prints each cycle's time and frame time and fails on growth or a slowdown; `--ignore` leaves readings out, and `--judge-port` judges another instance, a server the soaking one joins and leaves.
- `compare`: two `fua_stats json` readings side by side.
- `perfgate`: the counters at a view against this machine's saved baseline, kept under `~/.forkundera/baselines/`.

The `soak`, `wadsoak` and `wadsoaktitle` scenarios, the browser checks on reloading and on what a level change lets go of, and `tools/web/probe.mjs --soak` run the soak. `tools/zone-tripwire.sh` keeps every counter named `group/name`, every timed region's name to one site, and every zone `perf.ticprof` reads timed.

## Found with it

- Every new game leaked each player's userinfo CVars, because `TMap::TransferFrom` replaced them without deleting them (`userinfo_t::TransferFrom`), found with `fua_allocsites`.
- A browser numbers WebGPU textures and buffers apart, so a buffer sharing a kept texture's handle was left for the garbage collector (queue #48), found by comparing `gpu/page/` with `gpu/mem/tag/`.
- A `wad_reload` restart leaked what startup makes and only exit freed: actor light lists, the Huffman codec, the client's net buffer, weapon order entries and the startup screen; a music change leaked its reader and WAD handle, and a sound load a reader; the backend kept its textures for freed materials at the title. Found by soaking reloads and diffing two `fua_allocsites` reports.

## In-engine hooks

- `features/framephase/framephase.cpp`: each phase feeds `phase/<name>`, and `FrameBoundary` calls `FrameMark`.
- `d_net.cpp` `TryRunTics`, `g_game.cpp` `G_Ticker` and `p_tick.cpp` `P_Ticker`: the `sim/` zones and `TicMark`, which the bridge's `perf.ticprof` reads (`features/mcp-bridge/computation/ticprof_compute.h` names them).
- `dthinker.cpp` `TickThinkers`: with `fua_telemetry_thinkers 1`, each thinker's tick by class, as the breakdown `sim/thinker/`.
- `features/visibility/viswalk.cpp`: the `vis/` zones.
- `features/sprites/spritebuild.cpp` and `features/levelmesh/walldecals.cpp`: `scene/` zones.
- `features/jobs/jobs.cpp`: a `JobObserver` (`jobpool_compute.h`) on telemetry's clock times the `jobs/` regions and names each worker "job N".
- `features/hwrender/rendererrate.cpp`: vid_fps's split, under `frame/`.
- `features/hwrender/diligent/`:
  - `dgscene.cpp`, `dg2d.cpp`, `dgsky.cpp`, `dgviews.cpp` and `dgdynamic.cpp`: the `draw/` and `scene/` zones and the `frame/vsync` gauge.
  - `dgframe.cpp` and `dgupload.cpp`: the `load/` zones, the present interval and `fua_frametimes`'s per-frame parts under `frame/` and `levelmesh/`, and the scene buffer's rebuilds.
  - `dggputimer.cpp`: each pass's GPU time under `gpu/`, from duration queries.
  - `dgmemory.cpp`: every texture and buffer made through `CreateDeviceTexture`/`CreateDeviceBuffer`, under `gpu/mem/` and the breakdown `gpu/mem/tag/`. At shutdown it reports what is still held.
  - `dgdevice_vk.cpp`: `VK_EXT_memory_budget` under `gpu/vk_`, the device's heaps and the host's apart.
  - `dgscene.cpp`'s `CompileShader` and `CreatePipeline`: the shaders and pipelines made, under `gpu/made/`.
- `g_level.cpp`, `p_setup.cpp`, `textures/texturemanager.cpp` and `s_sound.cpp`: the `load/` zones and `audio/update`.
- `network.cpp`: the `net/` totals. `files.cpp` `FileReader::Read`: `io/read_bytes`.
- `features/crashreport/zx_crashreport.cpp`: the summary goes into the Sentry extras, throttled. `features/web/webcrash.cpp` hands it to the page for `web/crashreport.js`.
- `features/web/webhost.cpp`: the file offerer downloads what the registry writes (`Module.fuaOfferFile`).
- `mcp_rpc.cpp` `perf.counters`: every counter, as `fua_stats json` gives it. `mcp_sample.cpp`: holds `DbgHelpLock` around its dbghelp session.
- `src/CMakeLists.txt`: `telemetry.cpp`, `allocwatch.cpp` and `census.cpp` in the engine's sources, and the probe in `SYSTEM_SOURCES`.
