# The catalogue

The catalogue is the list of things a player can host. Each item is a folder of JSON and cfg.
No code changes when you add one.

Words used here:

| Word | Means |
|---|---|
| **Experience** | One folder. One pack, or one family of packs. Shown as a row in the list. |
| **Variant** | One way to play an experience. Shown as a pill. |
| **Mix** | A mod layered on top. Shared by many experiences. Shown as a pill. |

## Where it lives

Two roots are read, in this order. Same id in both means the second wins.

| Root | Path | Who writes it |
|---|---|---|
| Shipped | `<install>/catalogue` | This repo |
| User | `<savegames>/catalogue` | The player |

On a portable install both resolve to the same folder. It is then read once.

## Layout

```
catalogue/
  <entry-id>/
    addon.json       required
    server.cfg       required
    <variant>.cfg    one per variant
  remix/
    <mix-id>/
      remix.json     required
      <mix>.cfg      optional
```

Folder names are ids. Nothing inside a file sets its own id.

Only direct children of `catalogue/` are experiences. A folder with no `addon.json` is ignored.
That is why `remix/` is not read as one.

## addon.json

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | string | yes | What the list shows. |
| `kind` | `pve` or `pvp` | if no variants | Who you fight. |
| `summary` | string | no | One line about the pack. |
| `iwad` | filename | no | Preferred IWAD. The engine may still pick another. |
| `map` | lump name | no | Map to open on. Not the first map of the rotation. |
| `files` | array | see below | What every variant loads. |
| `variants` | array | no | Ways to play. Omit for a pack that plays one way. |
| `remixes` | array of ids | no | Mixes this can take. |
| `gamemode` | see table | no | The mode every variant runs in unless it says otherwise. |
| `lives` | int | no | Lives when nobody has chosen. Default 0. |
| `maxlives` | int | no | Highest the slider goes. Default 0, which hides it. |
| `fastweapons` | bool | no | Offer the weapon speed slider. Default false. |
| `teams` | bool | no | Offer the team count slider. Default false. |
| `order` | int | no | Higher floats nearer the top. Default 0. |
| `accent` | bool | no | Draw the first word of the name in the accent colour. |
| `art` | string | no | Override the menu picture. See Menu art. |

`files` is required only when there are no variants. With variants, each variant must end up
loading something. It may come from either list.

Unknown fields are skipped, not refused. A catalogue written for a later build still loads.

### variants[]

| Field | Type | Required | Meaning |
|---|---|---|---|
| `id` | string | yes | Stable. A remembered choice is keyed on it. |
| `name` | string | yes | What the pill shows. |
| `cfg` | filename | yes | Exec'd instead of the entry's. Must exist in the folder. |
| `kind` | `pve` or `pvp` | yes | Who you fight. |
| `tooltip` | string | no | One line, shown on hover. |
| `map` | lump name | no | Where this one opens. Falls back to the entry's. |
| `files` | array | no | Loaded after the entry's. |
| `remixes` | array of ids | no | Replaces the entry's list. See below. |
| `gamemode` | see table | no | Falls back to the entry's. |
| `lives` | int | no | Falls back to the entry's. |
| `maxlives` | int | no | Falls back to the entry's. |
| `fastweapons` | bool | no | Added to the entry's. |
| `teams` | bool | no | Added to the entry's. |
| `art` | string | no | Override the menu picture. Falls back to the entry's. |
| `default` | bool | no | The one an undecided player gets. At most one variant. |

The default variant's `cfg` should be `server.cfg`. An older build ignores variants and plays
`server.cfg`. Then both agree.

### files[]

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | filename | yes | Bare filename. No path. |
| `md5` | 32 hex | yes | Lower case only. Used to find and verify the file. |
| `provides` | role | no | What this file is. See Roles. |

