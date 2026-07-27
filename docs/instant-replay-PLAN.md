# Instant Replay — Design Doc

Status: **proposed** · Owner: rc4l · Target: ZandroX (Zandronum/GZDoom GPL fork)

Press a key and the last N seconds of gameplay are saved as a shareable video, the way NVIDIA
ShadowPlay / AMD ReLive / OBS's Replay Buffer work. No pre-arming, no external capture software.

---

## 1. Goal & user experience

- **One button.** A bindable action (`fua_clip`) writes `~/…/ZandroX/Clips/clip-YYYYMMDD-HHMMSS.mp4`.
- **Always-on rolling buffer.** When the feature is enabled, the engine continuously keeps the
  last N seconds in memory. The keypress *saves*; it does not *start* recording.
- **Cheap enough for Doom.** Capture must not visibly cost frames. This is the hard constraint and
  it drives every decision below (async readback, hardware encode, capped cadence, downscale).
- **Shareable anywhere.** The output must double-click-open on Windows, macOS, and Linux and drop
  cleanly into Discord / Messages / social. That means **H.264 in an MP4 container** (see §3).

Non-goals (v1): editing/trimming UI, streaming, capturing the console/menus artistically, GIF
export. All can come later on top of the same buffer.

---

## 2. Format & encoder decision (locked)

**Output: H.264 / MP4.** **Encoder: FFmpeg, hardware-first with an x264 software fallback.**

Why not WebM/VP9, the "ideal" open format: on macOS WebM does not open in QuickTime or Finder
Quick Look (Safari-only), so it fails the "double-click and share" bar. More decisively for a
performance-first feature, **there is no hardware VP8/VP9 encoder on Apple Silicon** (and rarely
elsewhere), so WebM forces the slowest CPU-only path exactly where we care. H.264 has dedicated
hardware encoders on all three platforms:

| Platform | Hardware H.264 encoder (preferred) | Software fallback |
|---|---|---|
| macOS | `h264_videotoolbox` (Apple silicon media engine) | x264 |
| Windows | `h264_nvenc` / `h264_qsv` / `h264_amf` | x264 |
| Linux | `h264_vaapi` (GPU) | x264 |

FFmpeg wraps all of these behind one API, so a single code path picks hardware when present and
degrades to x264 otherwise. FFmpeg also gives us muxing and (phase 3) audio resample/mux for free.

### Licensing (see also the separate license note we discussed)

- **Copyright:** clean. x264 is **GPL-2.0-or-later**, FFmpeg is LGPL-2.1+ and becomes GPL-2.0+ when
  built `--enable-gpl` (required for x264). Both are GPL-3.0-compatible → fine to link into ZandroX.
- **Build flags:** `--enable-gpl` ✅. **Never `--enable-nonfree`** (produces a non-distributable
  binary). Enable only the pieces we use (H.264 encode + MP4 mux + swscale/swresample).
- **Patents:** H.264 is patent-pooled (Via LA) — a *separate*, non-copyright matter the GPL doesn't
  address. The hardware-first design minimizes exposure: OS/GPU H.264 encoders are licensed by the
  platform vendor; only the x264 software fallback carries the usual (widely-navigated) FOSS
  residual, same as VLC/Handbrake/every distro FFmpeg.

---

## 3. Architecture

Nothing to reuse from the engine's demo system (those are input/netpacket sims, not frames) and
there is **no existing video recorder** in this fork. New feature, built from the screenshot
readback primitive plus a new FFmpeg dependency.

```
render thread                         encoder thread
─────────────                         ──────────────
OpenGLFrameBuffer::Swap()             loop:
  └─ if capture armed & due:            pull frame from queue
       PBO async readback (N-1)          swscale RGB→NV12 + downscale
       memcpy last-ready PBO ──────►     encode (HW or x264), CBR, keyframe every ~1s
       push (ts, pixels) to queue        push packet → ring buffer
                                         drop packets older than N s (whole GOPs only)

CCMD "fua_clip":
  snapshot ring → mux GOP-aligned packets → Clips/clip-<stamp>.mp4 → on-screen toast
```

### 3.1 Frame source & capture hook

- Reuse the readback logic in `OpenGLFrameBuffer::GetScreenshotBuffer`
  (`src/zandronum/src/gl/system/gl_framebuffer.cpp:526`): tightly-packed **RGB8, bottom-up**
  (negative pitch — OpenGL row order).
- **Hook point:** `OpenGLFrameBuffer::Swap()` (`gl_framebuffer.cpp:224`), just before
  `SwapBuffers()` — frame fully rendered, back buffer still intact.
