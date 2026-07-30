# mapinfo-passover

Ports the `passover` / `nopassover` MAPINFO map keywords from UZDoom.

- **Keywords:** `passover`, `nopassover`
- **Class:** PORTABLE (real behavior)
- **Upstream:** uzdoom@be2f9c866

## Behavior

DSDA/UMAPINFO toggles for infinitely-tall thing clipping, implemented via the
existing `COMPATF_NO_PASSMOBJ` compatibility flag:

- `passover` — force-clears `COMPATF_NO_PASSMOBJ` for the level, so things
  pass over/under each other (3D clipping).
- `nopassover` — force-sets `COMPATF_NO_PASSMOBJ`, so things are treated as
  infinitely tall (vanilla behavior).

Both set the compat mask bit so the level's choice overrides the CVAR default.

## Code hooks (in-place)

- `src/zandronum/src/g_mapinfo.cpp`
  - `MITYPE_CLRCOMPATFLAG` enum value + switch case (force-clears a compat bit
    while setting the mask), used by `passover`.
  - `MapFlagHandlers[]` rows: `passover` (`MITYPE_CLRCOMPATFLAG`) and
    `nopassover` (`MITYPE_COMPATFLAG`), both keyed on `COMPATF_NO_PASSMOBJ`.

The clipping behavior itself already reads `COMPATF_NO_PASSMOBJ` in the base
engine's movement code; these keywords only choose the per-level value.
