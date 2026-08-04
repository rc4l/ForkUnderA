# panel-menu

The layout decision behind `DFUAPanelListMenu` — the ListMenu subclass that draws a rounded gradient
panel sized around its own content, so a menu reads as a card over the title screen rather than loose
art and text on a busy background. Same visual language as the updater's notice chip and the server
browser.

## Files (this folder)

- `computation/panelmenu_compute.{h,cpp}` — `ComputeListMenuExtent`: given each item's box, the
  descriptor's cursor offset and its linespacing, the rectangle the panel must enclose.
- `computation/panelmenu_compute_test.cpp` — its tests.

## In-place engine edits

The drawing itself is not here. `DFUAPanelListMenu` lives in `src/menu/listmenu.cpp` beside the
ListMenu machinery it extends, and only the pure decision was lifted out so it could be tested
off-engine.

- `src/menu/listmenu.cpp` — `DFUAPanelListMenu::Drawer()` flattens `mDesc->mItems` into
  `zx::MenuItemBox` values and calls `ComputeListMenuExtent`; `FListMenuItemStaticPatch::GetInkTop()`.
- `src/menu/menu.h` — `FListMenuItem::GetInkTop()` (virtual, returns 0) and the
  `FListMenuItemStaticPatch` override + its cache field.
- `src/CMakeLists.txt` — `features/panel-menu/computation/panelmenu_compute.cpp`.

## Why `GetInkTop` exists

A title patch is authored with transparent slack above the letters — Freedoom's `M_DOOM` has a lot of
it. Measuring the item's *box* therefore put visibly more empty panel above the logo than below the
rows beneath it (~95px vs ~43px at 1280×800), even though the padding either side of the content was
mathematically symmetric at 8 virtual px.

`GetInkTop` reports how many rows are fully transparent before the artwork starts, read from the
texture's own column spans, so the extent is measured from the ink. It is scanned once and cached:
`GetColumn` decodes the texture, and the panel measures every item every frame.

This is deliberately general rather than a per-IWAD nudge — Doom's `M_DOOM`, Heretic's and Strife's
logos all carry different amounts of slack, and a constant tuned to one would be wrong for the others.
