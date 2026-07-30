# mapinfo-intermission-subtitle

Ports the `Subtitle` intermission-screen keyword from UZDoom.

- **Keyword:** `Subtitle = "<text or $lookup>"` (inside an intermission
  screen action in an `intermission { ... }` block)
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@2c226afff

## Behavior

Draws a caption near the bottom of an intermission/slideshow screen, wrapped
and centered in the small font. Supports the `$stringtable` lookup form.

## Code hooks (in-place)

- `src/zandronum/src/intermission/intermission.h`
  - `FIntermissionAction::mSubtitle` (definition) and
    `DIntermissionScreen::mSubtitle` (runtime).
- `src/zandronum/src/intermission/intermission_parse.cpp` —
  `FIntermissionAction::ParseKey` handles `Subtitle`.
- `src/zandronum/src/intermission/intermission.cpp`
  - `DIntermissionScreen::Init` copies `mSubtitle` from the descriptor.
  - `DIntermissionScreen::Drawer` resolves `$lookup` and draws the wrapped,
    centered caption at the bottom via `V_BreakLines` + `SmallFont`.
