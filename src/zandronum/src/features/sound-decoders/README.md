# sound-decoders

WAV, FLAC, Ogg Vorbis and MP3 sounds and music without libsndfile or mpg123. The browser build has
neither library, so every sound not in Doom's own format used to be silent there: most of Complex
Doom's weapons and monsters, and the engine's own secret, connect and taunt sounds. A desktop build
whose libraries are missing, or fail to load, falls back to these too.

`SoundRenderer::CreateDecoder` tries libsndfile, then mpg123, then these, so a desktop build that has
both libraries decodes exactly as before.

## Files (this folder)

- `computation/pcmdecode_compute.{h,cpp}`: `zx::PcmStream` and `ComputePcmFormat`, `ComputePcmOpen`,
  `ComputePcmRead`, `ComputePcmSeek` and `ComputePcmClose`, which decode a file's bytes from memory to
  16-bit mono or stereo PCM. Each format is one `PcmCodec` table of open, read, seek and close.
- `computation/pcmdecode_compute_test.cpp`: their tests. `pcmdecode_test_fixtures.h` holds a tenth of a
  second of FLAC, Ogg Vorbis and MP3 that ffmpeg made, and the tests write their own WAVs.
- `builtindecoder.{h,cpp}`: `BuiltinDecoder`, the `SoundDecoder` over a `PcmStream`.
- `vendor/`: the decoders, unchanged. `decoders.h` declares them, and `decoders_vendor.cpp` compiles them
  once.

| File | Version | Source | License |
|---|---|---|---|
| `dr_wav.h` | 0.14.5 | mackron/dr_libs, tag `wav-0.14.5` | public domain or MIT No Attribution |
| `dr_flac.h` | 0.13.3 | mackron/dr_libs, tag `flac-0.13.3` | public domain or MIT No Attribution |
| `dr_mp3.h` | 0.7.3 | mackron/dr_libs, tag `mp3-0.7.3` | public domain or MIT No Attribution |
| `stb_vorbis.c` | 1.22 | nothings/stb at `2c980bb59875b0d32144a71867fbdebb2f77cd20` | MIT or public domain |

## In-place engine edits

- `src/sound/i_sound.cpp`: `SoundRenderer::CreateDecoder` tries a `BuiltinDecoder` last.
- `src/CMakeLists.txt`: `builtindecoder.cpp` and `vendor/decoders_vendor.cpp` in the engine's sources,
  the vendored file with its warnings off.
- `tests/CMakeLists.txt`: `vendor/decoders_vendor.cpp` in `zandrox_tests`, with its warnings off.
- `tools/web/check.mjs`: "FLAC, Ogg Vorbis and WAV sounds play" plays the engine's own sound in each
  format in the browser.
