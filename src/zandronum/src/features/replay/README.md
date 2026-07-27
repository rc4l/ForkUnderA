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
| `cl_fua_replay` | `0` (off) | master enable — the rolling buffer only runs when on |
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

- **Phase 1 (done):** settings + `fua_clip` command + menu + comma bind, dark behind `cl_fua_replay`
  (default off).
- **Phase 2 (done, macOS):** capture in `OpenGLFrameBuffer::Swap()`, worker-thread H.264 encode into
  a rolling ring of packets (`ReplayEncoder`, libx264, VBV-capped bitrate), MP4 mux on `fua_clip` →
  `~/ZandroX-Clips/`. Verified end-to-end: comma in MAP01 → a valid, shareable `.mp4`. mac_compile.sh
  bundles the libav* stack (+x264/vpx/…) into the .app, so it's self-contained.
  - *Perf follow-up:* capture reuses the synchronous `glReadPixels` screenshot readback. It fires
    only at the capture rate (~30/s), but a double-buffered **PBO** async readback is the intended
    optimization to remove the GPU stall (design §3.1).
  - *Build follow-up:* Windows (vcpkg) + Linux (apt) FFmpeg provisioning still to wire; the CMake
    detection is pkg-config based and platform-neutral.
- **Phase 3:** hardware encoders — `cl_fua_replay_encoder 2` already selects `h264_videotoolbox`
  (wired, untested); NVENC/QSV/AMF/VAAPI + hw-first auto to follow, x264 fallback.
- **Phase 4:** audio via OpenAL-Soft `ALC_SOFT_loopback`.
