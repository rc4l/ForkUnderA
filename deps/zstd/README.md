# Vendored zstd (single-file library)

- **Version:** zstd v1.5.7 (facebook/zstd, released 2025-02-19)
- **Source tarball:** `zstd-1.5.7.tar.gz`, SHA-256
  `eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`, verified against the
  release's published `.sha256`.
- **License:** BSD-3-clause (`LICENSE` here); zstd is dual BSD/GPLv2 and we take the BSD side.
  Listed in the repository root's `THIRD-PARTY-NOTICES.txt`.

## What these files are

- `zstd.c` — the official single-file amalgamation, generated from the untouched release
  tree by zstd's own script: `build/single_file_libs/create_single_file_library.sh`
  (equivalently `combine.py -r lib -x legacy/zstd_legacy.h -o zstd.c zstd-in.c`).
- `zstd.h` — `lib/zstd.h` from the same release, byte for byte.
- `zstd_errors.h` — `lib/zstd_errors.h` from the same release, byte for byte (`zstd.h` includes it).

No local modifications. The engine links this as the static `fua_zstd` target
(`src/zandronum/src/CMakeLists.txt`), one pinned copy on every platform including the
Emscripten build, instead of upstream Zandronum's `find_package( ZStd )` system library.

## To upgrade

1. Download the new release tarball and verify its published SHA-256.
2. Run `build/single_file_libs/create_single_file_library.sh` in the extracted tree.
3. Replace `zstd.c`, `zstd.h`, `zstd_errors.h` and `LICENSE` here; update the version, date and checksum
   above and the version in `THIRD-PARTY-NOTICES.txt`.

zstd frames are forward- and backward-compatible across library versions, so the two ends
of a netgame may run different zstd versions; the `zstddict` lump, not the library version,
is what the connect handshake checks.
