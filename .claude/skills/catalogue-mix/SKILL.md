---
name: catalogue-mix
description: Adding a mix (gameplay mod) to catalogue/remix/ — the shared pool, group semantics, which experiences should offer it, and when a mix needs a cfg of its own. Use whenever adding a gameplay mod that layers on top of an experience, or changing which experiences can be played with one.
---

# Adding a mix

A **mix** is something layered on top of an experience: a gameplay mod, a weapon set, a monster
replacement. It is defined **once** in `catalogue/remix/<id>/remix.json` and named by the
experiences that can take it. Ids rather than definitions, because the same mix applies to many
packs and restating its files in each `addon.json` is the copies-drift problem this format refuses
everywhere else.

```json
{
  "name": "<what the pill shows>",
  "summary": "One sentence, shown on hover.",
  "group": "mix",
  "cfg": "<optional>.cfg",
  "files": [ { "name": "<mod>.pk3", "md5": "<32 hex>" } ]
}
```

`name` is required. `files` and `cfg` are both optional — the baseline mix has neither and still
needs a name.

## Groups are axes

`group` decides how choices relate:

- **Same group = mutually exclusive.** One pill lights at a time.
- **Different groups = independent axes.** Each gets its own row and they combine.

Group order on the panel is first appearance in the entry's `remixes` list. Every gameplay mod
shares one group, because they replace each other. Something genuinely orthogonal — an announcer, a
HUD, a scoreboard — is a **new group**, not another entry in that one. Getting this wrong is how a
player ends up unable to have two things that have no quarrel with each other.

**The first choice offered is the baseline**, conventionally an entry that adds nothing. Anything
that resolves to "no choice made" lands on the first offered, so write the list with the baseline
first, always.

## Which experiences offer it

Decided in each `addon.json`'s `remixes` list, in the order the panel draws them. Match reach to
what the mod actually covers:

- Covers people against monsters AND people against each other → every entry that offers mixes.
- PvE only → the cooperative and invasion entries, out of the deathmatch lists.
- PvP only → the deathmatch and duel entries.

Judge that by what the mod does, not by where you found it. A mod is on this axis at all because it
means the same thing to several packs; if it is only useful with one, it is not a mix — put its
files in that variant instead.

## When a mix needs a cfg

`cfg` is a bare filename beside the `remix.json`, exec'd **after** the experience's own cfg, so it
wins where they disagree. Give a mix a cfg only for the mod's own **requirements**, never for
taste:

- A mod built around picking things up repeatedly may require item respawn on, and may require the
  cooperative per-player weapon copies off because it hands weapons out itself. Those are demands,
  and it is entitled to overrule an entry that says otherwise.
- "It feels better with faster monsters" is not a requirement. Leave it out.

Write the cfg by the rules in `catalogue-cfg` — named cvars, and a comment saying which of them
override the entry and why, because that is the surprising part for whoever reads it next.

## The weapon-speed exclusion

A mix that **replaces the weapons** cannot be combined with a raised WEAPON SPEED, and the panel
enforces it in both directions (`weaponspick_compute`). Nothing in the catalogue declares this: the
rule is that any non-baseline mix owns the weapons. If you add a mix that layers on top of the
existing weapons rather than replacing them, that assumption is the thing to revisit — the
exclusion will be stricter than it needs to be, not looser.

## Verify

`+fua_catalogue` prints the remix pool with each file and cfg, and prints `plays with:` per entry.
Check the mix appears in the pool once and in every entry you meant. Then host an entry, pick the
mix, and confirm the file list at the foot of the panel changes to include it.
