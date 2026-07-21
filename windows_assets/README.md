# windows_assets

Prebuilt x64-windows dependencies for the OpenAL audio stack, so `windows_build.ps1`
can produce a ZandroX build without compiling dependencies from source (what
`windows_compile.ps1` does via vcpkg, ~15 minutes on a cold machine).

- `include/`, `lib/`, `bin/` — headers, import libs and runtime DLLs for openal-soft,
  libsndfile, mpg123, opus and openssl, plus the transitive libraries those pull in
  (FLAC, ogg, vorbis, mp3lame, …).
- `licenses/` — the license/copyright for each bundled library.

## Regenerating

These come from `vcpkg export` on a Windows runner. To refresh them (new versions, or
a changed package list), run the **Export Windows deps** workflow from the Actions tab,
download its `windows_assets` artifact, and replace this folder's `include/`, `lib/`,
`bin/` and `licenses/` with its contents.