## remix.json

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | string | yes | What the pill shows. |
| `summary` | string | no | One line, shown on hover. |
| `group` | string | no | Which axis. Empty is the default axis. |
| `cfg` | filename | no | Exec'd after the experience's. Must exist in the folder. |
| `files` | array | no | Loaded after everything else. |
| `provides` | array of roles | no | What this mix already contains. See Roles. |
| `gamemode` | see table | no | The way of playing this mix switches to. |
| `art` | string | no | Override the menu picture. See Menu art. |

A mix with neither `cfg` nor `files` is legal. That is the baseline.

### Mixes an entry owns

A mix normally lives in the shared `remix/` folder and any entry may list it. A mix whose cfg
names a map rotation belongs to ONE entry, so it goes in that entry's own folder instead:

```
catalogue/mm8bdm/remix/ctf/remix.json
```

Its id is then `mm8bdm/ctf`, and that is what `remixes` must list. The prefix keeps the pool flat
and the ids unambiguous. An entry's copy of a name wins over the shared one.

### Modes as pills

A mix that names a `gamemode` switches the mode. Put them all in one group so exactly one lights.

The mode a mix names beats the entry's and the variant's. Setting a gamemode cvar clears every
other one, so a mode cfg needs only its own line.

| Order things are applied |
|---|
| The entry's cfg, then each mix's cfg, then the panel's own cvars. |

So a mix cfg sets a floor and the sliders still win.

**List mods before modes.** Axes are drawn in the order `remixes` names them, so whichever group
appears first is the top row of pills.

## Menu art

An entry, variant or mix can show a small picture instead of its text header. The picture is pulled
out of the files it loads by a tool, shrunk, and committed beside the JSON. **The files are not
shipped; the picture is.**

| File | Belongs to |
|---|---|
| `art.png` | An entry that plays one way, or a mix |
| `art.<variant>.png` | One way of playing |

You do not write these by hand. Run the tool:

```
python .claude/skills/catalogue-menu-art/generate.py --store <folder holding the loaded files>
```

Nothing is required. A slot with no picture draws its name, which is a designed fallback and not a
degraded one.

The tool finds the picture on its own: what the pack's menu definition draws, else the conventional
logo lump, else the title screen. When that is wrong, the slot says so with `art`:

| Value | Means |
|---|---|
| `"art": "<lump>"` | Use this lump instead |
| `"art": "<file.png>"` | Use a picture you put beside the JSON |
| `"art": ""` | No picture. Draw the name |
| absent | Work it out. Nearly always right |

A value with an extension is a file. Lump names have none.

Use it only when the source is not what it claims: a logo lump holding an empty frame, a menu
drawing the base game's logo rather than the pack's, a blank spacer where the real title is a
rendered map. Not for taste.

The file form is for a pack with no usable art inside it at all, where the only logo is one you
have from elsewhere. It goes through the same shrinking and the same budget as an extracted one, so
the two cannot end up looking like they came from different tools.

## Roles

A mod may ship its own copy of something an experience loads separately. A spree announcer, for
example. Loading both is not a duplicate file, because the names differ. It is the same thing
twice. The player hears every announcement played over itself.

A role fixes this. Two halves:

| Where | Says |
|---|---|
| `provides` on a file | This file **is** a spree. |
| `provides` on a mix | This mix **already has** a spree. |

When a chosen mix provides a role, files filling that role are not loaded. A mix never drops its
own files.

Roles are plain lower case words. They are shared by convention. Nothing declares the list.

| Role | Used for |
|---|---|
| `spree` | Killing spree announcer. |

Never name a filename here. A filename carries a version. When the file is replaced by its next
release, a mix naming the old one goes on naming something nobody loads, and says nothing about
the file that took its place. It fails silently, because naming a file nobody loads is also the
normal case. A role outlives the release. It sits beside the filename, which is the line an
upgrade has to edit anyway.

## Values

### kind

| Value | Meaning |
|---|---|
| `pve` | Players against monsters. |
| `pvp` | Players against each other. |

Anything else is refused.

### gamemode

Accepted values:

`cooperative`, `survival`, `invasion`, `deathmatch`, `teamdeathmatch`, `duel`, `lastmanstanding`,
`teamlms`, `possession`, `teampossession`, `terminator`, `ctf`, `skulltag`.

