# save-settings

A **Save current settings** item in the Options menu that writes the config ini immediately, without
waiting for the client to exit, and reports the result in an on-screen popup.

**Provenance:** the menu item is uzdoom@190173bc442a0eab808f4d2c8a18a1f932e92547 · the popup and the
message-box ordering fix are ours.

## Files (this folder)

- `computation/savesettings_compute.{h,cpp}` — `zx::ComputeSaveSettingsMessage`, the popup text for
  a successful or failed save.
- `computation/savesettings_compute_test.cpp` — its tests.

## In-place engine edits

- `wadsrc/static/menudef.txt` — the `SafeCommand "Save current settings", "writeini"` row in
  `OptionsMenu`. It has to be `SafeCommand`: a plain `Command` runs its action in an unsafe context,
  and `writeini` is an `UNSAFE_CCMD`, so it would be refused.
- `src/m_misc.cpp` — `writeini` opens the popup when a menu is active. From the console during play
  it only prints, as before.
- `src/configfile.{h,cpp}` and `src/m_misc.cpp` — `FConfigFile::WriteConfigFileTo(path)` writes a
  copy without touching the config's own path, and `M_SaveDefaults` uses it for `writeini <file>`.
  The old code swapped the path to the copy and then "restored" it to the copy too, so after one
  `writeini backup.ini` every later save, including this menu item and the save on exit, went to
  `backup.ini`. Upstream UZDoom still has that bug. Tested in `configfile_nettest.cpp`.
- `src/menu/messagebox.cpp` — `DMessageBoxMenu::HandleResult` closes the Yes/No box **before**
  passing the answer to its parent. The old order dispatched first, so a confirmed action that
  opened its own message box had it popped straight back off by the box's `Close()`.

`savesettings_compute.cpp` needs no CMake edit: the engine and the test build both glob `*_compute.cpp`.
