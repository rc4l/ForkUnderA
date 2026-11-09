# controls-search

A search row at the top of Customize Controls. Typing anywhere in the menu filters the key list, which a mod's `KEYCONF` sections can make very long.

- Plain text matches anywhere in a row's label or command, ignoring case: `weap` finds "Next weapon" and `weapnext`.
- `*` matches any run and `?` any one character, anchored at both ends: `DnD*` finds rows starting with DnD, `*weapon` rows ending in weapon.
- A matching section title keeps its whole section; matching rows keep their section's title.
- On the search row, Backspace deletes a character, Escape clears the search (a second Escape leaves), and Enter or Down moves into the results. Elsewhere Backspace still unbinds the selected control.

## Files

- `controlsmenu.cpp` / `.h`: `DFUAControlsMenu`, which runs a private copy of the controls descriptor holding the fixed top rows, the search row and the rows the filter keeps, and `ControlsSearch_Install`.
- `computation/controlfilter_compute`: `ComputeControlPatternMatch` and `ComputeControlFilter`, tested.

## In-engine hooks

- `menu/menudef.cpp` `InitKeySections`: `zx::ControlsSearch_Install` gives `CustomizeControls` this class unless its MENUDEF names one, so a mod that redefines the menu still gets the search.
- `CMakeLists.txt`: `controlsmenu.cpp` is in `add_executable` before `zzautozend.cpp` (it uses `IMPLEMENT_CLASS`).
