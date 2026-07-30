# Progress trackers

Long-running efforts, each in its own subfolder with a `README.md`.

- [renderer-staircase/](renderer-staircase/) — strategy + test maps for replaying GZDoom's
  2014–2016 renderer evolution into ZandroX (anchor `ad88cfc5e` → target `g2.1.1`).

Upstream port status is no longer tracked per-effort here — it lives in the repo-wide
[`commit-tracker/`](../commit-tracker/): one TSV row per upstream commit from the anchor to
UZDoom HEAD, marked `pending` / `ported` / `adapted` / `skip`, with a `path → commits` index
alongside it.