- **Performance-critical:** the screenshot path uses a synchronous `glReadPixels` (a GPU stall).
  For per-frame capture we use a **double-buffered PBO**: issue `glReadPixels` into PBO *n*, and
  `glMapBuffer` PBO *n-1* (already done) — the render thread never blocks on the GPU. This is the
  single biggest perf lever after hardware encode. Falls back to a plain `glReadPixels` only if PBOs
  are unavailable (they are core since GL 2.1 + `ARB_pixel_buffer_object`, so effectively always).

### 3.2 Cadence, downscale, ring buffer

- **Fixed capture rate** independent of game FPS (default 30). A pure helper decides "is this frame
  due?" from the frame timestamp so a 200 fps game still only feeds 30 fps to the encoder.
- **Downscale** to a capped height (default 720p) on the encoder thread via `libswscale`, which also
  does the RGB→NV12 conversion the encoder wants. Cuts encode cost and RAM together.
- **Encoded ring buffer**, not raw. Raw 1080p30×10 s ≈ 1.8 GB — unacceptable. We keep ~N seconds of
  *compressed* packets; a keyframe every ~1 s lets us drop whole GOPs off the tail and start the
  saved clip cleanly. Memory footprint becomes a few MB.

### 3.3 Save path

- On the `fua_clip` CCMD, snapshot the ring, pick the oldest keyframe ≥ (now − N s), mux that GOP-aligned
  packet span to MP4 with `libavformat`, write to a `Clips/` dir next to screenshots, and show a HUD
  toast ("Saved clip-…mp4"). Muxing off the render thread; the keypress returns instantly.

### 3.4 Threading

- Render thread does only: PBO map + `memcpy` + enqueue (all cheap). Everything expensive (scale,
  encode, mux) is on one encoder thread. Bounded queue; if the encoder ever falls behind, drop the
  oldest queued *raw* frame (never block the game).

### 3.5 Module layout (package-by-feature, per `src/features/README.md`)

```
src/zandronum/src/features/replay/
  README.md
  zx_replay.cpp / .h            # orchestrator: init, Swap() hook, CCMD, save, CVAR glue
  zx_replay_encoder.cpp / .h    # thin FFmpeg wrapper (HW-first, x264 fallback, mux)
  computation/
    replay_compute.h / .cpp     # PURE logic, engine-free (only <cstdint>)
    replay_compute_test.cpp     # colocated GoogleTest, 100% line coverage (CI gate)
```

**Build-order rule (critical, from `features/README.md`):** add these `.cpp` to the
`add_executable` list in `src/zandronum/src/CMakeLists.txt` **before `zzautozend.cpp`** — not via a
trailing `target_sources` — or any `IMPLEMENT_CLASS` (the menu class, §4) falls outside the DObject
registry section and silently never registers.

Per the project's `Compute*` rule, all fragile arithmetic goes in `zx::` pure helpers with colocated
tests: `ComputeFrameDue(lastCaptureUs, nowUs, targetFps)`, `ComputeScaledDims(w,h,maxH)`,
`ComputeRingEvict(packets, nowUs, windowUs)` (returns the drop count to the oldest keyframe),
`ComputeClipFilename(stampParts)`. The FFmpeg wrapper itself is integration-tested at runtime, not
unit-tested off-engine.

---

## 4. Settings — how it lives in the Options menu

Three pieces, all following existing conventions: **CVARs** (C++), an **OptionValue + OptionMenu**
block (`menudef.txt`), and a **keybind** (`Control` row).

ZandroX-specific (FUA) CVARs keep the stock **`cl_`** client-cvar prefix and then add **`fua_`**, i.e.
**`cl_fua_*`** — so they sort with the other `cl_` client settings yet are instantly identifiable as
FUA additions. In `zx_replay.cpp`, mirroring the crashreport feature's CVAR shape:

```cpp
// master on/off — off by default so we cost nothing until the user opts in
CVAR (Bool,  cl_fua_replay,            false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// seconds of history to keep (the "last N seconds")
CVAR (Int,   cl_fua_replay_duration,   15,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// captured frames per second (independent of game fps)
CVAR (Int,   cl_fua_replay_fps,        30,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// max captured height; width scales to aspect (0 = native)
CVAR (Int,   cl_fua_replay_maxheight,  720,   CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// target bitrate in Mbit/s
CVAR (Int,   cl_fua_replay_bitrate,    12,    CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
// encoder preference: 0 auto (hw→sw), 1 force software x264, 2 force hardware
CVAR (Int,   cl_fua_replay_encoder,    0,     CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
```

Toggling `cl_fua_replay` on/off starts/stops the capture buffer via a `CUSTOM_CVAR` callback (same shape
as `snd_announcervolume`), so no restart is needed.

