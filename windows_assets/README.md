# windows_assets

Prebuilt x64-windows dependencies for the OpenAL audio stack, so `windows_build.ps1`
can produce a ZandroX build without compiling dependencies from source (what
`windows_compile.ps1` does via vcpkg, ~15 minutes on a cold machine).

- `include/`, `lib/`, `bin/` — headers, import libs and runtime DLLs for openal-soft,
  libsndfile, mpg123, opus, openssl, glew and ffmpeg, plus the transitive libraries those
  pull in (FLAC, ogg, vorbis, mp3lame, x264, …).
- `licenses/` — the license/copyright for each bundled library.

## What must be here, and why the list is not obvious

Two of these are load-bearing in ways a missing-file error would not tell you:

- **glew** — the client build has required it since d2e4479. Without it CMake fails at configure,
  which at least fails loudly.
- **ffmpeg** — instant replay is *optional* to CMake. Without it the build configures, compiles,
  packages and ships perfectly happily, and the resulting binary answers the clip key with "instant
  replay was not built into this binary". That is how it shipped for a while. `windows_build.ps1`
  now refuses to build without `lib/avcodec.lib` for exactly this reason: a dependency whose absence
  removes a feature rather than breaking the build has to be checked deliberately, because nothing
  else will check it for you.

## Regenerating

These come from `vcpkg export` on a Windows runner. To refresh them (new versions, or
a changed package list), run the **Export Windows deps** workflow from the Actions tab,
download its `windows_assets` artifact, and replace this folder's `include/`, `lib/`,
`bin/` and `licenses/` with its contents.
