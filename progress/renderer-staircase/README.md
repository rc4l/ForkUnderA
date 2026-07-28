# Renderer staircase ledger

One row per upstream (GZDoom) commit in the replay window: anchor `ad88cfc5e`
(2013-12-25, where Zandronum's GL renderer forked) → target `g2.1.1`
(2016-02-23, stable 2.1). The checklist answers, per commit: is it renderer
work, what part, did we take it, cleanly or adapted, where's our commit, and one
sentence of why.

## Schema (`ledger.tsv`, tab-separated, header row, oldest→newest)

| Column | Meaning |
|---|---|
| `sha` | upstream short SHA — primary key |
| `date` | upstream commit date (ISO `YYYY-MM-DD`) |
| `category` | which subsystem — see vocab below |
| `status` | `ported` / `adapted` / `skipped` / `deferred` / `pending` |
| `zandrox_sha` | our commit that carried it, or `—` |
| `note` | one sentence: for `adapted`/`skipped`/`deferred`, the *what+why*; for `ported`, upstream's own subject |

### `status` vocabulary

- `ported` — replayed as-is (hand-merges around [BB]/[AK] divergence still count as ported if no behavior changed).
- `adapted` — we changed something on purpose; `note` says what and why (fixed64 cast, Apple-core shim, etc.).
- `skipped` — deliberately not taken; `note` says why (x86-only, MSVC-asm, inapplicable to our tree).
- `deferred` — wanted later, not now; `note` links the issue.
- `pending` — in-window, not yet reached. Everything ahead of our position.

### `category` vocabulary

`renderer` `texture` `lighting` `shader` `models` `sky` `sprites` `dynlights`
`portal` `decal` `menu` `font` `hud` `system` `build` `bugfix` `other`

Category is the *dominant* subsystem the commit touches (heuristic seed, refined by hand
when a flight processes it).

## Conventions

- **Every flight updates its rows in the same commit that lands the code** (same discipline as
  the removal-guard rule). Advancing our position without ledgering fails the check below.
- **No silent skips**: a commit at or behind our position may not be `pending`. The
  flight-16 `setsectortag` class (a dependency discovered late) is exactly what this prevents.
- `zandrox_sha` on every `ported`/`adapted` row must exist in our history (provenance + GPL
  traceability + re-sync anchor).

## Position

Current replay frontier: **`abf11f1a1`** (2015-04-ish, end of flight 21 — just before the
stereo3D cluster). Everything at/behind it is resolved; everything ahead is `pending`.

## Regenerating

`tools/staircase-ledger-gen.sh <from-sha> <to-sha>` (run against the UZDoom clone) emits
base rows for a range. The seed marks GL-relevant commits `pending` with upstream subject as
`note`; a flight then edits the rows it resolves.

## Provenance of the initial fill

Rows were seeded by `staircase-ledger-gen.sh` (sha/date/category/subject) and backfilled once
against our flight history: `zandrox_sha` assigned by walking each flight's upstream range;
`status` set by position (behind frontier `abf11f1a1` => `ported`, ahead => `pending`); the 14
documented deviations (`adapted`/`skipped`/`deferred`) overlaid with their one-sentence notes.
`ported` here means "replayed within a flight" (our no-silent-skips discipline makes that
sound) — not a claim that each was individually re-verified this pass. Refine any row's
`category`/`note` when a flight next touches it.

## Check (CI)

`tools/staircase-ledger-check.sh` guards two invariants: no `pending` row behind the frontier
(silent-skip guard), and every resolved row's `zandrox_sha` exists in our history.
