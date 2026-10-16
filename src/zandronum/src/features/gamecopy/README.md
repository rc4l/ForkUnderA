# features/gamecopy

What the renderer reads of the game, copied in the handoff once a tic, so a frame drawn while the next tic runs
never reads what that tic is changing.

- `fields.h` is the one declaration: each `ZX_COPY_FIELD(kind, name, ...)` line is a field the renderer reads,
  and from that line come the copy's struct, its fill, the comparison that tells it changed, its blend between
  two tics, its hash and its dump, `fua_copy_check`'s comparison against the live game, and the boundary
  tripwire's pattern for reading it live.
- Map structures keep their copy beside the live fields (`sector->rc`, `side->rc`, ...), so the renderer walks the
  same pointers and reads `rc`; actors and players are copied as records, so the renderer holds no live actor.

## In-engine hooks

None yet.

<!-- boundary:begin (python3 tools/boundary_tripwire.py --doc writes this section) -->
## What crosses the boundary

Generated from the field tables and the `ZX_SEAM` declarations; do not edit by hand.

Copied by `features/gamecopy/fields.h`:

- line: `flags`, `special`, `Alpha`, `args`
- sector: `planes`, `floorplane`, `ceilingplane`, `ColorMap`, `special`, `lightlevel`, `MoreFlags`, `Flags`, `reflect`
- side: `textures`, `Light`, `Flags`
- vertex: `x`, `y`

Seams:

- `copy/blend` (Main), `features/gamecopy/gamecopy.cpp`
- `copy/fill` (Handoff), `features/gamecopy/gamecopy.cpp`
<!-- boundary:end -->
