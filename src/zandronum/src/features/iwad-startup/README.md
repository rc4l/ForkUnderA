# iwad-startup

Which IWAD a launch without `-iwad` starts, and whether the IWAD dialog asks.

- The shipped Freedoom alone (Phase 1 and 2, FreeDM) is one choice: Phase 2 starts, or the player's saved
  pick, without a dialog.
- With an IWAD of the player's own as well (doom2.wad beside Freedoom), the dialog asks as `queryiwad`
  says, preselecting the saved pick.
- An own IWAD new since the last pick (`fua_iwads_seen`) is offered once even past "don't ask", and
  preselected over a saved Freedoom pick. A Freedoom pick made while doom2.wad was listed sticks.
- `$PROGDIR` is in both search lists on every platform (Linux's defaults lacked it), and on case-sensitive
  disks a `DOOM2.WAD` answers for `doom2.wad`.

## Files

- `computation/iwadstartup_compute.{h,cpp}`: `ComputeIwadStartup`, `ComputeIwadSeenList`,
  `ComputeIsFreedoomIwad`, `ComputeFindNameIgnoringCase`; `computation/iwadstartup_compute_test.cpp`.

## In-place engine edits

- `d_iwad.cpp`: `fua_iwads_seen`; `IdentifyVersion` asks `ComputeIwadStartup`; `CheckIWAD` lists a folder
  for a name in another case off Windows.
- `features/wad-download/zx_wadsearch.cpp`: `RegisterKnownWadDirectories` adds `$PROGDIR`.
