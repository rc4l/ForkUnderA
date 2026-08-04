---
name: provenance-links
description: How to attribute ported/backported code in this ZandroX fork with permanent upstream provenance links. Use whenever porting or adapting code from an upstream engine (GZDoom, Zandronum, UZDoom, Q-Zandronum) so future re-syncs and GPL provenance are traceable.
---

# Provenance links for ported code

ZandroX is a GPL fork that backports and adapts code from upstream engines. Every piece of
**ported or adapted** code carries a permanent link to the exact upstream commit it came from.
These links make future upstream re-syncs cheap — they tell you at a glance whether a unit is
behind upstream, has diverged, or was already fixed here — and they document GPL provenance
alongside `THIRD-PARTY-NOTICES.txt`.

## When to apply

Add a provenance link when the code is **ported or adapted from an upstream** (GZDoom,
Zandronum, UZDoom, Q-Zandronum) — a function, method, or self-contained block whose logic
follows upstream's.

**Skip it for:**
- Original code and our own glue/wiring (no upstream equivalent).
- Our own fixes to vendored code — those get a **"why" comment instead** (see below).
- Trivial one-liners and mechanical changes (a cast, a rename, a signature tweak).

## Pick the source: prefer canonical upstream

Before porting from a fork (Q-Zandronum, etc.), **check whether UZDoom/GZDoom already has the
feature or fix** — read the actual code, don't stop at the first `if` block. If upstream has it,
port UZDoom/GZDoom's version and tag `uzdoom@<sha>`: it's usually more complete (e.g. the rail
floor/ceiling puff — Q-Zandronum had it, but only UZDoom added the sky-flat guard). Port a fork's
version (and tag it) only when upstream genuinely lacks it, or the fork's is deliberately different.

## Format

One link per **ported unit** — on that unit's header comment, using our existing `// [rc4l]`
tag. Block-level only; never per-line.

```cpp
// [rc4l] Ported from uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514: snd_alresampler applies the
// chosen AL_SOFT_source_resampler to every source; "Default" keeps the driver's own choice.
```

Rules:
- **Pin a full 40-char commit SHA**, never a branch name, tag, or line number — branches move
  and lines drift, a SHA is permanent. Form: `<engine>@<full-sha>`, e.g.
  `uzdoom@7bfbf612d9d8197c36bb77ab171005bce521a514`, `gzdoom@<full-sha>`,
  `zandronum@<full-sha>`, `qzandronum@<full-sha>`.
- Follow the SHA with a colon and a short description of **what the unit is** — not a changelog.
- **One link per ported unit.** If a function is a faithful port, tag the function. If only one
  block inside an otherwise-original function is ported, tag that block. Don't restate it on
  every line or every sibling statement.
- Get the SHA from the upstream checkout you ported from: `git -C <repo> rev-parse HEAD`.

## Original fixes get a "why", not a link

Code that is **ours** — a bug fix, a workaround, new glue — never points upstream. It gets a
comment explaining the **reasoning**, so the next reader understands the intent:

```cpp
// [rc4l] double(FRACUNIT), not bare FRACUNIT: FRACUNIT is now the strong Fixed type, so
// FRACUNIT * sin(...) bound to operator*(Fixed,int) and truncated sin to 0 -- zeroing the table.
```

A pure `*_compute.cpp` helper we **extracted** to make upstream logic testable is our own
structure: give it a "why"/what-it-does comment. You may still mention which upstream behavior
it mirrors in prose, but the authoritative `<engine>@<sha>` link belongs on the **call site**
in the backend where the port actually lives.

## Divergent fixes: say what will replace them

Sometimes upstream's real answer exists but is **out of reach** — it lives behind a rewrite, a
directory restructuring, or thousands of pending commits — so we write our own smaller fix instead.
That code is ours, but unlike ordinary glue it is **temporary by construction**: one day the
sequential backport reaches the commit that supersedes it, and whoever is doing that port needs to
know to *delete* our version rather than merge it, reconcile it, or wonder why it exists.

Say so, in the code, at the point of divergence:

```cpp
// [rc4l] PROVENANCE: NO UPSTREAM COMMIT -- ours.
//   SUPERSEDED BY: uzdoom@b77a0eb7cf9eab87aa9abfa3b7789af7c8a67571 (2017-02-01) "let D_PageDrawer
//   always clear the background". It replaces FillBorder(NULL) with an unconditional Clear of the
//   whole screen, so no strip can be left unpainted and the band this guards against cannot exist.
//   ON PORT: take that commit and DELETE this branch outright rather than reconciling it.
```

Three fields, all load-bearing:

- **`NO UPSTREAM COMMIT -- ours`** — states plainly that no SHA can be cited, so nobody wastes time
  searching for one or assumes the tag was forgotten.
- **`SUPERSEDED BY:`** — the full SHA, date and subject of the upstream commit that makes ours
  redundant, plus one line on *why* it does. If genuinely nothing will ever supersede it (our code
  touches a file upstream does not have), say `SUPERSEDED BY: nothing.` and why — that is equally
  useful, because it tells the porter to leave it alone.
- **`ON PORT:`** — the instruction. Delete, adopt-and-delete, or leave. A porter reading this under
  time pressure should not have to infer the intent.

Finding the successor is usually a single search of the upstream checkout — `git log -S <symbol>`
for the code you are working around, or `git log --diff-filter=A -- <file>` for the file that
replaces yours. Do it while the context is fresh; it is far more expensive to reconstruct later.

Record the same thing in `commit-tracker/coverage.tsv`: leave the superseding commit's row
`pending`, and note in ours that a local fix will need removing when it lands.

## Don't over-tag

The goal is traceability, not noise. Block-level links on the units that matter; a clean tree
everywhere else. If you find yourself adding the same SHA to five adjacent lines, collapse it to
one link on the enclosing unit.

## Quick check before committing ported code

1. Is this adapted from upstream? → add `// [rc4l] Ported from <engine>@<full-sha>: <what>`.
2. Is it our own fix/glue? → add a "why" comment, no upstream link.
2b. Is it ours *because upstream's fix is out of reach*? → add `PROVENANCE: NO UPSTREAM COMMIT`,
   `SUPERSEDED BY:` and `ON PORT:` so the sequential backport knows to delete it.
3. Is the SHA a full 40-char commit hash (not a branch/tag/line)? 
4. One link per unit, on its header — not per line?
5. The SHA you tag is a row in `commit-tracker/coverage.tsv` — set that row's status (`ported` /
   `adapted`) with your commit sha in the note, so the coverage ledger and the code agree.
