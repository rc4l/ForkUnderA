---
name: in-engine-recording
description: Record the running ZandroX engine to MP4 with the built-in FUA instant-replay recorder — for feature demos, before/after clips, and wad-swap montages. Use this instead of any screen-capture approach; the engine runs in its own macOS Space and external grabbers can't reach it reliably.
---

# Recording the engine (built-in FUA instant replay)

## When to use
Producing a video of the running engine — a feature demo, a before/after, a
wad-swap montage — driven over the MCP bridge (`mcp__zandronum__*`).

## Golden rule: use the ENGINE recorder, never a screen grab
The engine runs in its own macOS Space; Apple `screencapture` and ffmpeg
`avfoundation` display capture can't reliably reach it. The engine has a
built-in H.264 recorder — the FUA instant replay (`src/.../features/replay/`)
that reads its own framebuffer via async PBO readback. **Always use that.** If
you find yourself listing avfoundation devices, stop — you're on the wrong path.

## Controls
CVARs (all `CVAR_ARCHIVE | CVAR_GLOBALCONFIG`, so they persist across restarts
including `wad_reload`):
- `cl_fua_replay 1` — enable the rolling recorder (off by default).
- `cl_fua_replay_duration <secs>` — rolling-buffer length; `fua_clip` saves this many seconds.
- `cl_fua_replay_fps` (30), `cl_fua_replay_maxheight` (720), `cl_fua_replay_bitrate` (Mbps),
  `cl_fua_replay_encoder` (0 = auto → VideoToolbox on macOS, else libx264), `cl_fua_replay_audio` (off).

Command **`fua_clip`** (bound to `,`) saves the last `duration` seconds to:
- macOS `~/Movies/ZandroX/clip-YYYYMMDD-HHMMSS.mp4`
- Linux `~/Videos/ZandroX/…`, Windows `%USERPROFILE%\Videos\ZandroX\…`

`fua_clip` needs the buffer warm (a few seconds of rendered frames) — it prints
"warming up" otherwise.

## Recipe (via MCP)
1. `launch_instance` windowed (e.g. 1280×720) with the map set.
2. `run_command "cl_fua_replay 1; cl_fua_replay_duration <N>; cl_fua_replay_maxheight 720"`.
3. Clear any first-run menu (`menu_key back`); let it render a few seconds.
4. Do the thing you want captured.
5. `run_command "fua_clip"`; grab the newest file from the clips dir.
6. Trim/speed/concat with ffmpeg to the target length.

## Two shapes
- **Continuous shot** — one long `cl_fua_replay_duration` (~60–90 s), do everything,
  a single `fua_clip` at the end → one take; then speed it to fit (`setpts=PTS/<factor>`).
- **Per-segment** — `fua_clip` after each segment, then `concat` the clips.

The recorder survives `wad_reload` (persistent worker thread; PBOs re-alloc on
the new framebuffer), so both shapes work across reloads. See the
`wad-reloading` skill for how reloads behave over the bridge.

## HARD-WON PITFALLS — do not skip
- **VERIFY every clip's actual content.** Extract a frame per clip
  (`ffmpeg -ss <t> -i clip -vframes 1 f.png`) and compare (`md5` or eyeball).
  A prior `SaveClip` bug produced multiple *byte-identical* clips that each looked
  fine until checked (the encoder EOF'd after the first save — fixed in PR #106).
  "Saved a clip" ≠ "captured what you think."
- **`dumphud` does NOT log the status-bar background / full frame** — never use it
  to judge whether capture is working. Use a `screenshot` or the saved clip.
- A clip may omit at most the encoder's in-flight pipeline tail (~a couple frames
  on VideoToolbox, zero on x264) — imperceptible for a replay; don't chase it.
- This ffmpeg build has **no `drawtext`** (no libfreetype) — you can't burn in text
  labels; rely on visually-distinct footage.
- Random `MAPxx` only works for Doom-format wads. Total conversions (MM8BDM) put
  gameplay on **named** map lumps (`MM1CUT` = Cut Man), not `MAPxx` — check the
  wad's MAPINFO for real stage names or you'll land on a hub/story screen.

## ffmpeg cheatsheet
- Speed a take to ≤T s: `ffmpeg -i in.mp4 -filter:v "setpts=PTS/<factor>,scale=1280:720" -an -c:v libx264 -pix_fmt yuv420p out.mp4` (factor = duration / T).
- Concat trimmed segments: `-filter_complex` with `[i:v]trim=a:b,setpts=PTS-STARTPTS,scale=1280:720,fps=30[vi]` … `concat=n=K:v=1:a=0[out]`.

Code: `src/zandronum/src/features/replay/` (`zx_replay.cpp`, `zx_replay_encoder.cpp`,
`computation/replay_compute.*`). Invariant learned the hard way: `SaveClip` must
never terminally-flush the live encoder.
