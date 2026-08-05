# features/replay — FUA instant replay

Press a key (default **comma**) to save the last N seconds of gameplay as a shareable **H.264/MP4**
clip, ShadowPlay / OBS-Replay-Buffer style. No pre-arming, no external capture software. Full design
and rationale (format/encoder/licensing decisions, threading, performance): **`docs/instant-replay-PLAN.md`**.

## Layout

```
zx_replay.cpp              # CVARs, the fua_clip CCMD, (phase 2) the capture hook + save
computation/
  replay_compute.{h,cpp}   # pure logic: cadence, downscale dims, ring capacity, filename
  replay_compute_test.cpp  # colocated GoogleTest, 100% line coverage (CI gate)
```

## Control surface (naming)

ZandroX-specific CVARs keep the stock `cl_` client prefix and add `fua_` → **`cl_fua_*`**. The
bindable command is **`fua_clip`** (commands don't take the `cl_` cvar prefix).

| CVAR | Default | Meaning |
|---|---|---|
| `cl_fua_replay` | `1` (on) | master enable — opt out with `cl_fua_replay 0` |
| `cl_fua_replay_duration` | `15` | seconds of history kept |
| `cl_fua_replay_fps` | `30` | capture rate, independent of game FPS |
| `cl_fua_replay_maxheight` | `720` | cap capture height (0 = native), width scales to aspect |
| `cl_fua_replay_bitrate` | `12` | target bitrate, Mbit/s |
| `cl_fua_replay_encoder` | `0` | 0 auto (hw→sw), 1 force x264, 2 force hardware |

## In-place engine hooks (edits outside this folder)

- `src/CMakeLists.txt` — feature sources listed **before `zzautozend.cpp`** (DObject registry rule,
  see `features/README.md`).
- `wadsrc/static/menudef.txt` — top-level **FUA Options** menu (first entry of the Options menu) →
  **Instant Replay Options** submenu; plus a `Control` row under a "FUA" header in Customize Controls.
- `c_bind.cpp` — default bind: `,` (comma) → `fua_clip` (comma is free — WASD replaced the old
  comma/period turn keys; no existing bind displaced; default changes affect fresh configs only).

## Status

- **Phase 1 (done):** settings + `fua_clip` command + menu + comma bind, gated on `cl_fua_replay`
  (default **on**; opt out with `cl_fua_replay 0`). A rolling buffer only helps if it was already
  running when the moment happened, so an opt-in recorder cannot catch what you did not see coming.
- **Phase 2 (done, macOS):** capture in `OpenGLFrameBuffer::Swap()`, worker-thread H.264 encode into
  a rolling ring of packets (`ReplayEncoder`, libx264, VBV-capped bitrate), MP4 mux on `fua_clip` →
  the platform video folder (macOS `~/Movies/ZandroX`, Linux `~/Videos/ZandroX`). Verified end-to-end:
  comma in MAP01 → a valid, shareable `.mp4`. mac_compile.sh
  bundles the libav* stack (+x264/vpx/…) into the .app, so it's self-contained.
- **Build wiring (done):** FFmpeg is provisioned in all three builds (brew / apt / vcpkg
  `ffmpeg[x264]`); CMake detects it via pkg-config or a find_path/find_library fallback (Windows).
- **Capture perf (done):** the capture hook uses a **double-buffered PBO async readback** — it issues
  `glReadPixels` into one pixel-pack buffer (returns immediately) and hands the previous frame's
  already-completed readback to the encoder, so the render thread never stalls (design §3.1).
- **Phase 3 — hardware encoders (done for the software-upload path):** `cl_fua_replay_encoder` 0 =
  auto (hardware first, then x264), 1 = software, 2 = hardware only. The worker tries candidates in
  order and falls through if one isn't built in / can't open. macOS `h264_videotoolbox` is verified;
  Windows tries `h264_nvenc`/`amf`/`qsv` (fall back to x264 when absent); Linux VAAPI needs a
  hw-frames context and is deferred (software there for now).
- **Phase 4 — audio (experimental, `cl_fua_replay_audio`, default off):** when enabled, the OpenAL
  backend opens an **`ALC_SOFT_loopback`** device at startup; an SDL audio callback renders the mix
  (`alcRenderSamplesSOFT`) at the output rate, plays it back, and taps it for the recorder. The
  encoder gains an **AAC** stream and `SaveClip` muxes video+audio interleaved, aligned by capture
  time. The default (off) audio path is untouched — loopback only engages on opt-in + relaunch.
  Verified programmatically (2-stream clip, non-silent audio); quality/sync still needs a listen-test.
