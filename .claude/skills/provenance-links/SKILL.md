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

## Don't over-tag

The goal is traceability, not noise. Block-level links on the units that matter; a clean tree
everywhere else. If you find yourself adding the same SHA to five adjacent lines, collapse it to
one link on the enclosing unit.

## Quick check before committing ported code

1. Is this adapted from upstream? → add `// [rc4l] Ported from <engine>@<full-sha>: <what>`.
2. Is it our own fix/glue? → add a "why" comment, no upstream link.
3. Is the SHA a full 40-char commit hash (not a branch/tag/line)? 
4. One link per unit, on its header — not per line?
