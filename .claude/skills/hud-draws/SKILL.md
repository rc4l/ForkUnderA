---
name: hud-draws
description: How to test what SBARINFO and ACS put on the screen — status bar strings, numbers, images and translucent blocks, and HudMessage text, faded text and font images, and truecolor fonts' own and recoloured pixels — with the DrawTags and TrueColorFont unit tests and the huddraws scenario, which fails first on a build that loses a draw's alpha or reduces a truecolor glyph to the palette. Use for any change to sbarinfo.cpp, sbarinfo_commands.cpp, hudmessages.cpp, v_draw.cpp, v_text.cpp, v_font.cpp, FONTDEFS, the DTA_ tag list, features/draw2d, features/truecolorfont, or a mod's SBARINFO/HudMessage drawing, and for a report of HUD text or images missing, at the wrong translucency, appearing only in some order, or in other colours than the font's images.
---

# Testing HUD draws (SBARINFO and ACS)

Every SBARINFO and ACS draw ends in a `DCanvas::DrawTexture`/`DrawText` call with a `DTA_*` tag list. A
tag read at the wrong width does not fail where it is wrong: it takes whatever the previous call left on
the stack, so a draw vanishes only after another one. DnD's credit label (an SBARINFO string after an
image anchored at the right edge) read alpha `0xC070000000010000` and was invisible on GL and Vulkan alike.
The rule is in `features/draw2d/README.md` (`computation/drawtags_compute`): a 16.16 tag is passed and
read as a 32-bit int, and a `fixed_t` or 64-bit integer in a tag list does not compile (`pass int(...)`).

## Run both

```bash
# unit: the tag's read width, reproduced with a -256.0 left in the vararg slot
build-tests/Release/zandrox_tests.exe --gtest_filter=DrawTags.*
build-tests/Release/zandrox_tests.exe --gtest_filter=TrueColorFont.*
# engine: SBARINFO and ACS draws, Vulkan against itself (runs beside other engines; pass a port of its own when another session runs scenarios)
cd tools/fuactl && node src/cli.mjs scenarios huddraws [--engine <build>/forkundera.exe]
```

`scenarios/huddraws.mjs` judges five sides of one room:

| Side | What it shows |
|---|---|
| `after` | the status bar draws a panel at x -256 FIRST, then a font string, a `drawnumber` and an `alpha 0.5` image (DnD's order); MAP01's OPEN script shows BigFont text, text at alpha 0.5 and a font image at alpha 0.75 |
| `oracle` | the same status bar draws with the panel LAST (`give ZXHudOracle`), right by construction |
| `bare` | MAP02, the same room with no HudMessages |
| `nohud` | `screenblocks 12`, no status bar |
| `fontoracle` | MAP03, whose OPEN script gives `ZXFontOracle`: the status bar draws each truecolor string's glyphs as images where `drawstring` puts them (one pixel in, a glyph apart), and HudMessages show them as one image each, the Gold ones recoloured by `src/fontcolor.mjs` |

`sbar*` boxes must match `oracle` (ceiling 1) and differ from `nohud` (floor 5); a translucent one is held
under what it reads drawn opaque. `acs*` boxes must differ from `bare`. On the build before the fix it
fails five boxes: the string and number gone, the 50% image drawn opaque.

The truecolor boxes (`sbarTrueColor`, `sbarRecolored`, `sbarDontTranslate`, `acsTrueColor`, `acsRecolored`) are
judged per pixel against `fontoracle` and read 0.00 when right. Before `features/truecolorfont` a build stops at
startup on `DONTTRANSLATE`; with that keyword taken out it fails all five (13.75, 9.48, 81.59, 4.65, 3.23).
`src/fontcolor.mjs` is the engine's recolour in JS; `test/fontcolor.test.mjs` holds it to the C++ tests' values.

## Extending it

A new SBARINFO command or HudMessage path gets a draw in `huddraws.mjs` and a box: in both status bar
orders for SBARINFO (keep it clear of the panel so the oracle stays identical), or against `bare` for ACS.
Set its numbers from the fixed build, prove it fails on an old one (`--engine` at a kept binary), and keep
the builder's test (`test/huddraws.test.mjs`) in step. A string starting with `$` is a LANGUAGE lookup in
SBARINFO; a new variadic draw API needs the `ComputeTagArgsAreSafe` guard the four `DCanvas` entry points have.

## Beside this

- `fixed64-widening` shape 7, the class this bug belongs to.
- `template-maps` for building the scenario's rooms; `zandronum-rebuild` for the build under test.
- `features/draw2d/README.md` for where the 2D draw is worked out and recorded.
