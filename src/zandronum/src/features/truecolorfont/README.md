# features/truecolorfont

A FONTDEFS or template font whose glyphs are all truecolor (PNG, JPEG, any texture that is not paletted) draws
them with their own pixels, as UZDoom has since its font overhaul
([uzdoom@5f02b92cd0](https://github.com/UZDoom/UZDoom/commit/5f02b92cd0843026a6bc20743915333041c96cd7)).
Before, every glyph was reduced to the palette: DnD's credit `$` has 105 shades and drew with 9.

- **Untranslated draws the image.** The font's `CR_UNTRANSLATED` table is inactive, so the glyph's own material
  draws, the same one a `drawimage` of the PNG uses.
- **A colour recolours by brightness.** Each other colour is a table indexed by a texel's brightness: its TEXTCOLO
  ramp stretched over the font's darkest and brightest texel (`FRemapTable::ByLuminance`). The texture made under
  that translation is the glyph's true colour with each texel replaced by its brightness's entry, alpha kept.
- **`DONTTRANSLATE`** in a FONTDEFS definition (either format) draws every colour as Untranslated.
- A font mixing paletted and truecolor glyphs, and every paletted font, keeps the palette path. `NOTRANSLATION`
  names palette indices, so a truecolor font has nothing for it to hold out.

- `computation/truecolorfont_compute`: the TEXTCOLO step a brightness falls in and its colour (`ComputeRangeColor`,
  shared with the palette path's `BuildTranslations`), a colour's 256-level ramp, a font's brightness range, the
  brightness-to-colour table with UZDoom's scaling outside the range folded in (`ComputeLuminosityTable`), the
  recolour of RGBA or BGRA texels, and whether a font keeps its glyphs' colours.

`tools/fuactl/scenarios/huddraws.mjs` holds SBARINFO and ACS truecolor strings, untranslated, in Gold and under
`DONTTRANSLATE`, to the same pixels drawn as images (`src/fontcolor.mjs` recolours the expected Gold glyphs the
same way, held to this computation's values by `test/fontcolor.test.mjs`).

## In-engine hooks

- `v_font.cpp` `FFont::MakeGlyphs`: the template and FONTDEFS fonts' glyphs, the source texture itself for a
  truecolor font, and its brightness range read once; `LoadLuminosityTranslations` builds its colours, which
  `FFont::LoadTranslations` and `FSpecialFont::LoadTranslations` hand to it. `GetColorTranslation` answers every
  colour of a `DONTTRANSLATE` font with Untranslated, and `V_InitCustomFonts` parses the keyword.
- `v_font.h` `FFont`: `TrueColor`, `DontTranslate`, `MinLum`/`MaxLum`; the destructor leaves a truecolor font's
  glyphs to the texture manager.
- `r_data/r_translate.h`/`.cpp` `FRemapTable::ByLuminance`, copied by `operator=` and compared by `operator==`.
- `features/materials/translationpalette.h`/`.cpp`: the flag is in `PalData`'s CRC and compare, so a brightness
  table never shares an index with an index remap of the same entries; `GetLuminanceTable` returns it.
- `features/materials/material.cpp` `CreateTexBuffer`: under a brightness table, the pixels are made untranslated,
  then recoloured. GL and Vulkan both make a translated texture there, so neither backend changes.
- Threads: the tables are built only at font load (`V_InitFonts`, `V_InitCustomFonts`, a tic's font load through
  `AskMainFor`) and in `V_RetranslateFonts`, all on the main thread.