### 4.2 Menu blocks (`src/zandronum/wadsrc/static/menudef.txt`)

**A dedicated top-level "FUA Options" category is the home for all ZandroX-specific features.** FUA is
the ZandroX brand; giving our additions their own menu — rather than scattering them through the stock
Zandronum menus — makes it obvious what's ours and gives future FUA features an obvious home. It sits
as the **first entry of the main Options menu, above "Multiplayer Options."**

Added as the first `Submenu` in the `OptionsMenu` root block (`menudef.txt:344`):

```
OptionMenu "OptionsMenu"
{
    Title "OPTIONS"
    Submenu "FUA Options",          "FUAOptions"           // ZandroX-specific — pinned at top
    Submenu "Multiplayer Options",  "ZA_MultiplayerOptions"
    // …existing options follow…
}
```

The new FUA category menu (the future home for other FUA features too):

```
OptionMenu "FUAOptions"
{
    Title "FUA OPTIONS"
    Submenu "Instant Replay Options",   "ReplayOptions"
}
```

The Instant Replay submenu itself:

```
// value lists
OptionValue "ReplayDurations"   { 5,"5 seconds"  10,"10 seconds"  15,"15 seconds"  30,"30 seconds"  60,"60 seconds" }
OptionValue "ReplayFps"         { 30,"30 fps"  60,"60 fps" }
OptionValue "ReplayQuality"     { 720,"720p"  1080,"1080p"  0,"Native" }
OptionValue "ReplayEncoderMode" { 0,"Auto (hardware if available)"  2,"Hardware only"  1,"Software (x264)" }

OptionMenu "ReplayOptions"
{
    Title "INSTANT REPLAY"
    Option  "Enable instant replay",  "cl_fua_replay",           "OnOff"
    StaticText " "
    Option  "Clip length",            "cl_fua_replay_duration",  "ReplayDurations",  "cl_fua_replay"
    Option  "Capture rate",           "cl_fua_replay_fps",       "ReplayFps",        "cl_fua_replay"
    Option  "Resolution",             "cl_fua_replay_maxheight", "ReplayQuality",    "cl_fua_replay"
    Slider  "Bitrate (Mbit/s)",       "cl_fua_replay_bitrate",   4, 50, 2, 0
    Option  "Encoder",                "cl_fua_replay_encoder",   "ReplayEncoderMode","cl_fua_replay"
    StaticText " "
    StaticText "Press your \"Save replay clip\" key in-game to save.", 1
}
```

The 4th arg on the `Option` rows (`"cl_fua_replay"`) greys the sub-settings out when the feature is off —
the same disable-dependency pattern already used for `nametagcolor` on `displaynametags`
(`menudef.txt:855`).

### 4.3 Keybind — default **comma (`,`)**, all platforms

**Default bind: `,` → `fua_clip`, identical on Windows, Linux, and macOS.**

Why a plain key and not the originally-floated "Shift + screenshot key": the engine's bind system is
**single-scancode only** — `C_DoKey` (`c_bind.cpp:869`) dispatches on `ev->data1` and never consults
modifier state, so modifier *chords* ("Shift+PrintScreen") cannot be expressed as a distinct bind.
F-keys were considered but F1–F12 are all bound and F13–F16 don't exist on most keyboards. A **free
key present on every keyboard** is the reliable default — `,` (comma) is unbound (WASD replaced the
old comma/period turn keys) and, unlike letters, isn't socially contested (`c` is a common crouch,
`p` is `messagemode3`).

Add to `DefBindings[]` (`c_bind.cpp`), beside the screenshot bind:

```cpp
  { "sysrq", "screenshot" },
+ { ",", "fua_clip" },   // [rc4l] FUA instant replay -- save the last N seconds as a shareable clip
```

- Comma is free in the Doom bindings. No existing default is displaced. Default changes only affect
  **fresh configs** anyway — existing `zandronum.ini` binds are preserved.
- `fua_clip` is a normal `CCMD` (declared beside `screenshot` in `m_misc.cpp`), so it also works from
  the console and in aliases.

The bind also shows, live and rebindable, in two menu places: a `Control` row inside the Instant
Replay submenu itself (so the submenu always displays the real key — e.g. `Save replay clip … C`),
and under a "FUA" header at the **bottom** of the standard "Customize Controls" list:

```
StaticText ""
StaticText "FUA", 1
Control    "Save replay clip",   "fua_clip"
```

**Still open (separate from the clip bind):** the macOS *screenshot* default remains `sysrq`, i.e.
effectively dead on Mac. Out of scope for this feature unless we decide to also give macOS a working
screenshot default — tracked as a follow-up, not a blocker here.

