# language-lumps

LANGUAGE and LMACROS lumps read the way UZDoom reads them, on top of Zandronum's own language selection
(the `language` cvar, the `enu`/`fr`/`ptb` codes and `SetLanguageIDs`). `stringtable.cpp` reads the lumps
with `FScanner` and `Wads`. The parsing and the table's rules are `Compute*` helpers in `computation/`.

## What a lump can now say

- **`$$` aliases** (uzdoom@96ceb11af0): a string whose whole text is `$$OTHER_LABEL` reads as that label's
  text, in the running language, through any number of aliases. UZDoom follows them without a bound, so
  `A = "$$B"; B = "$$A";` overflows its stack. Here a chain longer than `kLanguageMaxAliasDepth` (32) or a
  loop reads as a missing label. A label can only alias another label whole. Joining parts is done with
  LMACROS, as in UZDoom.
- **`\xHH` escapes** (uzdoom@0b5b919352). In both forms of lump, a trailing `\` now ends the string,
  where both engines read past it.
- **CSV LANGUAGE lumps** (uzdoom@c2c9126453, 71d7013255, 6f2bc8379a, 81fd6c819f, 762d4250f8): a lump
  starting `default,` or `identifier,` is a spreadsheet. The `identifier` column holds the labels (spaces
  trimmed), `filter` holds the games, `remarks` is skipped, and every other column holds one or more
  space-separated language codes. An empty cell removes that language's text. A CSV with no
  `identifier` column is reported and skipped, where UZDoom read before every row.
- **LMACROS** (uzdoom@ce1ecd0d4b, 525b0214a7): `@[name]` in a string becomes the macro's male, female,
  neutral or object text. The local player's gender is set once a frame in `D_RunFrame`
  (uzdoom@f2d7bbea99). Zandronum's `other` gender reads the object (`it`) column. The macros of every
  LMACROS lump are kept, a later lump's replacing an earlier one's, where UZDoom keeps only the last
  lump. A short row's missing genders are empty, where UZDoom read past the row.
- **A mod's default text beats older translations** (uzdoom@4668fa95e3, 68c33a6e43, 83f46c4b2f): a
  string under `default` from a later file replaces an earlier file's text in every language, so a mod
  that rewrites a message does not show the IWAD's old French one.
- **`*` and `default` keep their table** (uzdoom@5f2d41884c): `[enu default]` fills only the default
  table, and `[* fr]` only the global one.
- **Lenient headers** (uzdoom@cec46eccb8): a string before any `[language]` header skips the rest of the
  lump with a message instead of ending the game. `~` is ignored with a message, as in UZDoom; ours never
  worked, since a `~` before a code the pass did not match was an error. A code that is not two or three
  letters is ignored. An empty code is still an error.
- **Dehacked matches text ignoring case** (uzdoom@d5f138f14d).
- **Missing labels are logged once under `developer`** (uzdoom@a7c9e29ae8, d453019a61), cut after
  `language_debug_maxlen` characters (64).

## Not taken

UZDoom's IETF language tags (`en-GB`, `zh-Hant`), their fallback chains, OS locale detection and
switching language without a reload (uzdoom@1947b9d8ef, 95ca2e209a and the commits after them) replace
the language-ID system. That would change what `language` accepts, so those rows stay `pending`.

## Hooks

- `stringtable.h` / `stringtable.cpp`: the class keeps its interface (`operator()`, `operator[]`,
  `MatchString`, `SetString`, `LoadStrings`, `FlushAllForRestart`) over a `LanguageTable`, and gains
  `SetDefaultGender`.
- `d_main.cpp` `D_RunFrame`: `GStrings.SetDefaultGender (D_GenderToInt (gender))` each frame.

## Tests

- `computation/languageparse_compute_test.cpp`, `computation/languagetable_compute_test.cpp`.
- `node tools/fuactl/scenarios/languagelumps.mjs [port]`: a wad with a text LANGUAGE lump, a CSV one and
  LMACROS. It checks `print` for the aliases, an alias loop, `\x`, each gender, the CSV columns, and the
  mod's default `TXT_YES` over `language.fr`'s.
