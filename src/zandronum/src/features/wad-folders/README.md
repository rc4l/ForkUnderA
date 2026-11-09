# wad-folders

The player's own WAD folders (a "WAD dump"), searched, subfolders too, before a server's or a preset's
files are downloaded, and at startup for IWADs at their top level.

- Kept in the ini's `[fua_WadFolders]` section, one `Path=` per folder, as typed (`$HOME/wads` stays unexpanded).
- Added from **Options → FUA Options → WAD Folders** (the OS's folder picker), the Windows welcome
  dialog's **Add WAD folder...**, or `fua_wadfolder_add <folder>`; listed by `fua_wadfolders`, removed from
  the menu or by `fua_wadfolder_remove <n>`. The add, remove and pick commands are `UNSAFE_CCMD`, so a mod
  cannot point the search somewhere or open dialogs; the menu's own items run them trusted.
- A worker walks the folders once at startup, and again after each change, into a name index (the WAD
  library's depth and file caps). Lookups never wait for it, except the one that decides a download
  (`PrepareLookup`), which also rebuilds an index a minute old that has not seen the name.
- Every copy found is still checked by MD5 before it is used, so a same-named file of another version is
  passed over as it is anywhere else.
- The picker: Windows' folder dialog, `NSOpenPanel` on macOS, zenity or kdialog on Linux, none in a browser
  (the menu entry is hidden there).

## Files

- `zx_wadfolders.{h,cpp}`: the list, the index worker, the console commands and the menu's items.
- `zx_folderpicker.h`, and one file per platform CMake picks (`FUA_FOLDERPICKER_SOURCES`): `zx_folderpicker_win32.cpp`,
  `zx_folderpicker_mac.mm`, `zx_folderpicker_linux.cpp`, `zx_folderpicker_web.cpp`.
- `computation/wadfolders_compute.{h,cpp}`: folder normalizing, the list's add and remove, the index, the
  walk over an injected lister, the refresh rule; `computation/wadfolders_compute_test.cpp`, their tests.

## In-place engine edits

- `d_main.cpp`: `StartIndex(false)` beside `RegisterKnownWadDirectories`.
- `d_iwad.cpp`: `IdentifyVersion` checks each folder's top for IWADs; `FindIWAD` registers them.
- `features/wad-download/zx_wadsearch.cpp`: `FindAllFilesInEngineSearchPaths` and
  `FindAllIwadsInEngineSearchPaths` add the index's copies after the search paths'.
- `features/wad-download/zx_waddownload.cpp`: `PlanVerifiedCopy` calls `PrepareLookup` first.
- `features/server-browser/zx_joinserver.cpp`: the join's IWAD lookup calls `PrepareLookup` first.
- `features/wad-library/zx_wadlibrary.cpp`: the folders are library roots too.
- `win32/zdoom.rc`, `win32/resource.h`, `win32/i_system.cpp`: the welcome dialog's button, which restarts
  when the folder holds an IWAD so the list offers it.
- `wadsrc/static/menudef.txt`: `FUAWadFolders`, filled by `RefreshMenu`, and its entry in `FUAOptions`.
- `CMakeLists.txt`: the sources, the picker's file per platform, and `uuid` for the Windows dialog's class ids.