### 4.4 Feedback

On save: a brief on-screen message (existing `Printf`/HUD midprint path) and a console line, matching
how the crashreport feature narrates its actions. On failure (disk full, no encoder): a clear one-line
reason, never a silent no-op.

---

## 5. Build / dependency wiring

FFmpeg (subset: avcodec, avformat, avutil, swscale, swresample) added via the established pattern:

- `src/zandronum/src/CMakeLists.txt`: `find_package`/`find_library` behind a `NO_REPLAY` /
  `NO_FFMPEG` guard + `-DHAVE_FFMPEG`; append libs to `ZDOOM_LIBS`; add the `features/replay/*.cpp`
  to the `add_executable` list **before `zzautozend.cpp`** (§3.5).
- **macOS** (`mac_compile.sh`): `brew install ffmpeg` (or a pinned static build for the bundle);
  ensure the dylibs are bundled + `otool` verified like libopenal is.
- **Linux** (`linux_compile.sh`): add `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
  libswresample-dev` to the `DEPS` apt array.
- **Windows** (`windows_compile.ps1`): add `ffmpeg` to the vcpkg `x64-windows-static` install list;
  pass `-DFFMPEG_*` paths. The zero-DLL static build guard still applies — verify no new runtime DLLs.

If a fully-static FFmpeg proves heavy on any one platform, the `NO_REPLAY` guard lets that platform
ship without the feature rather than block a release.

---

## 6. Testing & verification

- **Unit:** every `Compute*` helper gets a colocated `*_test.cpp` at 100% line coverage (CI gate) —
  frame-due cadence, scaled dims, ring eviction to keyframe boundary, filename formatting.
- **Integration/runtime (via MCP, windowed):** enable `cl_fua_replay`, play MAP01, hit the bind, confirm
  a playable MP4 lands in `Clips/`, opens in QuickTime/VLC, is ~N seconds, and correct dimensions.
- **Perf:** measure frame-time with capture on vs off (target: within noise on the hardware path);
  confirm the PBO path shows no `glFinish`-style stall.
- **Encoder matrix:** verify hardware path on each OS and the forced-x264 fallback (`cl_fua_replay_encoder 1`).

---

## 7. Phasing

1. **Scaffold — DONE.** feature folder, CVARs, menu + comma keybind wired, `fua_clip` CCMD. Dark
   behind `cl_fua_replay` default-off. Verified live in-engine.
2. **Capture + save (video-only, software x264) — DONE (macOS).** `Swap()` capture, 30fps cadence,
   downscale (`ComputeScaledDims`), worker-thread encode into a rolling packet ring (`ReplayEncoder`,
   libx264, VBV-capped bitrate), whole-GOP eviction, MP4 mux on `fua_clip` → `~/ZandroX-Clips/`, HUD
   "Saved…" line. Verified: comma in MAP01 → a valid, shareable `.mp4`; the .app self-bundles libav*.
   *Remaining:* PBO async readback (perf), and Windows/Linux FFmpeg provisioning.
3. **Hardware encoders** — `cl_fua_replay_encoder 2` selects `h264_videotoolbox` today (wired,
   untested); `nvenc`/`qsv`/`amf`/`vaapi` + hw-first auto to follow, x264 fallback.
4. **Audio** — tap the mixed master via OpenAL-Soft `ALC_SOFT_loopback` (no new dep; the backend
   already supports it), resample + mux an AAC track. Deferred because the OpenAL backend exposes no
   ready mixdown today (`oalsound.cpp` — confirmed no loopback/capture use yet).
5. **Polish** — disk-space guard + old-clip pruning, "clip saved" HUD flourish, optional "open folder".

---

## 8. Risks & open questions

- **Audio (phase 4)** is the least-proven part — `ALC_SOFT_loopback` reroutes the audio path and
  needs care not to add latency. Video-only phases don't depend on it.
- **Static FFmpeg size/link** on Windows (the zero-DLL invariant) and bundling on macOS are the
  build risks; the `NO_REPLAY` guard is the escape hatch.
- **PBO on the macOS GL 2.1 legacy path** — should be fine (PBOs predate 2.1) but verify on the
  `macos-gl-legacy-path` build specifically.
- **Encoder availability at runtime** — detect and fall back silently to x264; never hard-fail a
  clip because hardware encode is missing.
- Resolved: default keybind is **`c`** (`fua_clip`), all platforms, no existing bind displaced (§4.3).
- Open: whether the buffer should keep running while paused/in menus (proposed: yes while in-game,
  skip pure menu frames). And the separate macOS-screenshot-default follow-up noted in §4.3.
```

