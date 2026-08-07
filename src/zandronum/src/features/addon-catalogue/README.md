# features/addon-catalogue

An offline catalogue of addons, so hosting Brutal Doom is a pick from a list rather than a hunt for
six files in the right order.

## The shape

An addon is a **descriptor, not content**. We cannot legally ship Brutal Doom, and we do not need to:
the entry names files by hash, and `features/wad-download` already fetches them from mirrors into the
by-hash store. A catalogue entry is a pre-filled `HostConfig` plus a download plan.

```
catalogue/                  next to the exe, shipped in the release
  index.json                generated at packaging time; scan the folder if absent
  brutal-doom/
    preset.json             what it is: slots, hashes, compatibility
    server.cfg              how it launches: dmflags, gamemode, cvars
<userdir>/catalogue/        the player's own entries, and the pressure valve when a shipped hash
                            goes stale
```

Folders are flat and id-keyed. The taxonomy lives in `preset.json`, never in the path, because the
picker already groups by role at runtime and a path-borne category is a second source of truth that
drifts the first time something is recategorised.

## Why slots instead of combinations

N mappacks against M gameplay mods is N×M pairings and nobody can author that. So an entry declares
what it **fills** and what it **locks**, and the combination is composed at pick time. That is N+M of
curation for N×M of coverage.

| addon | fills | locks |
|---|---|---|
| Alien Vendetta, Scythe | `maps` | |
| Brutal Doom, Complex Doom | `gameplay` | |
| Skulltag | `gameplay`, `maps` | |
| Stronghold, All Out War | `gameplay`, `maps` | `gameplay`, `maps` |

`locks` is the whole difference between a total conversion that tolerates a mappack and one that is
its own game. A lock is only violated by somebody *else* filling the slot, so an entry filling and
locking the same slot is legal, which is the point of locking it.

Load order comes from the slot and never from the user: iwad, maps, gameplay, patch, cosmetic. That
ordering is what makes a gameplay mod's actors win over the mappack under it.

## What this will and will not tell you

It answers **"will this load and work"**, never "is this a good idea". A duel mappack under a weapons
mod is legal and usually unwise; a validator that editorialises about taste does not get believed
about correctness either.

Three verdicts, because two is not enough:

- **Blocked** on arity or a lock, and it cannot proceed.
- **Warned** on a declared conflict, or custom-actor maps under a gameplay mod.
- **Allowed** for everything else, *including pairs nobody has tried*. Silence on an untried
  combination is the correct answer, not a gap, and it is why this scales.

## Where the rules live

Facts in data, rules in code. `compat_compute.cpp` holds arity, locks, ordering and nothing else. A
per-pair rule must never appear there: the moment `if (brutal && duel40)` is written in C++, every
new mod becomes a code change. Known-bad specific pairs are `conflictsWith` in the entries, a list
proportional to the problems people actually hit rather than to the matrix.

## Notes for later

- Slot and actor-style names are parsed **by name, never by ordinal**, because they are written into
  files that ship on players' disks. Renumbering would silently change what an unchanged entry means.
  Same hazard `tools/wire-enum-snapshot.py` guards on the wire.
- The shipped catalogue and the engine always ship together and so cannot drift. Only
  `<userdir>/catalogue/` can be stale, which is what the per-file `schema` version is for, and an
  unknown entry there must be skipped with a message rather than failing startup.
- Offline is deliberate. Reading `index.json` from disk and fetching it from GitHub Pages later are
  the same file and the same parse, so nothing here forecloses that.

## Status

`compat_compute` only. No parser, no resolver, no UI yet.
