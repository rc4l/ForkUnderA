---
name: catalogue-menu-art
description: Generating the small menu-art thumbnails a catalogue entry, variant or mix shows in place of its text header, using the generate.py and menuart.py scripts in this skill. Use when adding or changing anything the catalogue loads, when a header should show art instead of a name, when the panel's size or colours change, or when the art looks wrong.
---

# Menu art for the catalogue

An entry, variant or mix can show a picture instead of a text header. The picture comes out of the
archives it loads, shrunk to a few kilobytes and committed. **The archives are not shipped; the art
is.** That is the whole point: the identity of a pack costs kilobytes, the pack costs hundreds of
megabytes.

Two scripts sit beside this file. Neither needs anything but Pillow.

| Script | For |
|---|---|
| `generate.py` | The whole catalogue at once. **This is the normal entry point.** |
| `menuart.py` | One slot. Use it to try a single case before changing anything. |

```
python .claude/skills/catalogue-menu-art/generate.py --store <folder holding the loaded files>
python .claude/skills/catalogue-menu-art/menuart.py <archive> [<archive> ...] -o art.png
```

Archives go in **load order**. The tool applies the engine's own rule, so it returns what a player
would actually see.

`generate.py` writes `art.png` beside an entry that plays one way, `art.<variant>.png` for each way
of playing, and `art.png` beside each mix. Its settings live in that file rather than in somebody's
shell history, so the next run reproduces the same catalogue. Nothing about any particular pack
lives there.

Every referenced file has to be in the store, or the load order is incomplete and a slot can resolve
to the wrong picture. The script says which are absent; do not ignore it.

## What it picks

| Order | Source | Why |
|---|---|---|
| 1 | What the pack's **menu definition** draws | The only one that says what is really on screen |
| 2 | `M_DOOM` | The convention. A logo, made to be read small on a dark background |
| 3 | `TITLEPIC` | A whole illustration. Recognisable at this size, its fine text is not |
| - | none of them | No file is written. The caller falls back to the text header |

The first step matters more than it sounds. The conventional name is a **convention**: a pack that
replaces the main menu outright names its own graphic, and then the picture a player actually sees
is one nothing conventional would look for. Such a pack has a perfectly good logo and appears to
have none, which is a silent and very convincing failure.

Blank graphics are skipped at every step. Archives ship them as placeholders and spacers, they
decode perfectly, and a flat slab in place of a name is worse than the name.

Exit code 0 means art was written, 1 means there was none, 2 means it could not reach the budget.
Use `--json` when a script needs the details.

## The size is fixed first, the budget second

This order matters and is the whole design:

1. Fit the slot, aspect kept. The slot is a UI decision and is never traded away.
2. Drop colours until the PNG fits the budget.

Reversing it produces an image smaller than its slot, which then gets stretched back and looks
worse than a posterised one of the right size. Resolution is what the eye reads; colour count is
what it forgives.

## Ship larger than the slot

The panel is measured in layout pixels, and a real screen multiplies them. A slot three layout
pixels tall is nine real pixels on a screen scaled 3:1. **So an asset the size of its slot arrives
already needing a 3x upscale**, and looks it.

Ship at roughly twice the slot's layout height. The engine's own scaling closes the rest and does it
better than a hard upscale of something too small. Measured, drawing into the same slot:

| Asset | Engine scales | Result |
|---|---|---|
| slot size | 3.0x up | Blocky, or mushy with smoothing |
| ~2x slot | 1.7x up | The setting. Clean at the sizes screens actually use |
| ~3x slot | 1.1x up | Sharper still, but see the budget below |

## Spending the budget

With the byte ceiling fixed, a taller asset buys pixels by giving up colours. That trade is worth
knowing because it goes the opposite way to intuition. At one fixed budget:

| Asset | Colours it can afford | Look |
|---|---|---|
| 2x slot | most | **Best.** Fire and gradients stay smooth |
| 2.5x | fewer | Slightly flatter |
| 3x | few | Visible banding on anything with a glow |
| 4x | fewest | Worst, and the widest art breaks the ceiling |

Colour beats resolution here. Smooth scaling hides a 1.7x resolution gap; nothing hides banding.
So when the budget binds, come **down** in size rather than up.

## Transparency

Logos have it and backgrounds do not, and that difference is not cosmetic. Measured across a full
catalogue:

