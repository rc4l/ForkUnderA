# statusbar-widescreen

Widescreen status-bar background support for the classic (hardcoded) Doom
status bar.

- **Class:** PORTABLE (real behavior)
- **Upstream:** No single cherry-pickable commit. GZDoom implemented widescreen
  status bars as part of its ZScript status-bar rewrite, which **replaced and
  deleted** the C++ `DDoomStatusBar` this engine still uses — so there is no
  C++ diff to port. This implements the same behavior against the documented
  ZDoom convention. Reference:
  ZDoom "Widescreen statusbars" thread — https://forum.zdoom.org/viewtopic.php?t=37960
  (design: the original 320-wide bar is centered, with filler extensions added
  on each side), and GZDoom's ZScript `DoomStatusBar` behavior.

## Problem

The classic Doom status bar (`DDoomStatusBar`, `doom_sbar.cpp`) assumes a
320-pixel-wide `STBAR`. A widescreen replacement (e.g. Eviternity II's
1600×32 `STBAR`) was drawn left-aligned, so only its far-left slice showed —
a black/blank bar with the vanilla widgets floating on top.

## Behavior

By the widescreen convention, a wide `STBAR` places the original 320-wide
layout in its **center** with filler extensions on each side. So with
`xoff = (barWidth - 320) / 2`:

- the composited overlays (`STARMS`/`STPTS`, `STTPRCNT`, `STFBANY`) are baked
  into the centered region (`xoff + vanilla_x`);
- the full bar is drawn shifted left by `xoff`, so its center-320 lands exactly
  where the vanilla bar goes — extensions fill the border areas in unscaled
  mode and fall harmlessly off-screen in scaled mode;
- the number-background partial redraws read their slice from the centered
  region instead of the left filler.

Vanilla 320-wide bars are unaffected (`xoff == 0` makes every change a no-op).

## Code hooks (in-place)

- `src/zandronum/src/g_doom/doom_sbar.cpp`
  - `FDoomStatusBarTexture::MakeTexture()` — composite overlays at `xoff + x`.
  - `DDoomStatusBar::Draw()` / `DrawMainBar()` — draw the bar at `-xoff` so its
    center aligns with the vanilla layout.
- `src/zandronum/src/g_shared/shared_sbar.cpp`
  - `DBaseStatusBar::DrawPartialImage()` — shift the draw origin by `-xoff` and
    the source window by `+xoff` so number backgrounds read the centered slice.

## Verification

- Vanilla `doom2.wad` (320 `STBAR`): pixel-identical, no regression.
- Synthetic 640-wide `STBAR` (vanilla centered + colored side filler): widgets
  align with the centered layout, no filler bleed behind numbers.
- Eviternity II (1600-wide `STBAR`): art fills the width; AMMO/HEALTH/ARMS/
  ARMOR labels, aligned numbers, and the BULL/SHEL/RCKT/CELL readout all render.
