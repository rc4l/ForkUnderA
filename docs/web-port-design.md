# FUA in a browser: design, start to finish

**Target:** FUA running single player in a desktop browser, served from `localhost`.
- It loads the engine pk3s plus Freedoom (or the player's own IWAD), draws through Diligent on WebGPU, and plays with sound, mouse look, and saves that survive a reload.
- Online play and public hosting are out of scope here. `docs/web-port-scope.md` has the inventory, what isn't possible, and the guard rails this design follows.

Paths are relative to `src/zandronum/src/` unless noted. The order is deliberate:
1. Prove the risks first.
2. Cut every seam on desktop, where the scenario suite and fuactl can verify it.
3. Bring WebGPU up natively against Vulkan.
4. Only then assemble the Emscripten build.

## 1. Toolchain and decisions

- **1a.** Pick an Emscripten version that still provides the WebGPU binding Diligent links (`-s USE_WEBGPU=1`, `deps/DiligentCore/Graphics/GraphicsEngineWebGPU/CMakeLists.txt:181-182`). If none current does, patch Diligent's web build to the emdawnwebgpu port. Record the pinned version in the build script.
- **1b.** Install emsdk at that version, outside the repo, with its location given by an environment variable as the coverage tool's is (`FUACTL_OPENCPPCOVERAGE`).
- **1c.** Choose exception handling: native WebAssembly exceptions (`-fwasm-exceptions`). The engine's restart and error paths throw (`CRestartException`, `d_main.cpp:1360-1363, 3661`; `CDoomError`, `doomerrors.h`), and there are 63 `throw` sites outside `gl/`. There is no `setjmp`/`longjmp`.
- **1d.** Choose the main-loop model: a one-frame step called by `emscripten_set_main_loop`, so the build doesn't depend on JSPI (Chrome only) or ASYNCIFY (size and speed cost).
- **1e.** Choose threads for the first build.
  - Diligent forces `-pthread -mbulk-memory` on web (`deps/DiligentCore/CMakeLists.txt:388-392`), so the page needs cross-origin isolation (COOP `same-origin`, COEP `require-corp`).
  - Locally, the dev server sends those headers, so keep Diligent's flags.
  - The engine's job pool still runs with `fua_jobs_threads 0` until threads are measured (`features/jobs/jobs.cpp:26`).
- **1f.** Choose the file system: IDBFS mounted over the config and save directory, synced after writes. WASMFS with OPFS is the later upgrade.
- **1g.** Choose the bundled game: Freedoom Phase 2 (BSD), shipped beside the engine pk3s, with its licence on the page.

## 2. Spike: prove the risky parts before building on them

- **2a.** Add a debug command that writes every composed Diligent shader source to disk, scene variants included (defines spliced after `#version`, `features/hwrender/diligent/dgscene.cpp:244`).
- **2b.** Build Diligent's Tint natively (fetched by `deps/DiligentCore/ThirdParty/dawn/CMakeLists.txt:17-46`), plus glslang, as standalone converters.
- **2c.** Push every dumped source through GLSL → SPIR-V → WGSL, and list each failure with its construct.
- **2d.** Confirm the known failures and look for others:
  - `nonuniformEXT` and `uMaterials[]` (`dgscene_glsl.h:142, 198, 233, 466`);
  - 23 inter-stage locations (`dgscene_glsl.h:26-48`);
  - depth read through `sampler2D` (`dgdecals.cpp:180, 474-478`);
  - ray queries (`dgviews.cpp:479-487`).
- **2e.** Build Diligent's WebGPU backend under Emscripten from our CMake, and clear a canvas in a bare test page. This proves the binding choice (1a) and the build flags.
- **2f.** In that test page, confirm how adapter and device creation finish. Diligent spins on `IsReady` with a no-op event pump on web (`EngineFactoryWebGPU.cpp:102-107, 183-184, 328-329`), which can't finish inside a single browser task.
  - **Decided:** the page creates the device before `main` runs, with the adapter's full limits, and the engine attaches to it (`AttachToWebGPUDevice`, `emscripten_webgpu_get_device()`). This is what Diligent's own web samples do. `tools/web/webgpu-check` proves it.
- **2g.** Write the spike's findings into `docs/web-port-scope.md` under its risks.

## 3. Build profiles

- **3a.** Add CMake presets (`CMakePresets.json` at the repo root) for named profiles: `desktop`, `server`, `offline`, `nogl-vulkan` and `web`. Each sets the switches (`NO_GL`, `FUA_DILIGENT`, `FUA_MCP_BRIDGE`, `NO_REPLAY`, `ZX_ENABLE_SENTRY`, `NO_GTK`, and the new gates below).
- **3b.** Point `linux_compile.sh`, `mac_compile.sh` and the Windows scripts at their presets, so a profile is defined in one place.
- **3c.** Build `offline` and `nogl-vulkan` in PR CI beside the existing jobs. Add `web` once section 6 links.

## 4. Desktop seams: loop, clock and quitting

- **4a.** Split `D_DoomLoop` (`d_main.cpp:1354-1499`) into `D_RunFrame`, one pass of the loop body, plus a desktop driver that calls it until exit. The scenario suite must stay green.
- **4b.** Move the restart handling (`do{}while(1)` with `CRestartException`, `d_main.cpp:3054-3696`) into the driver, so a restart is a driver state rather than a throw out of an endless loop.
  - **Decided:** with a driver installed (`features/mainloop`), `D_DoomLoop` hands `D_RunFrame` to it and unwinds `D_DoomMain` to `main` with `SteppedLoopStarted`.
  - A restart the frame callback catches reloads the page with the new arguments. The engine's own restart re-initialises everything anyway, so `D_DoomMain`'s restart loop stays as it is.
- **4c.** Turn the screen-wipe loop (`d_main.cpp:1253-1267`, `I_WaitVBL(2)` between passes) into a state the frame step advances.
- **4d.** Make the quit sequence non-blocking: `I_WaitVBL(105)` for the quit sound (`menu/messagebox.cpp:420`), then `ST_Endoom` → `exit(0)` (`posix/sdl/st_start.cpp:350-354`), becomes a timed state ending in a driver-owned shutdown.
- **4e.** Add a clock implementation beside the signal-based and polled ones in `I_SelectTimer` (`posix/sdl/i_system.cpp:268-296`), driven by a monotonic millisecond source. The tic arithmetic goes in a tested `ComputeTicsDue`.
- **4f.** Replace the FPS limiter's `timer_create(SIGEV_THREAD)` (`posix/sdl/hardware.cpp:311-341`), whose semaphore nothing on SDL waits on, with a frame-skip decision in the driver (`ComputeFrameDue`, tested).
- **4g.** Make `TryRunTics`' waits (`d_net.cpp:1756-1758, 1870-1894`) return to the driver instead of blocking when the clock says no tic is due.
- **4h.** Done when the desktop driver behaves exactly as `D_DoomLoop` did, the full scenario suite passes, and `fuactl` timing shows no change.

## 5. Desktop seams: storage, transport and the online module

- **5a.** Put config, save, screenshot and cache paths behind one storage seam:
  - `m_specialpaths.cpp:472-550` (`~/.config/zandronum`, the shared cache);
  - `HOME` reads (`d_main.cpp:2184`);
  - `getpwuid` (`cmdlib.cpp:896`).

  The desktop implementation is today's code.
- **5b.** Split `NETWORK_Construct` (`network.cpp:281-460`): socket creation and binding (`320-375`, `412`) apart from Huffman, message buffers and lump checksums (`289, 429-460`).
- **5c.** Put socket send and receive behind a transport interface (UDP today), chosen at startup. Add an offline transport that sends nothing and receives nothing, so the per-tic `recvfrom` calls (`g_game.cpp:1655-1657` → `network.cpp:824, 916`) cost nothing.
- **5d.** Add an offline switch that skips:
  - the startup server-browser registry query (`d_main.cpp:1471` → `browser.cpp:2126-2139`, including its main-thread DNS at `networkshared.cpp:725` and `features/net/zx_resolve.cpp:52`);
  - the update check (`d_main.cpp:1366`).
- **5e.** Gather the always-compiled online features into one CMake source list, `ONLINE_SOURCES` (`CMakeLists.txt:1479-1592`):
  - server hosting, port mapping, wad-serve, wad-download and the wad library;
  - server-browser fetching, the updater and the registry list;
  - identity and the curl net layer (`features/net/zx_httpsget.cpp`, `zx_httpfile.cpp`).
- **5f.** Write one stub file, `features/offline/offlinestubs.cpp`, answering the entry points shared code calls into that list, compiled against the same headers. Grow it only from linker evidence in the `offline` profile.
- **5g.** Give VoIP its own CMake switch (Opus, rnnoise, `voicechat.cpp`), separate from `NO_SOUND` (`CMakeLists.txt:640-644`). The `offline` profile turns it off.
- **5h.** Make SRP accounts (`csrp`, the reason OpenSSL is `REQUIRED`, `CMakeLists.txt:631-632`) optional. Move file hashing for wad identity (`features/wad-download/zx_filehash.cpp`) to the in-tree MD5, and leave OpenSSL out of `offline`.
- **5i.** Keep GeoIP out of client profiles; only the server uses it (`network.cpp:725`). Keep sqlite, which ACS's database functions need (`za_database.cpp`, `d_main.cpp:3444`).
- **5j.** Done when the `offline` profile links and passes the scenario suite natively with no socket opened (checked with `fuactl` and the OS's socket list).

## 6. Desktop seams: a build with no desktop GL

- **6a.** Allow `FUA_DILIGENT` with `NO_GL` (lift the forced-off rule, `src/zandronum/CMakeLists.txt:150-153`), for the `nogl-vulkan` and `web` profiles.
- **6b.** Split `posix/sdl/sdlglvideo.cpp`, which is wholly `#ifndef NO_GL`, into window and input-facing video code (window creation, `I_EngineWindow`, mode handling) and the GL context and framebuffer code.
- **6c.** Give `DiligentFrameBuffer` (`features/hwrender/backendvideo.cpp:26-35, 106-121`) a GL-free base class for SDL, in place of `SDLGLFB`.
- **6d.** Move the `gl/` includes in `backendvideo.cpp:6` and `features/frame/levelrenderer.cpp:4, 18` behind named adapters. `gl_PreprocessLevel` and `gl_CleanLevelData` (`levelrenderer.cpp:70, 81`) already have `NO_GL` stubs in `posix/sdl/glstubs.cpp`.
- **6e.** Wrap no further feature code in `#ifndef NO_GL`. Where a feature needs GL (`fua_warmup.cpp:21`, `hitboxviz.cpp:128, 217`, `standaloneframe.cpp`'s `CollectBackendFrame` call), move the GL half behind its own file chosen by CMake.
- **6f.** Done when `nogl-vulkan` builds on Linux in CI and a `-vulkan` launch of it passes the scenario suite's Vulkan sides.

## 7. WebGPU as a third backend on desktop

- **7a.** Stop forcing Diligent's WebGPU backend off for desktop (`src/zandronum/CMakeLists.txt:181-187`). Link `Diligent-GraphicsEngineWebGPU` and Dawn (`GraphicsEngineWebGPU/CMakeLists.txt:138`) beside the Vulkan engine.
- **7b.** Add a WebGPU value to `vid_backend` and a `-webgpu` flag (`features/hwrender/computation/backendchoice_compute.h`), with tests.
- **7c.** Create the device and contexts by backend in `dgprobe.cpp:151-180` (`GetEngineFactoryWebGPU`, `CreateDeviceAndContextsWebGPU`). The Vulkan-only limits query (`dgprobe.cpp:65-71`) stays behind its existing cast check.
- **7d.** Create the swapchain by backend in `dgwindow.cpp:129-157` (`CreateSwapChainWebGPU`).
- **7e.** Add `ComputeRenderTier` in `features/hwrender/computation/`. It maps device capabilities to the tier's flags (bindless, resource arrays, ray tracing, depth clamp, inter-stage variable limit, storage buffers per stage), with tests, and a cvar can force any tier on Vulkan.
- **7f.** Make the texture array a tier define in the scene shaders: `GL_EXT_nonuniform_qualifier` and `uMaterials[]` only in the bindless tier (`dgscene_glsl.h:142, 198, 233, 466`; `dgscene.cpp:241-243`). The resource layout drops `uMaterials` outside it (`dgpipelines.cpp:761, 778`).
- **7g.** In the non-array tier, draw through the per-batch `uTex`/`uBrightmap` binding the non-bindless path uses (`fua_dg_bindless`, `dgscene.cpp:174`; `BindlessActive`, `dgpipelines.cpp:504`), and check the batch count it costs on the heaviest maps.
- **7h.** The scene vertex-to-pixel interface uses 23 locations. Desktop adapters allow 28, so the device asks for the adapter's limit and startup checks it. For a device capped at 16, packing scalars into shared vectors, or reading per-piece fields from the `Pieces` buffer in the pixel stage, is the fallback.
- **7i.** Read scene depth in the decal pass as a depth texture with a non-filtering sampler (`dgdecals.cpp:180, 474-478`).
- **7j.** Keep ray-traced mirrors off in the WebGPU tier. The planar path (`kMirrorPlanarPS`, `dgviews.cpp:637`) already covers them.
- **7k.** Request depth-clip control when the tier wants depth clamp. Diligent asks for it only with `DepthBiasClamp` (`EngineFactoryWebGPU.cpp:252`), so patch it or accept GL-style clipping in that tier.
- **7l.** Make frame pacing tier-aware. The per-frame fence wait (`dgscene.cpp:635-640`) and `IdleGPU` (`dgwindow.cpp:211`) are no-ops on the web, so check every dynamic-buffer ring for reuse while the GPU may still read it, using WebGPU's queue ordering.
- **7m.** Make read-back asynchronous (buffer map callbacks) for screenshots, save pictures and replay (`dgdiag.cpp:265-361`; callers `backendvideo.cpp:210`, `dgcamera.cpp:86-98`), since `WaitForIdle` doesn't exist on web.
- **7n.** Keep GLDEFS user shaders on their probe compile (`dgpipelines.cpp:622-663`), so one that fails WGSL conversion is dropped rather than failing the pipeline.
- **7o.** Add a WebGPU side to the scenario harness: `fua_diligent_shot` captures any Diligent device, so `tools/fuactl` gains a `wgpu` frame beside `gl` and `vk`. Compare WebGPU against Vulkan live, with the existing per-box tolerances.
- **7p.** Done when the whole scenario suite passes WebGPU against Vulkan on Windows, and fuactl shows no CPU regression on the heaviest maps.

## 8. The Emscripten build

- **8a.** Add a `web` branch to `CMakeLists.txt` for Emscripten.
  - The platform is `posix/sdl/` (`i_input.cpp`, `i_joystick.cpp`, `i_main.cpp`, `i_system.cpp`, `hardware.cpp`) without `crashcatcher.c` (`CMakeLists.txt:704`), plus `features/web/`.
  - The Diligent platform source is `dgweb.cpp`, through `FUA_DILIGENT_PLATFORM_SOURCES` (`CMakeLists.txt:1997-2013`).
- **8b.** Libraries:
  - Use `-sUSE_SDL=2` for SDL2 and `-lopenal` for Emscripten's OpenAL.
  - zlib, bzip2, jpeg, lzma, GME, dumb, gdtoa and sqlite build from the tree.
  - FluidSynth, FFmpeg, sentry, GTK, OpenSSL, Opus, rnnoise, GeoIP and curl stay off, per the `web` preset.
- **8c.** Import host tools (zipdir, lemon, re2c, updaterevision) from a native build through `IMPORT_EXECUTABLES` (`src/zandronum/CMakeLists.txt:28-34`).
- **8d.** Guard the two empty `asm("")` barriers for wasm (`p_acs.cpp:2923`, `parsecontext.cpp:162`). Check that `x86.cpp` takes its non-x86 path (`x86.cpp:10`) and that `nodebuild.cpp` builds with `DISABLE_SSE`.
- **8e.** Link flags:
  - `-fwasm-exceptions`;
  - `-sALLOW_MEMORY_GROWTH=1` with an initial heap sized from a desktop run's peak;
  - a stack big enough for the BSP and node recursion (measured, not guessed);
  - `-sEXIT_RUNTIME=0`;
  - the `--preload-file` or fetch list from section 10.
- **8f.** Add `web_compile.sh` beside `linux_compile.sh` and `mac_compile.sh`. It sources emsdk, configures the `web` preset, builds, and writes `dist-web/` (the wasm, JS glue, data and page).
- **8g.** Done when `web_compile.sh` produces a module that links with no unresolved symbols, and every leftover stub has been traced to a decision recorded in section 5 or 6.

## 9. `features/web/`: the browser glue

- **9a.** Loop driver: `emscripten_set_main_loop` (or its argument-taking form) calls `D_RunFrame` once per animation frame, and is cancelled by the driver-owned shutdown from 4d.
- **9b.** Clock: the 4e seam's web implementation reads `emscripten_get_now`.
- **9c.** Storage: the 5a seam's web implementation.
  - Mount IDBFS over the config and save directory at startup, and load it before the engine reads its config (`FS.syncfs(true)`, finished before main runs).
  - Flush it after config writes and saves (`FS.syncfs(false)`).
- **9d.** Arguments: the page's URL query becomes `argv` (`?map=MAP01&file=...&iwad=...`), through a tested `ComputeArgsFromQuery`.
- **9e.** IWAD choice: replace the GTK, kdialog and stdin pickers (`posix/sdl/i_system.cpp:515-734`) with the page's file picker. The picked file is written into the file system before the engine looks for IWADs.
- **9f.** Offline: force the offline transport and offline switch from 5c-5d on.
- **9g.** Identity: replace the `flock` at startup (`features/identity/zx_identity.cpp:79`) with a no-op, since one tab owns its storage.
- **9h.** URLs: `I_OpenURL` (`posix/sdl/i_system.cpp:966-980`, `fork` plus `xdg-open`) becomes `window.open`.
- **9i.** Errors: `I_FatalError` (`posix/sdl/i_system.cpp:385-430`, stderr plus `exit(-1)`) shows the message on the page and stops the loop instead of exiting.
- **9j.** Every pure decision above (tics due, frame due, args from query, storage paths, render tier) is a `Compute*` helper with a colocated `*_test.cpp`.
- **9k.** The feature README lists each in-engine hook: `d_main.cpp`, `posix/sdl/i_system.cpp`, `m_specialpaths.cpp`, `network.cpp`, `features/identity`.

## 10. Rendering in the page

- **10a.** `features/hwrender/diligent/dgweb.cpp`:
  - creates the swapchain on the page's canvas (`NativeWindow.pCanvasId`, `SwapChainWebGPUImpl.cpp:455-459`);
  - sizes it from the canvas times `devicePixelRatio`;
  - resizes on the canvas resize event.
- **10b.** Device: create it the way 2f decided, before `D_DoomMain` reaches the video setup. If the browser has no WebGPU, the page explains which browsers do.
- **10c.** Presentation: Diligent's `Present` requests an animation frame (`SwapChainWebGPUImpl.cpp:330-331`), so the main loop runs at the browser's frame rate (`emscripten_set_main_loop` with fps 0) and `vid_maxfps` isn't used.
- **10d.** Shaders compile at runtime through glslang and Tint inside the module. Measure the wasm size this costs, and list precompiling our own shaders to WGSL at build time as the follow-up.
- **10e.** Screenshots and save pictures use 7m's asynchronous read-back. Replay recording stays off (`NO_REPLAY`).
- **10f.** Done when Freedoom MAP01 draws in Chrome from `localhost` with the menus, HUD, console and wipes, and a capture matches the desktop WebGPU capture within the suite's tolerances.

## 11. Sound, music and input

- **11a.** OpenAL:
  - The device opens through Emscripten's OpenAL.
  - Every extension probe takes its false path cleanly: EFX, HRTF and the soft extensions (`features/openal-sound/oalsound.cpp:1103-1198`).
  - Streams update from the main loop (`UpdateSounds`, `oalsound.cpp:2295`).
- **11b.** Autoplay: the page resumes the audio context on the first click or key press, before which the engine plays into a suspended context.
- **11c.** Music: MIDI through the in-process OPL synth (the default device, `sound/computation/midi_device_compute.cpp:18`), with module and GME formats from the tree. FluidSynth (`dlopen`, `sound/music_fluidsynth_mididevice.cpp:695`) and Timidity (`sound/music_midi_timidity.cpp:451-476`) are compiled out.
- **11d.** Mouse:
  - Clicking the canvas requests pointer lock (SDL relative mode, `posix/sdl/i_input.cpp:260, 265`).
  - `SDL_WarpMouseInWindow` (`i_input.cpp:152`) does nothing on the web.
  - `CenterMouse` only matters outside relative mode.
- **11e.** Keyboard: the page keeps game keys (the tilde console key, the F-keys, Tab) from triggering the browser. The keys the browser reserves for itself (Ctrl+W and the like) stay the browser's.
- **11f.** Gamepads through SDL's Emscripten Gamepad API backend (`posix/sdl/i_joystick.cpp`).
- **11g.** Fullscreen on a user gesture, through SDL's fullscreen toggle.
- **11h.** Done when sound effects, music, mouse look, keyboard and the console all work in Chrome.

## 12. The page and the local server (`web/` at the repo root)

- **12a.** `web/index.html` and a loader script:
  - fetch the wasm and data with a progress bar;
  - create the canvas;
  - finish the WebGPU device and IDBFS steps (2f, 9c);
  - start the module.
- **12b.** File panel: pick an IWAD and PWADs, keep them in IndexedDB, and list them on the next visit.
- **12c.** Data:
  - The engine pk3s (`fua_core`, `brightmaps.pk3`, `skulltag_actors.pk3`, about 2 MB, from the `wadsrc` targets) and Freedoom are fetched or preloaded into the file system.
  - Freedoom's licence is shown, and `doom2.wad` is never bundled.
- **12d.** Error and requirement messages: no WebGPU, a fatal engine error (9i), out of memory.
- **12e.** Local server: a small script (`tools/web/serve.mjs`) serving `dist-web/` with the `application/wasm` type and the isolation headers from 1e (COOP `same-origin`, COEP `require-corp`).
- **12f.** `web_run.sh`, the counterpart of `mac_build_run.sh`: build if stale, start the local server, open the browser at `localhost`.

## 13. Tests and checks

- **13a.** The gtests also build under Emscripten and run under Node in CI, so every `Compute*` test runs in the browser's runtime.
- **13b.** PR CI builds every preset from section 3: `desktop`, `server`, `offline`, `nogl-vulkan` and `web`.
- **13c.** The scenario suite's WebGPU side (7o) runs against Vulkan on Windows.
- **13d.** A browser smoke test: headless Chrome with WebGPU (Puppeteer) loads `dist-web/` from the local server, starts MAP01, reads the canvas, and compares one capture within tolerance. It's kept small because CI uses software WebGPU.
- **13e.** A tripwire script beside the existing ones flags `NO_GL`, `__EMSCRIPTEN__` and `_WIN32` outside a short, named list of seam and platform files.
- **13f.** Hands-on checks in Chrome:
  - new game;
  - save, reload the page, load;
  - music and sound;
  - console and cvars;
  - an ACS-heavy map and a DECORATE mod;
  - the player's own `doom2.wad` through the file panel.

## Progress (branch `web-port`)

- **Running:** `web_compile.sh` builds the module (about 14 MB of wasm), and `web_run.sh` (12f) builds, serves `dist-web/` and opens it. Chrome draws Freedoom MAP01 through WebGPU with the HUD and weapon (`?map=MAP01`).
- **Desktop WebGPU (7a-7d, 7f, 7h-7k, 7o):** `-webgpu` runs the backend on Dawn and matches Vulkan pixel for pixel on MAP01. `fuactl scenarios --webgpu` runs the suite on WebGPU against Vulkan.
- **Shaders:**
  - textures and samplers are declared apart, and the material array only exists under `FUA_BINDLESS`;
  - storage blocks are named so WebGPU's reflection keeps their names;
  - storage buffers carry no dynamic offset;
  - the 2D and sky pipelines match their passes' attachments.
- **Loop (4a, 4c, 4d, 4g; 4b decided):** `features/mainloop` and `features/web`'s driver.
- **Offline and online seams (5d-5h):**
  - `-offline`, which the page always passes; an offline launch greys "Play Online!" and asks the network nothing;
  - `ZX_ONLINE` off leaves out `ONLINE_SOURCES` (95 files) for `features/offline/offlinestubs.cpp`, grown from the offline link's 77 undefined symbols; the browser build is built this way (0.9 MB less wasm) and so is `linux-offline`;
  - `ZX_VOIP` and `ZX_ACCOUNTS`;
  - hashing through the in-tree SHA-256 and MD5;
  - the accounts' crypto behind `zx_identitycrypto.h`.
- **No desktop GL (6a, 6b, 6c, part of 6e):** the SDL frame buffer split into window and GL halves, and the hitbox overlay's backend half split from GL's.
- **The page (12a-12e):**
  - the player's WADs are kept in IndexedDB and chosen in a panel;
  - the config and saves are kept in IndexedDB, written every few seconds and when the page is hidden or left;
  - a restart stores them before it reloads.
- **Quitting (4d):** a page runs no atexit handlers and kept calling the frame after `exit()`. Every exit now goes through `features/web`'s `--wrap=exit`, which ends the loop, runs the engine's shutdown (writing the config) and has the page store everything and offer to play again.
- **Read-backs (7m, 10e):** WebGPU in a browser maps buffers only asynchronously, so `CanWaitForGPU()` is false there.
  - A screenshot is the page's: the engine asks during the frame (`DCanvas::ScreenshotLater`), and the page downloads the canvas just after it (`Module.postMainLoop`).
  - A save picture is drawn at the save and read back on a later frame (`ReadTextureLater`, polled after each present). The save is written with the placeholder, and the picture is spliced in once it arrives (`FRenderer::SaveWritten`, `ComputeSpliceSavePicture`).
- **Camera textures on a binding per material:** the binding was made before the camera's first render and kept the white placeholder. WebGPU always draws this way, as does Vulkan past the array's cap; `cameratexpermaterial` covers it.
- **Checks (13a, 13c, 13d, 13e):**
  - The gtests build with Emscripten, threads and wasm exceptions, and pass in Node (`test-web` in CI).
  - `tools/web/check.mjs` runs the page in headless Chrome: its screenshot shows MAP01, the sound waits for a click and plays after one (11b, 11c), Tab stays in the game rather than moving the page's focus (11e), the screenshot command downloads the frame, a save gets its picture, quitting stops the loop, a setting survives quitting, leaving and reloading, and errors reach the console or the page. CI runs it after the web build, on SwiftShader's WebGPU.
  - CI builds the web module (`build-web`) and runs the tripwire.
  - `fuactl scenarios --webgpu` runs the suite on WebGPU against Vulkan.
- **Next:**
  - music and pointer lock, checked by hand (11; headless Chrome has no pointer lock);
  - the build presets for the other profiles (3);
  - the `linux-offline` profile through the scenario suite with no socket opened (5j).

## 14. Done: playing locally

- **14a.** `./web_compile.sh && ./web_run.sh` opens Chrome at `localhost`.
- **14b.** The page loads with a progress bar, starts Freedoom (or the player's IWAD from the file panel), and reaches the title screen with music.
- **14c.** A new game on MAP01 plays at the browser's frame rate with mouse look, sound and the HUD.
- **14d.** A save survives a page reload, and the config keeps its changes.
- **14e.** Every preset builds in CI, the WebGPU scenario side and the browser smoke test pass, and the coverage gate holds on every new `Compute*` unit.
