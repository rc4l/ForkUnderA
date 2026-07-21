# freeform-menu

A resolution-independent menu framework (480×270 virtual canvas, free x/y + gravity/anchor
positioning, mouse + scroll). Ported from **Q-Zandronum** (author Evghenii Olenciuc /
`[geNia]`, tagged `#QZA`). Upstream copyright headers are kept intact in the source files.

## Files (this folder)
- `freeformmenu.cpp` — the `DFreeformMenu` handler + all item Draw/Responder logic.
- `freeformmenuitems.h` — the `FFreeformMenuItem*` class hierarchy.

These compile into the engine via `target_sources( zdoom PRIVATE … )` (added to
`src/zandronum/src/CMakeLists.txt` once the hooks below are in and it compiles).

## Required in-engine hooks (the parts that can't live in this folder)

Anchors verified against ZA_3.2.1; menu base classes (`DMenu`, `FListMenuItem`,
`FMenuDescriptor`) are API-identical to Q-Zandronum's, so these are additive.

- [ ] **`src/menu/menu.h`** — add `MDESC_FreeformMenu` to the descriptor enum; add the
      `EGravity` enum, `FFreeformMenuDescriptor : FMenuDescriptor`, `DFreeformMenu : DMenu`,
      and `FFreeformMenuItem : FListMenuItem` (base) declarations; add `MOUSE_Click2` /
      `MOUSE_Release2`; make `DMenu::MouseEventBack` `virtual`; add `FListMenuItem::SetY` /
      `SetAction`; make `DEnterKey` visible to freeformmenu.cpp.
- [ ] **`src/menu/menu.cpp`** — add the `MDESC_FreeformMenu` branch in `M_SetMenu`
      (instantiate `DFreeformMenu`, ~24 lines); add the 2 right-click responder lines.
- [ ] **`src/menu/menudef.cpp`** — `#include "freeformmenuitems.h"`; add
      `DefaultFreeformMenuSettings`; transplant the ~960-line parser
      (`ParseFreeformMenu` / `ParseAddFreeformMenu` / `ParseFreeformMenuBody` +
      `ParseFreeformCommon/Label/ActionableParameters`), **adapted to 3.2.1's `FScanner` /
      `OptionValues` API**; add the `FREEFORMMENU` / `ADDFREEFORMMENU` dispatch keywords.
      *(This is the largest, most delicate hook.)*
- [ ] **`src/v_video.h`** — `extern float ScaleFac;` and the `DTA_ScaleYNoMove` draw tag.
- [ ] **`src/v_video.cpp`** — set `ScaleFac = MIN(width/480.f, height/270.f);`.
- [ ] **`src/v_draw.cpp`** — handle `DTA_ScaleYNoMove` (~9 lines). *(Do NOT pull in
      Q's unrelated widescreen/`Is54Aspect` changes — not needed by freeform.)*
- [ ] **`src/v_text.cpp`** — consume `ScaleFac` for `DTA_ScaleYNoMove` text.
- [ ] **`wadsrc/static/menudef.txt`** — add the `DefaultFreeformMenu` default block.
- [ ] **Build** — `target_sources( zdoom PRIVATE freeform-menu/freeformmenu.cpp )`.

## Verification
No unit tests (pure UI, engine-coupled). Verify by building `SOUND=1 ./mac_compile.sh` and
driving via the MCP: define a `FREEFORMMENU` in a test menudef lump, open it with
`verify_menu`, and screenshot — the menu should render scaled on the 480×270 canvas.

## Status
**In progress — foundation only.** Source imported; hooks above not yet applied; not yet
wired into the engine build (so `main` and this branch stay green). Next: apply the hooks
top-to-bottom, wire `target_sources`, compile, then MCP-verify rendering.