Anything else, including an absent field, means unknown. Unknown is not an error. It costs the
controls that read the mode.

This field does not set the mode. Your cfg does. This field tells the panel which controls make
sense. Say the same thing in both.

## Entry against variant

| Field | Rule |
|---|---|
| `gamemode`, `map`, `lives`, `maxlives` | The variant wins when it states one. |
| `fastweapons`, `teams` | Either saying yes is yes. |
| `files` | Both load. Entry first. |
| `remixes` | The variant replaces the entry's. |

`remixes` is keyed on the key being present, not on the array having contents. `"remixes": []`
means this variant takes no mixes. Omitting the key means it takes the entry's.

## Load order

1. The entry's `files`.
2. The chosen variant's `files`.
3. Each chosen mix's `files`, one axis after another.
4. Any file whose role a chosen mix provides is then removed.

Later files win where two disagree. Put content that must be overridden first.

## Groups

`group` decides how mixes relate.

| Case | Effect |
|---|---|
| Same group | Mutually exclusive. One pill lights. |
| Different groups | Independent. They combine. |

The first mix an entry names in a group is that group's baseline. A player who has never chosen
gets it. Write the baseline first.

## Controls the panel derives

These are not fields. They are decided from the fields above.

| Control | Shown when | Range |
|---|---|---|
| Lives | `maxlives` above 0, and the mode is one of the five below | See the lives table. |
| Teams | `teams` is true, and the mode has a team twin | 0, 2, 3, 4. There is no 1. |
| Weapon speed | `fastweapons` is true | 0 to 2. Above 0 also turns on infinite ammo. |

Lives mean different things per mode. This is Zandronum's behaviour, not ours.

| Mode | Lowest | What 0 means |
|---|---|---|
| `cooperative` | 0 | Cooperative. Any higher value switches the mode to survival. |
| `invasion` | 0 | Genuinely unlimited. |
| `survival`, `lastmanstanding`, `teamlms` | 1 | Not available. One life is the floor. |

Every other mode has no lives control at all. The engine decides this, not us: a mode gets the
control when its `gamemode.txt` block carries `USEMAXLIVES`.

Three modes have a team twin, and `teams` switches between them.

| Free-for-all | Teams |
|---|---|
| `deathmatch` | `teamplay` |
| `lastmanstanding` | `teamlms` |
| `possession` | `teampossession` |

`ctf` and `skulltag` get no control. They have sides, but the count is the map's: one flag per
side, so a third team would spawn with nothing to take. `terminator` gets none either, because one
ball against everyone is the mode.

A slider needs room to move. When the lowest and `maxlives` are the same value it is not drawn,
but the value is still sent to the server.

Raising weapon speed and picking a non-baseline mix are mutually exclusive. A mix that replaces
the weapons owns them.

## What is refused

A bad experience is skipped and named in the console. The rest of the catalogue still loads.

| Rule | Applies to |
|---|---|
| Must be one JSON object, with nothing after it | addon.json, remix.json |
| `name` must be present | entry, variant, mix |
| `kind` must be `pve` or `pvp` | entry with no variants, every variant |
| Filenames must be bare: no `/`, `\`, `..`, `:` | files, cfg, iwad |
| `md5` must be 32 lower case hex digits | files |
| `map` must be a lump name, not starting with `-` or `+` | entry, variant |
| `server.cfg` must sit beside `addon.json` | entry |
| Every variant's `cfg` must exist in the folder | entry |
| A mix's `cfg` must exist in its folder | mix |
| Every variant must load at least one file | entry |
| Variant ids must be unique | entry |
| At most one variant may set `default` | entry |
| A remix id may be listed once | entry |
| A provided role may not be empty | mix |

Existence of a cfg is checked. Its contents are not. A cfg is console commands, and the engine
decides what a line means.

## Checking your work

`fua_catalogue` in the console prints every experience, its variants, its files and the mix pool.
Broken folders are printed in red at startup with the reason.

`fua_host <id> [variant]` hosts one without using the menu.
