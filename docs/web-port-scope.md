# FUA in a browser: scope

**Goal:** FUA running single player in a browser tab, loaded from a free GitHub Pages URL, with no server.
Multiplayer comes later, through a relay. Paths are relative to `src/zandronum/src/` unless noted.
This is the state as of the vulkanv2 merge (PR #326), 2026-10-14.

**Verdict:** this is possible, and nothing fundamental blocks it.
- The renderer is the biggest piece.
- Handing the main loop back to the browser is the second.
- Everything else is gating native-only features off and replacing a few OS calls.
- WebAssembly 3.0's 64-bit memory isn't needed. Normal wasm32 gives 4 GB, and our int64 `fixed_t` is native in wasm.

Other projects that have done this:
- [halo-ce-web-multiplayer](https://github.com/GACWR/halo-ce-web-multiplayer): Emscripten, WebGL 2, a WebSocket/WebRTC gateway, players supply their own data.
- [diabloweb](https://github.com/d07RiV/diabloweb): devilution in WebAssembly on GitHub Pages, with the shareware data bundled and the full game's data supplied by the player.

## What we already have

- **Diligent ships a WebGPU backend and an Emscripten platform.**
  - Emscripten sets `PLATFORM_WEB`, and WebGPU is the default there (`deps/DiligentCore/CMakeLists.txt:145-148, 311-315`).
  - The swapchain is made from a canvas (`SwapChainWebGPUImpl.cpp:455-459`), and NDC matches Vulkan.
- **The backend hardly touches Vulkan directly.**
  - Its only raw Vulkan call is the limits query in `features/hwrender/diligent/dgprobe.cpp:65-71`, which is already skipped when the cast fails.
  - Device creation (`dgprobe.cpp:151-180`) and the swapchain (`dgwindow.cpp:129-157`) are the only Vulkan-named factory calls.
- **A run without GL already exists** (`-vulkan`, `vid_backend`; `features/hwrender/backendvideo.cpp:71-84`).
  - 2D, HUD, menus and console draw through `dg2d.cpp`, wipes through `dgwipe.cpp`, and textures come from `features/materials`.
  - Models keep a CPU copy.
- **Pipelines use only what WebGPU has.**
  - Triangle lists only.
  - No geometry or tessellation shaders, compute, indirect draws or push constants.
  - RGBA8 textures only.
  - At most 6 storage buffers in any stage, within WebGPU's 8.
- **Already optional with a fallback:** ray-traced mirrors (off by default, with planar mirrors as the fallback) and depth clamp (capability-gated).
- **Threads can already be avoided.**
  - `fua_jobs_threads 0` runs the job pool on the game thread (`features/jobs/jobs.cpp:26`).
  - A single-threaded build needs no SharedArrayBuffer, so GitHub Pages needs no special headers.
- **Audio should work with Emscripten's built-in OpenAL.**
  - The OpenAL backend probes every extension at runtime: EFX, HRTF and the soft extensions are optional (`features/openal-sound/oalsound.cpp:1103-1198`).
  - MIDI defaults to the in-process OPL synth, and module music (dumb, game-music-emu) is built from source in the tree.
- **The libraries and build tools mostly come along.**
  - zlib, bzip2, jpeg, lzma, gdtoa, sqlite, GME and dumb are built from source in the tree.
  - CMake already cross-compiles, importing host tools from a native build (`IMPORT_EXECUTABLES`, `src/zandronum/CMakeLists.txt:28-34`).
- **Data is small.**
  - The engine's pk3s total about 2 MB.
  - Freedoom (BSD-licensed) can be the bundled game, and players load their own `doom2.wad`.
- **Hosting is free.** `.github/workflows/pages.yml` already deploys to Pages.

## What it takes

### 1. Renderer: a WebGPU tier
The scene shaders can't run on WebGPU as written, even with bindless off. The work:

- **Texture array.**
  - Every scene pixel shader enables `GL_EXT_nonuniform_qualifier` and declares `uMaterials[]` unconditionally (`dgscene_glsl.h:142, 198, 233, 466`).
  - WebGPU has no resource arrays; Diligent's web backend rejects them (`PipelineResourceSignatureWebGPUImpl.cpp:300`).
  - Make the array a bindless-tier define, and draw the web tier through the per-batch `uTex` binding the non-bindless path already uses.
- **Inter-stage varyings.**
  - The scene vertex shader passes 23 locations (0–22) to the pixel shader (`dgscene_glsl.h:26-48`). WebGPU only guarantees 16.
  - Pack them, or read per-piece data from the `Pieces` storage buffer in the pixel shader.
- **Decals read depth through a float `sampler2D`** (`dgdecals.cpp:180, 474-478`). WGSL wants a depth texture type.
- **Blocking GPU waits.**
  - Fences, `WaitForIdle` and `IdleGPU` are no-ops on web (`FenceWebGPUImpl.cpp:89`, `DeviceContextWebGPUImpl.cpp:1370`).
  - The per-frame pacing fence (`dgscene.cpp:635-640`) has to be skipped there.
  - Screenshots, save pictures and replay read-back (`dgdiag.cpp:265-361`) need asynchronous read-back, or stay off at first.
- **Device and canvas.**
  - Use `GetEngineFactoryWebGPU`.
  - Fill `NativeWindow.pCanvasId` (`dgwindow.cpp:147-155` only handles Win32, Linux and macOS).
  - Link `Diligent-GraphicsEngineWebGPU` in place of the Vulkan engine (`CMakeLists.txt:2027-2035`).
- **Building without desktop GL.**
  - Every Diligent build compiles `gl/` and needs GL and GLEW.
  - `FUA_DILIGENT` is forced off under `NO_GL` (`src/zandronum/CMakeLists.txt:150-153`).
  - `DiligentFrameBuffer` subclasses the SDL GL video class from `posix/sdl/sdlglvideo.cpp`, which is wholly `#ifndef NO_GL`.
  - Either compile `gl/` against Emscripten's GL headers and never make a context, or allow `NO_GL` with Diligent and move the frame buffer's base off `SDLGLFB`.

### 2. Main loop
- **What blocks today.**
  - `D_DoomLoop` never returns (`d_main.cpp:1354-1499`), inside a restart loop driven by an exception (`d_main.cpp:3054-3696`).
  - Tics come from a `SIGALRM`/`setitimer` handler plus `sem_wait`, or a busy-spin (`posix/sdl/i_system.cpp:194-296`).
  - The wipe loop sleeps between frames (`d_main.cpp:1253-1267`), and quitting waits 1.5 s (`menu/messagebox.cpp:420`).
- **The options.**
  - JSPI keeps the code as it is, but only Chrome ships it today.
  - ASYNCIFY works in every browser but makes the binary larger and slower.
  - Splitting `D_DoomLoop` into one-frame steps driven by `emscripten_set_main_loop` is the best long-term choice and the most work. It also needs the wipe as a state machine and timing from `emscripten_get_now`.

### 3. A web build target that compiles out native-only features
- **Already gated, just switch off:**
  - FFmpeg replay (`NO_REPLAY`);
  - sentry (`ZX_ENABLE_SENTRY`);
  - the MCP bridge (`FUA_MCP_BRIDGE`);
  - GTK (`NO_GTK`).
- **No gate today (always compiled, `CMakeLists.txt:1479-1592`), needs one:**
  - server hosting, port mapping, wad-serve, wad-download and the wad library;
  - the server browser, updater and registry list;
  - identity;
  - the libcurl net layer.
- **Required libraries:**
  - OpenSSL is `REQUIRED` for `csrp` account login (`CMakeLists.txt:632`); compile out accounts for an offline build.
  - Opus and rnnoise are needed for VoIP, which has no gate apart from `NO_SOUND`.
  - GeoIP is always linked, though only the server uses it.
- **Other:**
  - `crashcatcher.c` (signals and fork/exec) is always in the SDL source list.
  - Toolchain: `emcmake cmake`, SDL2 via `-sUSE_SDL=2`, OpenAL via `-lopenal`, with host tools imported from a native build.

### 4. Offline networking
- `NETWORK_Construct` always binds UDP sockets (`network.cpp:281-460`), and `recvfrom` runs every tic (`g_game.cpp:1655-1657`).
- The server-browser query (`d_main.cpp:1471`) and the update check (`d_main.cpp:1366`) start a few seconds after launch.
- Emscripten's socket emulation would try WebSocket connections for all of these. An offline flag needs to skip them.
- Socket setup is tangled with the Huffman and message-buffer setup in the same function, so it has to be split first.

### 5. OS facilities
- **Paths:** `~/.config` paths (`m_specialpaths.cpp:472-550`) move to Emscripten's file system, persisted to IndexedDB (IDBFS or OPFS) for config and saves.
- **Small replacements:**
  - `flock` in identity (`features/identity/zx_identity.cpp:79`);
  - `xdg-open` → `window.open`;
  - the FPS limiter's `timer_create` thread (`posix/sdl/hardware.cpp:311-341`).
- **IWAD picker:** GTK, kdialog or stdin today (`posix/sdl/i_system.cpp:515-734`); replace with an in-page file picker.
- **Already fine:**
  - Directory scanning (`scandir`/`opendir`) works on Emscripten's in-memory file system.
  - Terminal handling and the stdin network loop are only used outside single player.

### 6. Page and data
- A loading page that fetches the wasm, the engine pk3s and Freedoom, with progress.
- Upload of the player's own IWAD and PWADs, kept in IndexedDB.
- Pointer lock and fullscreen on click.
- Audio unlocks on the first gesture, because of browsers' autoplay policy.
- A Pages workflow that builds with emsdk. Pages' limits (100 MB per file, about 1 GB per site) are no problem.

## Which game systems work

**Work as they are (pure computation):**
- ACS: `p_acs.cpp` is a bytecode interpreter.
- DECORATE: `thingdef/` (parser, expressions and code pointers).
- MAPINFO, SNDINFO, LANGUAGE, TEXTURES and GLDEFS lights.
- Bots, demos, the console, cvars and menus.
- Save games, once the file system is persistent.

**Small guards needed:**
- `p_acs.cpp:2923` and `parsecontext.cpp:162` each hold an empty `asm("")` (a GCC trick that stops the compiler optimising across that point). Each needs a one-line guard for wasm.
- `x86.cpp` and the SSE path in `nodebuild.cpp` already have non-x86 paths (`x86.cpp:10`, `DISABLE_SSE`).

**Music:**
- Works: OPL MIDI (the default) and the module and GME formats.
- Doesn't: FluidSynth (loaded with `dlopen`) and Timidity (an external process).

**GLDEFS custom hardware shaders** compile at runtime through glslang and Tint. They may work, or may stay off on the web (see risk 3).

**Online play goes over WebRTC, because browsers have no UDP.**
- The registry gains an HTTPS door and relays the WebRTC offer and answer over the channel that already carries punch instructions to servers.
- Servers gain a WebRTC endpoint beside their UDP port. ICE does the hole punching, and a TURN relay serves only players on strict networks.
- Servers that only speak plain UDP are reachable only through a relay we would host (the Halo port's gateway design).
- Server list: the registry list is HTTPS already, so it can be a `fetch` if the registry allows cross-origin requests. Server queries go through the registry's door.
- WAD downloads: `fetch` where the host allows cross-origin requests, otherwise through a proxy.
- VoIP: possible later (microphone through `getUserMedia`, Opus compiled to wasm).

**Hosting from a browser tab isn't practical.**
- A tab can't listen on a port, and UPnP and NAT-PMP are impossible.
- The only route is a WebRTC listen server behind a signalling service, plus TURN for hard NATs. That's a project of its own.
- Servers stay native (our Docker server image already exists), and browser players join them.

## What isn't possible in a browser

Online play here assumes the WebRTC approach. The registry gains an HTTPS door and relays the WebRTC offer and answer over its existing channel to servers, the same channel that carries punch instructions today (`server-registry/main.cpp:920`). Servers gain a WebRTC endpoint beside their UDP port.

1. Joining servers that only speak plain UDP (stock Zandronum, or ours before the WebRTC endpoint), except through a relay we host.
2. Plain UDP to anything, including the registry and master queries; the registry needs the HTTPS door.
3. LAN discovery by broadcast.
4. Hosting from a tab for native UDP-only clients, and UPnP/NAT-PMP port mapping. A tab can host only for clients that speak WebRTC.
5. Running a dedicated server or the hosting supervisor (they launch child processes).
6. Ray-traced mirrors (WebGPU has no ray tracing; mirrors are planar).
7. Bindless materials (no texture arrays; materials are bound per batch).
8. The GL renderer, and with it side-by-side GL vs Vulkan.
9. FluidSynth loaded at runtime as a system library, and Timidity as an external program; MIDI uses OPL.
10. The native FFmpeg instant-replay recorder (a browser recorder would replace it).
11. Native crash handling: signal handlers, gdb re-launch, sentry-native dumps.
12. The MCP bridge and fuactl over a local socket, so the scenario harness can't drive the browser build as it is.
13. Running other programs: process priority, `xdg-open` (`window.open` covers URLs).
14. Scanning the player's disk: IWAD search, wad-library folder scans, Steam/GOG detection.
15. Fetching WADs from sites that don't allow cross-origin requests, without a proxy.
16. Presentation control: uncapped frame rate, vsync off, exclusive fullscreen, refresh-rate switching.
17. WebGPU on Linux browsers by default, today.

## The approach: the same playbook as Vulkan

Vulkan was built behind seams cut from the monolith, and checked against GL with the scenario suite. The web port repeats that, mostly on desktop where the tools work:

1. **Cut the seams on desktop first.** The existing suite verifies each one.
   - Loop: `D_DoomLoop` splits into a one-frame step plus a driver.
   - Clock: a third `I_SelectTimer` implementation.
   - Storage: behind `m_specialpaths`.
   - Transport: `network.cpp`'s send and receive behind an interface (UDP, offline, later WebRTC).
   - Online features: one CMake source list with one stub file, kept honest by a native offline configuration in CI.
2. **Bring WebGPU up as a third backend on desktop.** Diligent's WebGPU backend runs natively through Dawn (`GraphicsEngineWebGPU/CMakeLists.txt:138`).
   - `vid_backend` gains a WebGPU value, and `ComputeRenderTier` picks the feature level from the device's capabilities.
   - The scenario suite gains a WebGPU side, checked against Vulkan the way Vulkan was checked against GL.
3. **Assemble the Emscripten build** from the web implementation of each seam, plus `dgweb.cpp` and `features/web/`. The gtests also run under Node in CI.
4. **Add online play** as a WebRTC transport behind the same interface, plus the registry's web door and a WebRTC endpoint on servers.

## Guard rails, so the pattern scales

The seams-plus-parity pattern is right for a single-binary engine. As targets multiply, drift moves from code into configurations, and the PR #326 merge showed where.
- The server build failed twice: on symbols nothing defined, then on stubs that duplicated real ones.
- `glstubs.cpp` documents its own signature drift.
- Features read cvars defined in `gl/`.

Rules that keep it honest, in their scalable form:
1. **Named build profiles, not independent switches.**
   - Desktop client, server, web and dev each set the toggles (`NO_GL`, `FUA_DILIGENT`, the bridge, replay, sentry, sound…), and CI builds every profile on every PR.
   - Building every combination of switches is the anti-pattern this replaces.
2. **Seam contracts only where a second implementation exists.**
   - Compile-time seams stay plain headers, checked by building every profile.
   - Interfaces or function tables are only for choices made at runtime (backend, transport).
   - Speculative interfaces and virtual calls in hot paths are the anti-pattern.
3. **`#ifdef`s only at seams.** A tripwire flags `NO_GL` / `__EMSCRIPTEN__` / `_WIN32` elsewhere, and its allowlist stays a short, named list of seam files. Allowlist creep is the failure mode.
4. **Boundaries enforced by the build, not by grep.**
   - Each module becomes its own CMake object library with declared dependencies, so it can include only what it depends on.
   - Object libraries (`$<TARGET_OBJECTS>`), unlike static libraries, keep `IMPLEMENT_CLASS` registration ahead of `zzautozend.cpp`.
   - An include-graph tripwire is the stopgap until then. Modules own their cvars and state.
5. **Live comparison first, golden captures last.**
   - WebGPU is checked against Vulkan live on desktop, so no golden images are needed there.
   - Golden captures, with the scenario suite's per-box tolerances, are only for where no second renderer exists: the browser, and after GL is removed.
   - Browser smoke tests stay few (boot, a map, one capture), because CI runs Chrome's software WebGPU.

## How it fits the codebase

The repo's conventions decide most of the layout:
- a feature folder under `features/`;
- platform files picked by CMake;
- stub files for builds that leave a subsystem out;
- `Compute*` helpers with tests.

- **`features/web/`** holds the browser glue that is new code, listing its in-engine hooks in its README:
  - the per-frame step that replaces `D_DoomLoop`'s endless loop;
  - timing from `emscripten_get_now`;
  - IndexedDB persistence for config and saves;
  - the bridge to the page's file picker;
  - the offline switch for `NETWORK_Construct`, the server-browser query and the updater.

  The pure parts are `Compute*` helpers with colocated tests: tics due at a given clock, the web paths for config and saves, the offline decisions.
- **Platform layer:** Emscripten ships SDL2, so the web build reuses `posix/sdl/` (input, joystick, main, system) the way Linux does. It swaps in `features/web/` for the loop, timer and paths, and drops `crashcatcher.c`.
- **Renderer:**
  - `features/hwrender/diligent/dgweb.cpp` is the canvas swapchain. It sits beside `dgsdl.cpp`, `dgwin32.cpp` and `dgcocoa.mm`, picked through `FUA_DILIGENT_PLATFORM_SOURCES`.
  - The WebGPU tier is a set of shader defines in `dgscene_glsl.h`.
  - Which tier a device gets (bindless or per-batch materials, packed varyings, depth clamp, ray tracing) is a `ComputeRenderTier` helper in `features/hwrender/computation/`, fed by the device's capabilities.
- **Native-only features:**
  - The ungated features (server hosting, port mapping, wad-serve, wad-download, the updater, the registry list, identity and the curl net layer) move into one CMake source list.
  - The web build leaves that list out, the way `GL_SOURCES` now leaves out side by side under `NO_GL`.
  - One stub file in `features/web/` answers their entry points, as `posix/sdl/glstubs.cpp` and `features/hwrender/backendstubs.cpp` do.
  - The gated ones just switch off: replay, sentry, the MCP bridge and GTK.
- **Build and CI:**
  - An `if( EMSCRIPTEN )` branch in CMake.
  - A `web_compile.sh` beside `linux_compile.sh` and `mac_compile.sh`.
  - Host tools imported from a native build (`IMPORT_EXECUTABLES`).
  - A `build-web` job in `_build.yml`, and the Pages workflow deploying the result.
  - The gtests can also be built with Emscripten and run under Node in CI, so the same `Compute*` tests check wasm behaviour.
- **The page:** a top-level `web/` folder with the HTML/JS loader, the file picker, pointer lock and the audio unlock. It's not engine code, so it lives outside `src/`.
- **Online play, later:**
  - The client side is a WebRTC implementation of the transport seam under `network.cpp`, in `features/web-transport/`.
  - The registry's HTTPS door and offer/answer relay live in `server-registry/`, beside its punch broker. The server's WebRTC endpoint is a feature folder in the engine, shipped in the existing server image.

## Browser coverage
- **WebGPU** is on by default in:
  - Chrome and Edge on Windows, macOS, ChromeOS and Android;
  - Safari 26;
  - Firefox on Windows since 141 (and, per some sources, on Apple Silicon Macs).
- It isn't on by default on Linux in any browser yet ([implementation status](https://github.com/gpuweb/gpuweb/wiki/implementation-status)).
- **JSPI** ships only in Chrome; that's why the main-loop rework is recommended over relying on JSPI.

## Risks to settle first (a spike)
1. **Tint on our SPIR-V.** Diligent turns GLSL into SPIR-V with glslang, then into WGSL with Tint (`ShaderWebGPUImpl.cpp:112-153`, `WGSLUtils.cpp:92-111`). Tint is fetched at configure time (`ThirdParty/dawn/CMakeLists.txt:17-21`), so it hasn't been tried. Run every pipeline's source through it offline before committing to anything.
2. **Emscripten's WebGPU binding.** Diligent links Emscripten's original built-in WebGPU (`-s USE_WEBGPU=1`, `GraphicsEngineWebGPU/CMakeLists.txt:181-182`), not the newer emdawnwebgpu port. We may need to pin an Emscripten version or update Diligent.
3. **Binary size.** Shaders compile at runtime, so glslang, SPIRV-Tools and Tint all end up in the wasm. Pre-compiling our own shaders to WGSL at build time would remove most of that. GLDEFS user shaders would still compile at runtime, or stay off on the web.
4. **CPU cost, single-threaded.** The visibility walk and level mesh would run single-threaded, with wasm running perhaps 1.5–2.5× slower than native. Measure on the heaviest maps; threads, through a service worker that adds the isolation headers, are the follow-up.

## What the spike found

- **Toolchain.**
  - Emscripten 4.0.0 is the pin, the version Diligent's own web CI builds with.
  - Our Diligent submodule codes against Emscripten's built-in WebGPU header through compatibility aliases (`GraphicsEngineWebGPU/include/pch.h:38-74`), so the newer emdawnwebgpu port isn't needed.
- **Device creation.**
  - Diligent's web samples make the adapter and device in the page's JavaScript, then attach to it (`AttachToWebGPUDevice` with `emscripten_webgpu_get_device()`). That settles the blocking wait in Diligent's own creation path.
  - The page must ask for the adapter's full limits, as Diligent's native path does (`requiredLimits` from `adapter.limits`). WebGPU's defaults are too small for us.
- **It runs.** `tools/web/webgpu-check` builds Diligent's WebGPU backend from our tree. Headless Chrome draws the engine's GLSL triangle through it, with glslang and Tint inside the module, which is 7.2 MB uncompressed.
- **Shaders.** The 16 shaders a launch makes went through the conversion (`fua_dg_shaderdump`, `shadercheck.js`):
  - **Combined image-samplers.** Tint's SPIR-V reader rejects `sampler2D` outright.
    - Every backend shader now declares a `texture2D` and a `sampler` named `<texture>_sampler`. Diligent pairs the two by that suffix, so the bindings and immutable samplers keep their names.
    - Vulkan draws through the same source.
  - **Texture arrays.** Rejected, as expected. The material array now sits behind `FUA_BINDLESS`, defined only where the device has the array; WebGPU draws with a binding per material.
  - **Vertex outputs.** The scene shader uses 23, over WebGPU's default of 16 but under the 28 that desktop adapters report. Asking for the adapter's limit covers it, and packing the outputs becomes the fallback for a device capped at 16.
  - **Storage buffers per stage.** The default is 8 and adapters report 16; the same request covers it.
  - **Depth clamp.** Desktop adapters offer `depth-clip-control`, so GL's depth clamp can stay.
- **Not yet checked:** the shaders that need content to appear (decals, mirrors, portals, horizon, lines, stereo). They come through when the scenario suite runs on desktop WebGPU (design 7o).

## Milestones
1. **Spike:** Tint over every shader. Diligent's WebGPU backend clearing a canvas from our CMake under Emscripten.
2. **Boot:** single-threaded, offline, reaching the title screen with menus and sound (2D through `dg2d` on WebGPU).
3. **World:** the scene shader's WebGPU tier. Freedoom MAP01 playable at a Pages URL in Chrome.
4. **Polish:**
   - saves and config persisted;
   - the player's IWAD upload;
   - pointer lock;
   - the per-frame main loop for every browser;
   - threads.
5. **Later:** multiplayer over WebRTC. The registry's web door relays the offer and answer over the channel that carries punch instructions today, servers gain a WebRTC endpoint beside their UDP port, and a TURN relay serves only players on strict networks.
