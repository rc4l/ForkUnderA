# features/trnslate

UZDoom's `TRNSLATE` lump: translations defined once by name, which DECORATE's `Translation` and
`A_SetTranslation` then use.

```
BlueBaron = "112:127=192:207", "16:31=200:207"   // ranges, as DECORATE's Translation takes them
Darker : BlueBaron = "0:15=0:0"                  // on another named translation (or Ice, None, Player1..8)
Rust : 1 = "176:191=[160,64,0]:[64,16,0]"        // on standard translation 0 to 6
FromPal = "MYPAL", 16                            // a palette lump's colours from index 16 (PNG, JASC-PAL or raw)
```

- **Upstream origin:** `uzdoom@a505e910` (the lump, `R_FindCustomTranslation`, `A_SetTranslation`),
  `uzdoom@0c04cddd` (palette lumps, `ReadPalette`, `FRemapTable::AddColors`), `uzdoom@15d4fb8f` (palette lumps
  found by full name, then short name).
- `computation/trnslate_compute`: the parser (`ComputeParseTrnslate`), the built-in names (`ComputeTrnslateBuiltin`),
  the palette lump reader (`ComputeReadPaletteLump`), `AddColors`' bounds and `A_SetTranslation`'s outcome.
- `trnslate.cpp`: reads every `TRNSLATE` lump in load order, builds each table on its base and stores it in
  `TRANSLATION_Custom`; a later definition of a name replaces the earlier one. Parse errors and a missing base are
  fatal, as UZDoom's are; a bad range prints and is skipped.
- `A_SetTranslation(name)`: a known name applies, `"None"` clears the translation as UZDoom's does (DECORATE hands `""`
  over as `None` too, as UZDoom's DECORATE does), and an unknown name is ignored. `A_fua_ResetTranslation()` restores the
  class's own translation, which UZDoom's DECORATE has no way to ask for. Both go through `SetActorTranslation`,
  which ACS's `SetTranslation` shares (`features/acs-parity`): the server sets it and sends it through
  `features/net-replication`'s `SendThingTranslation`, and a clientside script's change stays on its machine.
- Not ported: `RainPillar1..8` (we have no rain pillar tables), and the ZScript-only users (particles, `A_OverlayTranslation`).

## In-engine hooks

- `r_data/r_translate.h`: `TRANSLATION_Custom` slot and `FRemapTable::AddColors`; `r_data/r_translate.cpp`: `AddColors`.
- `d_main.cpp`: `R_InitTranslationTables` and `R_ParseTrnslate` before `FActorInfo::StaticInit`, so DECORATE can name
  them; `r_utility.cpp`: `R_Init` no longer builds the tables.
- `thingdef/thingdef_properties.cpp`: `Translation "Name"` looks the name up (this replaces the old `"Ice"` case).
- `thingdef/thingdef_codeptr.cpp` + `wadsrc/static/actors/actor.txt`: `A_SetTranslation` and `A_fua_ResetTranslation`,
  through `trnslate.cpp`'s `SetActorTranslation`.

## Tests

- `computation/trnslate_compute_test.cpp`.
- `fuactl scenarios trnslate` (`tools/fuactl/scenarios/trnslate.mjs`): every definition kind, palette lump format and
  `A_SetTranslation` case on MAP01, drawn against the same tables written inline on MAP02.
