# features/trnslate

UZDoom's `TRNSLATE` lump: translations defined once by name, which DECORATE's `Translation` and
`A_SetTranslation` then use.

```
BlueBaron = "112:127=192:207", "16:31=200:207"   // ranges, as DECORATE's Translation takes them
Darker : BlueBaron = "0:15=0:0"                  // on another named translation (or Ice, None, Player1..8)
Rust : 1 = "176:191=[160,64,0]:[64,16,0]"        // on standard translation 0 to 6
FromPal = "MYPAL", 16                            // a palette lump's colours from index 16 (PNG, JASC-PAL or raw)
Ember = "16:48=#[255,128,0]"                     // colourise: each entry's luminance times the colour, 48 left out
Dusk = "16:48=@40[0,64,255]"                     // tint: 40% of the way to the colour, 48 left out
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
- **Colourise and tint** (`#` and `@` in any range string, ACS `TRANSLATIONRANGE4`/`5` at UZDoom's opcodes 383 and 384):
  `ComputeColourise` and `ComputeTint` are UZDoom's `AddColourisation` and `AddTint`, the end index left out as theirs
  leave it and a channel past a byte wrapping as UZDoom's `PalEntry` keeps it. Every range now refuses an index outside
  the palette with UZDoom's warning (`ComputePaletteIndexFits`), where it read and wrote past the tables.
- **ACS translations in a netgame**: one `TranslationRange` record holds any of the five kinds. A script's range goes
  through `AddTranslationRange`, the server sends it (`features/net-replication`'s `SendTranslationRange`) and keeps
  it for a joiner, all in the one layout `ComputeWriteTranslationRange` writes and the client's
  `ComputeReadTranslationRange` reads. A kind the client doesn't know is no longer read as a desaturation.
- Not ported: `RainPillar1..8` (we have no rain pillar tables), and the ZScript-only users (particles, `A_OverlayTranslation`).

## In-engine hooks

- `r_data/r_translate.h`: `TRANSLATION_Custom` slot and `FRemapTable::AddColors`; `r_data/r_translate.cpp`: `AddColors`,
  `AddColourisation`, `AddTint`, every range's index check and `AddToTranslation`'s `#` and `@`.
- `p_acs.h`, `p_acs.cpp`: `PCD_TRANSLATIONRANGE4`/`5` and every range opcode through `AddScriptTranslationRange`.
- `sv_main.{h,cpp}`: `EDITEDTRANSLATION_s` holds a `TranslationRange`, which the join replays; `sv_commands.cpp`:
  `SERVERCOMMANDS_CreateTranslation` writes any kind; `cl_main.cpp`: `client_CreateTranslation` reads it;
  `features/net-replication/translations`: `SendTranslationRange`.
- `mcp_rpc.cpp`: `world.translation`, a table's remap and colours for a scenario's judge.
- `d_main.cpp`: `R_InitTranslationTables` and `R_ParseTrnslate` before `FActorInfo::StaticInit`, so DECORATE can name
  them; `r_utility.cpp`: `R_Init` no longer builds the tables.
- `thingdef/thingdef_properties.cpp`: `Translation "Name"` looks the name up (this replaces the old `"Ice"` case).
- `thingdef/thingdef_codeptr.cpp` + `wadsrc/static/actors/actor.txt`: `A_SetTranslation` and `A_fua_ResetTranslation`,
  through `trnslate.cpp`'s `SetActorTranslation`.

## Tests

- `computation/trnslate_compute_test.cpp`; `translation_wire_nettest.cpp`: `SVC_CREATETRANSLATION`'s bytes for each kind,
  an unknown kind and a cut packet.
- `fuactl scenarios acsranges`: ACS colourise, tint and a range past the palette, each table read back through
  `world.translation` against UZDoom's formula, the TRNSLATE `#` and `@` twins equal to them, and fullbright squares
  drawn through them; `--uzdoom` draws the same squares in UZDoom against the same formula. It fails on a build
  without them ("Unknown P-Code 383"). `netacsranges`: the server's tables on every client and a late joiner
  (`fua_debug_netdrop` of `SVC_CREATETRANSLATION` fails them).
- `fuactl scenarios trnslate` (`tools/fuactl/scenarios/trnslate.mjs`): every definition kind, palette lump format and
  `A_SetTranslation` case on MAP01, drawn against the same tables written inline on MAP02.
