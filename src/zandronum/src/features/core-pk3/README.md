# core-pk3

The engine's own data pk3 is named for the build that produced it: `fua_core_<version>.pk3`.

## Why

A fixed `zandronum.pk3` loads happily when it is stale, and the engine then misbehaves somewhere far
from the cause. That failure mode has cost whole sessions here, which is why `mac_build_run.sh` and
`windows_build_run.ps1` exist at all: to hash-verify pk3s against the build tree because the engine
could not be relied on to notice.

Keyed to the build, a mismatched pair is simply not found, and the engine says so on line one. The
point of this feature is turning a silent wrong-data bug into a loud startup error.

The second thing it buys: a player can keep every core they have ever downloaded in one folder and
the right one still loads, because the engine asks for an exact filename rather than globbing.

## The single source

`src/zandronum/CMakeLists.txt` computes `FUA_CORE_PK3_NAME` once, from `FUA_VERSION`, and uses it
for both the file `zipdir` writes and the `-DFUA_CORE_PK3_NAME=` the engine compiles against. They
cannot drift apart within a build tree, which is what a glob would have allowed.

`FUA_VERSION` comes from CI when CI passes it, and from `git describe --tags --abbrev=0` otherwise.
CI has to pass it because during a release build the tag does not exist yet: `release.yml` mints the
version from its `bump` input and only creates the tag after the artifacts are built, so asking git
at that moment answers with the PREVIOUS release.

## In-place engine edits

| file | what |
|---|---|
| `src/zandronum/CMakeLists.txt` | computes `FUA_CORE_PK3_NAME`, pins the `pk3` target name, injects the define |
| `src/zandronum/wadsrc/CMakeLists.txt` | `add_pk3` takes the computed name |
| `src/zandronum/src/version.h` | `BASEWAD` is `FUA_CORE_PK3_NAME`, with a `fua_core_dev.pk3` fallback for builds that bypass our CMake |
| `src/zandronum/src/d_main.cpp` | `d_FuaDescribeFoundCores` scans beside the exe; the fatal error names what it found |

## What is here

`computation/corepk3_compute` owns the two decisions:

- `IsCorePk3Name` — is this filename one of ours. Case-insensitive, because Windows filesystems are.
  Used to filter the scan, and intended for the guard against a SECOND core arriving through `-file`,
  which would put two sets of engine lumps in play and leave `dup_const`'s first-definition-wins rule
  silently picking a winner.
- `DescribeFoundCores` — what to print under `Cannot find <expected>`. Never lists the expected one,
  which would contradict the line above it, and says something rather than nothing when there are no
  others, because "none at all" and "the wrong one" have different fixes.

The directory scan stays in `d_main.cpp`: it is I/O, not a decision.

## Not done yet

- The `-file` guard described above is not wired in; only the predicate it needs exists.
- Stale `fua_core_*.pk3` accumulate in build and dist directories. Packaging should delete the ones
  that are not this build's.
- `skulltag_actors.pk3` and `brightmaps.pk3` keep fixed names. Whether they should be keyed too is
  an open question; mixed conventions in one folder are worse than either answer applied uniformly.
