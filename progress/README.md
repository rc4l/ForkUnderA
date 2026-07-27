# Progress trackers

Per-feature ledgers tracking work that spans many commits. Each subfolder is one
long-running effort with its own `ledger.tsv` and `README.md` (schema + conventions).

- [renderer-staircase/](renderer-staircase/) — the commit-by-commit replay of GZDoom's
  2014–2016 renderer evolution into ZandroX (anchor `ad88cfc5e` → target `g2.1.1`).

New tracker? Copy the renderer-staircase schema; keep ledgers as TSV (greppable, diffable,
awk-friendly), one row per unit of work, header row first, oldest→newest.