| Lump | Opaque | Binary alpha | Antialiased |
|---|---|---|---|
| Logo | 5 | 19 | 8 |
| Background | all | none | none |

A logo flattened onto one colour draws that colour as a **rectangle around itself** the moment the
surface behind it is not that colour. Panels are gradients, so it always is. The tool keeps
transparency, and there is nothing to configure for it.

What it does with the in-between pixels is worth knowing, because it is a deliberate approximation:

- **Fully clear stays clear**, recorded as one palette entry. This is the part that stops the box.
- **Part-transparent pixels are blended into the surface** named by `--background`, then stored
  opaque.

Full per-pixel alpha would need a palette of colour-and-alpha pairs, and the only pixels wanting it
are the antialiased rim. Blending that rim into the surface it will be drawn on reproduces it
exactly, for one palette entry and no format complexity. The approximation shows only where the real
surface differs from the colour given, and across the height of one logo a panel varies by a level
or two.

So `--background` must be the **composite** the art will sit on, not one layer's colour. Read the
drawing code and work out what the layers actually add up to; a panel is usually several deep and
its declared colour is rarely what reaches the screen.

## Traps this already handles

Do not re-solve these:

- **A truncated picture.** Archives ship images with no terminating chunk. Engines draw them; strict
  decoders refuse them. The tool reads them anyway, because refusing would silently lose the art of
  something that works in game.
- **Two copies of one name in one archive.** A low-resolution lump beside a high-resolution
  replacement. The later one wins, which is the archive stating its own preference.
- **No palette anywhere in the load order.** Add-ons rarely ship one because the base game supplies
  it, and a load order usually starts with an add-on. A stock palette is built in as the last
  resort. Anything shipping its own still wins.
- **Non-square pixels.** Source pixels are taller than they are wide. The tool corrects this, which
  is why the output matches what a player sees rather than a squashed copy.
- **A picture whose last byte is its terminator.** Requiring a byte after it rejects every
  well-formed picture whose final column ends the lump, which is most of them.

## Verify by looking

Byte counts prove nothing about whether a picture decoded correctly. A wrong palette or a misparsed
picture produces a plausibly sized file of garbage. **Open the output.** Build a contact sheet of
every slot at the size the panel will draw it, and look at the whole thing before committing.

Some art will be poor, and the tool is not at fault. Sources vary: a logo may be eighteen pixels
tall, or low contrast, or an illustration rather than a wordmark. Check the source at its native
size before concluding the tool broke it. It usually did not.

## When a slot has to say for itself

The automatic answer is right nearly always. When it is not, the slot says so **in the catalogue**,
not in this tool. A pack's art is a fact about that pack, and the packs are described over there;
a list of names living in a tool is a list somebody has to edit the tool to change.

| Written on the entry, variant or mix | Means |
|---|---|
| `"art": "<lump>"` | Use this instead of what would be resolved |
| `"art": "<file.png>"` | A picture supplied beside the JSON |
| `"art": ""` | No picture. Draw the name |
| absent | Resolve automatically |

A value with an extension is a file, since lump names have none.

A variant speaks for itself, or inherits what its entry said.

Reach for it only when the source is not what it claims, and say in the commit what was wrong with
it. Three shapes seen so far:

- A logo lump holding an empty frame, with the real art on the title screen.
- A menu drawing the **base game's** logo. It is real art and it is what the menu shows, but it
  names the base game rather than the pack, so every variant comes out as the same generic picture
  where their names had told you which was which.
- A menu graphic that is a blank spacer, with the real title rendered as a MAP rather than stored as
  a picture. Nothing can be extracted from a rendered scene, so either the pack's badge elsewhere in
  its own files, or a supplied picture, is the only thing to point at.

It is not for taste.

A supplied picture goes through the same slot, the same budget and the same treatment as an
extracted one. That is the point of routing it through here rather than dropping a finished image
into the folder: one path, one look, and a regeneration cannot quietly leave it behind.

## When to regenerate

- A new entry, variant or mix. Generate art for it.
- A file swapped for a newer release. Regenerate, since the art may have changed with it.
- The panel's dimensions changed. Regenerate everything at the new slot size.

Something with no art is not a failure. The text header is a designed fallback, not a degraded one,
and a bad picture is worse than the name it replaced.
