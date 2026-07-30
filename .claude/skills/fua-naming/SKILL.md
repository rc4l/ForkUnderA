---
name: fua-naming
description: Naming rule for identifiers unique to ZandroX (things WE invented, with no equivalent in Zandronum, UZDoom, GZDoom, or other source ports) — embed `fua_` so ours are instantly greppable and never collide with upstream on a re-sync. Use whenever adding a new CVAR, CCMD, DECORATE/ACS function, menu item, or any other named identifier that originates here.
---

# The `fua_` rule for ZandroX-original identifiers

If **we** invented it — it has no equivalent in Zandronum, UZDoom, GZDoom, or any other source
port — its name must carry **`fua_`**, placed right after the identifier's conventional prefix.
If there's no conventional prefix, it leads with `fua_`.

Pattern: `<standard-prefix>_fua_<name>`  (or just `fua_<name>` when there's no prefix).

| Kind | Upstream-style name | ZandroX-original name |
|------|---------------------|-----------------------|
| Client CVAR | `cl_namehere` | **`cl_fua_namehere`** |
| Server CVAR | `sv_namehere` | **`sv_fua_namehere`** |
| Other CVAR (`menu_`, `snd_`, `vid_`, …) | `menu_namehere` | **`menu_fua_namehere`** |
| Console command (no cvar prefix) | `namehere` | **`fua_namehere`** |
| DECORATE action | `A_NameHere` | **`A_fua_NameHere`** |
| ACS function (script-callable builtin) | `NameHere` | **`fua_NameHere`** |
| ZScript / other named function | `NameHere` | **`fua_NameHere`** |

Real examples already in the tree: `cl_fua_replay`, `cl_fua_replay_bitrate`, `cl_fua_flat_holes`,
`cl_fua_holedebug`, and the bindable command `fua_clip` (see `features/replay/`).

## When it does NOT apply

- **Ported or adapted upstream code keeps its upstream name.** Never rename `A_FireCustomProjectile`
  or `cl_bloodtype` — those are theirs. `fua_` marks *origin here*, not "we touched it."
- Internal C++ symbols (local functions, class members) don't need it — the rule is about
  **user-facing / lump-facing names** (CVARs, CCMDs, DECORATE/ACS/ZScript functions, menu names)
  that share a namespace with upstream and could collide or get confused on a re-sync.

## Why

- **Greppable:** `grep -r fua_` lists everything original to this fork in one shot.
- **Collision-proof:** an upstream re-sync can never introduce a name that clashes with ours.
- **Self-documenting provenance:** the name itself says "this is ours," complementing the
  `[rc4l]` comments and the commit tracker.

## Quick check before naming a new thing

1. Does this identifier already exist in Zandronum / UZDoom / GZDoom / another port? → use their
   name as-is (it's a port, not ours).
2. Is it new and ours, sharing a namespace with upstream (CVAR / CCMD / DECORATE / ACS / menu)? →
   sandwich `fua_` after the conventional prefix.
