# Renderer staircase

Strategy and rationale for replaying GZDoom's 2014–2016 renderer evolution into ZandroX
live in [`docs/renderer-staircase.md`](../../docs/renderer-staircase.md); the flight test maps
are in [`testmaps/`](testmaps/).

## Where the per-commit status went

The old `ledger.tsv` here (one row per renderer commit) has been **superseded by the
repo-wide commit tracker** at [`commit-tracker/`](../../commit-tracker/), which covers every
upstream commit from the same anchor (`ad88cfc5e`) to UZDoom HEAD — not just the renderer
window. Its ported / adapted / skipped rows were migrated in; three merge-commit annotations
that upstream history later rewrote away are preserved in the tracker's README.

Record staircase-flight status there now (`ported` / `adapted` / `skip`, with your zandrox
commit in the note). See `commit-tracker/README.md` for the status vocab and query recipes,
and `tools/commit-tracker-check.sh` for the CI guard.
