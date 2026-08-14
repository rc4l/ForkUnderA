---
name: catalogue-entry
description: How to add or change an experience in catalogue/ — the addon.json schema, variants, file lists and md5s, which settings are per-entry and which are per-variant, and how to verify it loads. Use whenever adding a new experience, adding a variation to one, or changing what an experience loads.
---

# Adding an experience to the catalogue

An **experience** is a folder under `catalogue/<id>/` holding an `addon.json` and one or more
`.cfg` files. The folder name is the id; nothing inside the file may claim a different one.

Three levels, and they are not interchangeable:

| | what it is | where it lives |
|---|---|---|
| **Experience** | a pack somebody picks from the HOST list | `catalogue/<id>/addon.json` |
| **Variation** | one way of playing that pack | a `variants[]` entry |
| **Mix** | something layered on top, shared by many experiences | `catalogue/remix/<id>/` — see `catalogue-mix` |

## The file

```json
{
  "name": "<what the list shows>",
  "summary": "One sentence. It is drawn under the title and wraps.",
  "iwad": "doom2.wad",
  "kind": "pve",
  "gamemode": "cooperative",
  "lives": 0,
  "maxlives": 5,
  "order": 20,
  "accent": true,
  "remixes": ["<baseline>", "<mix>", "<mix>"],
  "files": [ { "name": "<shared>.pk3", "md5": "<32 hex>" } ],
  "variants": [ ... ]
}
```

`name`, `files` and a label are required. **`kind` is required of whichever thing is actually the
experience**: the entry when it has no variants, every variant when it has. `pve` or `pvp`, nothing
else. An unlabelled experience is refused by name at startup.

There is no version field. The reader ignores keys it does not know, which is what lets a catalogue
written for a later build load on an earlier one, and a field that changes meaning is a change to
make in the reader rather than something to gate behind a number nobody bumps.

`order` floats an entry up the list (higher is nearer the top, default 0 keeps folder order).
`accent` draws its leading word in the accent colour. Both are curation, so use them sparingly: two
accented entries next to each other is a mark that has stopped marking anything.

## Variants

```json
{
  "id": "<stable id>",
  "name": "<what the row shows>",
  "cfg": "server.cfg",
  "kind": "pve",
  "default": true,
  "map": "MAP01",
  "tooltip": "One line: what this way of playing actually is.",
  "files": [ { "name": "<its own>.wad", "md5": "<32 hex>" } ]
}
```

- Ids must be unique within the entry and are what a remembered choice is keyed on. Renaming one
  silently forgets what a player had chosen.
- **Exactly one** variant may claim `"default": true`, and **its cfg must be `server.cfg`**. An
  older build ignores `variants` entirely and plays `server.cfg`, so this is what makes old and new
  agree about what an unchosen entry does.
- `map` is optional and is where THIS variant opens. Not the same as the first of its rotation: a
  pack may open on a welcome map deliberately left out of the rotation.
- A variant's `files` are loaded **after** the entry's, never instead of. Every way of playing must
  resolve to at least one file between the two.

### Settings that are per-variant

An entry may gather packs that are not alike. These override the entry's answer for one variant:

| key | meaning | absent means |
|---|---|---|
| `gamemode` | what the panel reads to know whether lives/teams apply | the entry's |
| `lives`, `maxlives` | `maxlives: 0` removes the lives control entirely | the entry's |
| `fastweapons` | offer the WEAPON SPEED slider | the entry's, OR'd |
| `teams` | offer the TEAMS slider | the entry's, OR'd |
| `remixes` | which mixes this way of playing takes | the entry's |

**`"remixes": []` means none.** Presence of the key is the override, not whether the list is
non-empty — that is how a variant sits inside an entry offering several mixes and takes none of
them, because its pack replaces what they would change.

Reach for these when one entry holds packs that do not want the same controls. If every variant
wants the same answer, say it once on the entry.

## Getting the files right

Never guess a file list or a rotation. For each file:

1. Download it and take its md5 (`Get-FileHash -Algorithm MD5`). The catalogue is content-addressed:
   having a file with the right *name* is not having the right file.
2. **Read the maps out of the wad**, do not count to thirty-two. Map lumps are not always
   contiguous and not always numbered from one; a rotation written from the count sends the server
   to maps that are not there. For a WAD, walk the directory for `MAPxx`/`ExMy` lumps; for a pk3,
   list `maps/`.
3. **Check the pk3 for a `GAMEINFO` lump.** `LOAD = "other.pk3"` means the engine loads that file
   itself. Still list it — that is what downloads it for somebody who has not got it — but know
   that a mod pulling in its own dependency is how an entry can host fine and be unjoinable.

Load order is the order listed, entry files first. A patch goes after what it patches; an announcer
or an overlay goes after the maps.

## Verify before committing

```powershell
./windows_build_run.ps1
./dist-windows/forkundera.exe -iwad freedoom2.wad +logfile chk.log +fua_catalogue
```

Read the log. It prints every entry, its variants, each file with its md5, and each resolved cfg
path. Check the **entry count** and that there is no `skipping` line: a malformed entry is dropped
with a reason and everything else still loads, so a typo shows up as one missing experience rather
than as an error anybody notices.

Then host it from the panel and join it. The catalogue loading is not the same as the server
starting, and neither is the same as a client being able to authenticate against it.
