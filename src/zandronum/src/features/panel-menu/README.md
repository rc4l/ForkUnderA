# panel-menu

The layout decision behind `DFUAPanelListMenu` — the ListMenu subclass that draws a rounded gradient
panel sized around its own content, so a menu reads as a card over the title screen rather than loose
art and text on a busy background. Same visual language as the updater's notice chip and the server
browser.

## Files (this folder)

- `panelmenu.cpp` — `DFUAPanelListMenu`, the `DListMenu` subclass that draws the panel.
- `computation/panelmenu_compute.{h,cpp}` — `ComputeListMenuExtent`: given each item's drawn box, the
  descriptor's cursor offset and its linespacing, the rectangle the panel must enclose.
- `computation/panelmenu_compute_test.cpp` — its tests.

Nothing upstream draws a panel behind menu items, so none of this merges with anything on a re-sync;
keeping it out of `menu/listmenu.cpp` is what stops the sequential backport tripping over it.

## In-place engine edits

Only the accessors the panel reads, which have to sit on the vendored class hierarchy:

- `src/menu/menu.h` — `FListMenuItem::GetDrawnX()` / `GetDrawnY()` (virtual, identity by default) and
  the `FListMenuItemStaticPatch` overrides.
- `src/menu/listmenu.cpp` — `FListMenuItemStaticPatch::GetWidth()`, `GetDrawnX()`, `GetDrawnY()`.
- `src/CMakeLists.txt` — both `.cpp` files above, listed **before `zzautozend.cpp`** (the
  `IMPLEMENT_CLASS` link-order rule in `features/README.md`).

## Why `GetDrawnX` / `GetDrawnY` exist

`DrawTexture` honours a patch's own offsets, so a `StaticPatch` paints at
`(x - leftoffset, y - topoffset)` — not at `(x, y)`. Freedoom's `M_DOOM` is 159×37 with offsets
`(13, -16)`, and the menu places it with `StaticPatch 94, 2`, so it actually paints at `(81, 18)`.

Measuring the *stated* position was wrong in both axes at once:

- **Vertically** the extent started sixteen rows too high. At `CleanYfac` 4 that is 64 screen px —
  enough that the computed top went negative (`topPx = -24` at 1280×800) and `ComputePanelRect`
  clamped the panel flush to the screen edge, cutting off its rounded corners.
- **Horizontally** it read the logo as spanning 94→253 (centre 173) when it really spans 81→240
  (centre 160.5, i.e. dead centre of the 320-wide page), so the panel was sized as though the content
  leaned right and the rows looked off-centre inside it.

Reading the drawn corner fixes both, for any IWAD's art rather than a constant tuned to one — Doom,
Heretic and Strife all ship different offsets.

An earlier attempt scanned the texture's column spans for transparent rows above the artwork. That
was the wrong diagnosis: `M_DOOM` paints from row 0 and has no slack at all. The gap was the offset.
