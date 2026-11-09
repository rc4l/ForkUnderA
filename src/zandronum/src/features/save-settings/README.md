# save-settings

A **Save current settings** item in the Options menu that writes the config ini immediately, without
waiting for the client to exit, and reports the result in an on-screen popup. Saving only reads the
running game: the keys and settings a player has stay as they are, and a mod's settings reach the ini.

**Provenance:** the menu item is uzdoom@190173bc442a0eab808f4d2c8a18a1f932e92547 · the popup, the
message-box ordering fix, the read-only save and the mod user cvar mirror are ours.

## Files (this folder)

- `computation/savesettings_compute.{h,cpp}` — `zx::ComputeSaveSettingsMessage`, the popup text for
  a successful or failed save.
- `computation/bindarchive_compute.{h,cpp}` — `zx::ComputeBindArchive`, whether a save writes, clears
  or skips one key's ini entry, given the KEYCONF action being saved and the keys it already claimed.
- `computation/cvarsave_compute.{h,cpp}` — which value `cl_protectcvars` records and a save writes
  (`ComputeRecordsPreScriptValue`, `ComputeArchivedCVarValue`), and when a server sends a player's mod
  user cvar and the client copies it into its own cvar (`ComputeSendsUserCVarChange`,
  `ComputeMirrorsToOwnCVar`).
- `computation/*_test.cpp` — their tests.

## Why a save had to stop changing the game

Two engine paths were written for the one save that used to happen, at exit, so they changed the
running game while saving: the KEYCONF key sections unbound every mod key they saved, and
`cl_protectcvars` reset every cvar a script's `ConsoleCommand` had changed. With this menu item a save
happens mid-game, so a mod's keys (Death and Decay's `DNDKEYS` sections) and settings were undone the
moment the player saved.

## In-place engine edits

- `wadsrc/static/menudef.txt` — the `SafeCommand "Save current settings", "writeini"` row in
  `OptionsMenu`. It has to be `SafeCommand`: a plain `Command` runs its action in an unsafe context,
  and `writeini` is an `UNSAFE_CCMD`, so it would be refused.
- `src/m_misc.cpp` — `writeini` opens the popup when a menu is active. From the console during play
  it only prints, as before.
- `src/configfile.{h,cpp}` and `src/m_misc.cpp` — `FConfigFile::WriteConfigFileTo(path)` writes a
  copy without touching the config's own path, and `M_SaveDefaults` uses it for `writeini <name>`,
  which `features/command-safety` keeps to a plain `.ini` beside the config.
  The old code swapped the path to the copy and then "restored" it to the copy too, so after one
  `writeini backup.ini` every later save, including this menu item and the save on exit, went to
  `backup.ini`. Upstream UZDoom still has that bug. Tested in `configfile_nettest.cpp`.
- `src/menu/messagebox.cpp` — `DMessageBoxMenu::HandleResult` closes the Yes/No box **before**
  passing the answer to its parent. The old order dispatched first, so a confirmed action that
  opened its own message box had it popped straight back off by the box's `Close()`.
- `src/c_bind.{h,cpp}`, `src/keysections.cpp`, `src/gameconfigfile.cpp` — `ArchiveBindings` takes a
  claim set instead of marking saved keys `"\1"` and blanking them; `M_SaveCustomKeys` (declared in
  `c_bind.h` now, not `m_misc.h`) fills one set each for `Bindings` and `DoubleBindings`, and the
  general passes skip the claimed keys.
- `src/menu/menu.cpp` `reset2saved`, `src/gameconfigfile.{h,cpp}` `ResetToSaved` and
  `src/keysections.cpp` `M_LoadCustomKeys` — **Reset to last saved** sets only the cvars whose saved
  value differs from the live one (`ComputeResetChangesCVar`), then restores the saved keys:
  `DoKeySetup`, then each KEYCONF key section again. It had restored no keys since the bindings left
  `DoGameSetup` for the startup-only `DoKeySetup` (uzdoom@c36222d2e), and never a mod's KEYCONF keys;
  and setting every cvar ran each callback, `gl_wallmesh`'s dropping every floor and ceiling.
- `src/features/levelmesh/wallcache.cpp` `gl_wallmesh` — a set to the value it already has drops
  nothing. Its `InvalidateAll` clears every flat, and the map bake makes a level's flats once, so
  `gl_wallmesh 1` typed, exec'd or reset while already on left every floor and ceiling undrawn but a
  moving sector's. Turning it off or on still invalidates, as before.
- `src/c_cvars.{h,cpp}` — `SetGenericRep` records only engine cvars (no `CVAR_MOD` or `CVAR_AUTO`)
  for `cl_protectcvars`, so a mod's own cvars its scripts change are saved like any other.
  `C_ArchiveCVars` writes a recorded cvar's pre-script value without resetting it, and the old reset
  is `C_RestoreProtectedCVars`.
- `src/d_main.cpp` — the restart before a server hop or wad reload calls `C_RestoreProtectedCVars`
  before its save, so a server's script changes to engine cvars still end there.
- `src/p_acs.cpp` `SetUserCVar` — a server tells every client when a script changes a player's mod
  user cvar, through net-replication's `SendUserCVar`; before, only the server's copy changed and the player's client sent its old value back
  on the next connect.
- `src/cl_main.cpp` `ServerCommands::SetPlayerUserInfo::Execute` — the client copies such a value for
  its own player into its own cvar without sending it back, so the ini saves it. No new message, so
  older servers and clients are unaffected.

The `*_compute.cpp` files need no CMake edit: the engine and the test build both glob `*_compute.cpp`.
