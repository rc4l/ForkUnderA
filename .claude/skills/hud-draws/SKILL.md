---
name: hud-draws
description: How to test what SBARINFO and ACS put on the screen — status bar strings, numbers, images and translucent blocks, and HudMessage text, faded text and font images — with the DrawTags unit tests and the huddraws scenario, which fails first on a build that loses a draw's alpha. Use for any change to sbarinfo.cpp, sbarinfo_commands.cpp, hudmessages.cpp, v_draw.cpp, v_text.cpp, the DTA_ tag list, features/draw2d, or a mod's SBARINFO/HudMessage drawing, and for a report of HUD text or images missing, at the wrong translucency, or appearing only in some order.
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
# engine: SBARINFO and ACS draws, Vulkan against itself (runs beside other engines)
cd tools/fuactl && node src/cli.mjs scenarios huddraws [--engine <build>/forkundera.exe]
```

`scenarios/huddraws.mjs` judges four sides of one room:

| Side | What it shows |
|---|---|
| `after` | the status bar draws a panel at x -256 FIRST, then a font string, a `drawnumber` and an `alpha 0.5` image (DnD's order); MAP01's OPEN script shows BigFont text, text at alpha 0.5 and a font image at alpha 0.75 |
| `oracle` | the same status bar draws with the panel LAST (`give ZXHudOracle`), right by construction |
| `bare` | MAP02, the same room with no HudMessages |
| `nohud` | `screenblocks 12`, no status bar |

`sbar*` boxes must match `oracle` (ceiling 1) and differ from `nohud` (floor 5); a translucent one is held
under what it reads drawn opaque. `acs*` boxes must differ from `bare`. On the build before the fix it
fails five boxes: the string and number gone, the 50% image drawn opaque.

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
