---
name: catalogue-cfg
description: Writing a server.cfg for a catalogue experience — named sv_/compat_ cvars instead of raw dmflags numbers, how to decode a number somebody hands you, and the catalogue-wide conventions for skill, ammo, rotations and votes. Use whenever writing or editing any cfg under catalogue/.
---

# Writing a catalogue cfg

The cfg is handed to the spawned server with `+exec` and is the whole of how an experience plays.
The client never reads it, with one narrow exception (`addmap` lines, for the FIRST MAP picker).

## Never write a flag number

```
// NO
dmflags 3227652
dmflags2 256

// YES
sv_weaponstay true
sv_itemrespawn true
sv_nojump true
sv_nofov true
sv_noweaponspawn true
sv_bfgfreeaim true
```

The numbers are easy to get wrong and say nothing to whoever reads them next. Every bit has a named
cvar; use it. This is not a style preference — a misread digit has silently changed how an
experience plays, and a name cannot be misread.

### Decoding a number somebody gives you

Servers and forum posts quote `dmflags` / `dmflags2` / `dmflags3` / `compatflags` / `compatflags2`.
To turn one into names, read them out of the engine rather than a wiki:

1. `CVAR(Flag, <cvar>, <bitfield>, <FLAG>)` in `src/zandronum/src/` maps each cvar to its bit.
2. The bit values are the enums in `doomdef.h` (`DF_*`, `DF2_*`, `ZADF_*`, `COMPATF_*`, `COMPATF2_*`).
3. Match the bitfield name to the field being quoted.

`dmflags3` in the wild is `zadmflags`. **Check that every set bit is accounted for** — a leftover
means you read the wrong field, and unaccounted bits are how a misreading survives review.

Never transcribe a flag number off a low-resolution screenshot. Upscale it or ask.

## Catalogue conventions

These hold across every entry unless there is a reason in the file saying otherwise:

- **PvE runs `skill 3`** (Ultra-Violence). Deathmatch may run `skill 4` (Nightmare).
- **`sv_doubleammo true` unless the experience is on skill 4** — Nightmare already doubles it. Skip
  it where the ammo is infinite anyway, and say so, or it reads as an oversight.
- **No chasecam, anywhere.** Third person is a cheat.
- **No degeneration.** Health above 100% draining away is not wanted in this catalogue.
- **A written rotation allows votes:**
  ```
  sv_nomapvote false
  sv_nochangemapvote false
  sv_nonextmapvote false
  ```
- **`sv_randommaprotation 0` for campaigns.** They get harder as they go, and a shuffle drops a
  fresh party into the middle of one with the weapon they started with. Shuffling is a deliberate
  choice for arena packs, not a default.

## Rotations

Write the rotation out with `addmap`, one per line, **read from the wad** — never counted to a
round number. See `catalogue-entry`. A pack whose own mapinfo chains its maps needs no rotation at
all; say that in a comment rather than writing a second opinion about an order it already gives.

The rotation is also what the FIRST MAP picker offers, and the engine begins the rotation at the
map the host chose. On a shuffled rotation that choice means nothing, which is another reason to
keep campaigns in order.

## Comments

One or two sentences, then move on. Say **why**, not what — `sv_nojump true` does not need
explaining, but the reason a pack turns item respawn off when everything else leaves it on does.
Mark anything that is our judgement rather than the source server's with `[rc4l]`.

When several variants of one experience share a cfg body and differ only in rotation, say so at the
top of each: *"If you change something above the rotation, change it in all of them."*

## Verify

Exec'd cfgs are not covered by the tests. After writing one:

```powershell
./windows_build_run.ps1
./dist-windows/forkundera.exe -iwad freedoom2.wad +logfile chk.log +fua_catalogue
```

then host the variant from the panel and check the map it opens on and the settings it reports.
`hosting:` lines in the client log show exactly what the child process was told.
