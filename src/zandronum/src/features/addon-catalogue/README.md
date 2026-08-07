# features/addon-catalogue

An offline catalogue of things you can play, so hosting Duel 40 is a pick from a list rather than a
hunt for the right files in the right order.

## An entry

A folder in `catalogue/`, complete and hostable on its own.

```
catalogue/duel40/
  addon.json     what to load: name, iwad, files with their md5
  server.cfg     how it plays: gamemode, map rotation, dmflags
```

**We read `addon.json`. Zandronum reads `server.cfg`.** We hand the second over with `+exec` and
never parse it, which is why reusing its own format was worth doing: a format we cannot get wrong,
and one operators already know how to write.

The **folder name is the id**. It is never read from inside the file, because the folder is what a
player renames and two sources for one identity can only disagree.

An entry is **N files in load order**, not one. Skulltag is its content pk3 then a spree announcer.
Anything that is not something you would pick on its own is a line in some entry's `files[]` rather
than an entry of its own, which is what keeps `catalogue/` a list of choices.

Entries **ship manifests, not content**. We cannot legally ship Brutal Doom and do not need to: the
md5 goes to `features/wad-download`, which already fetches from mirrors into the by-hash store.

## No composition, for now

Every entry is complete. Nothing combines with anything. "Brutal Doom on Sunlust" is its own folder
if someone wants it.

That is a deliberate cut. A slot model paying for itself needs a catalogue big enough that authoring
the popular pairs by hand costs more than the machinery does, and at the tens of entries this starts
at, it does not. `compat_compute` is in the tree, tested and unwired, for the day that changes.

## Presentation

The picker is its **own screen, not a deeper menu**. People do not dig through menus, so it is a tab
beside PUBLIC and PRIVATE rather than a level below HOST, and everything happens on it.

```
PUBLIC   PRIVATE   [ HOST ]
--------------------------------------------
 search: ___          | Duel 40
                      | Forty-map duel pack.
 > Duel 40            |
   Skulltag           | Loads 2 files, 42 maps
   Custom setup...    | On freedoom2 (wants doom2)
                      |
                      | Name    My Server
                      | Players 8
                      |   [ START SERVER ]
```

Same list-and-detail shape the browser already uses, so no new interaction to learn. Custom is a row
rather than a mode, so the manual form costs a step only for the people who want it.

**The picker knows nothing about hosting.** It answers "what do you want to play" and returns an id.
That one rule is what lets an offline picker reuse it later with only the right-hand panel differing.

**Search is the interface at scale.** Past a few screens nobody browses, so it is built in from the
start rather than added once the list is already too long, and selection follows the entry rather
than the row so typing never moves the highlight onto something else.

## The units

| unit | answers |
|---|---|
| `addonfile_compute` | what one `addon.json` says, or why it was refused |
| `iwadpick_compute` | which IWAD to actually host on |
| `pickerview_compute` | which rows are visible and which is selected |
| `compat_compute` | whether a set may load together (unwired) |

## Notes

- The reader takes a **restricted JSON**: one flat object, strings and ints, one array of flat
  objects. Widening it past the schema would only widen what can go wrong, and these files come off a
  player's disk as readily as out of our release. Unknown keys are skipped so a later optional field
  cannot orphan older entries.
- Entries carry a `schema`. The shipped catalogue and the engine always ship together and cannot
  drift, so this exists for `<userdir>/catalogue/`, and an entry from the future is skipped with a
  message rather than read with today's meanings.
- Offline is deliberate. Reading an index from disk and fetching one from GitHub Pages later are the
  same parse, so nothing here forecloses it.

## Status

Parsing and picking. No loader walking the folder yet, no UI, nothing wired.
