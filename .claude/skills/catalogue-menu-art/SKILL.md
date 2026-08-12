---
name: catalogue-menu-art
description: Generating the small menu-art thumbnails a catalogue entry, variant or mix shows in place of its text header, using menuart.py. Use when adding or changing anything the catalogue loads, when a header should show art instead of a name, or when the art looks wrong.
---

# Menu art for the catalogue

An entry, variant or mix can show a picture instead of a text header. The picture comes out of the
archives it loads, shrunk to a kilobyte and committed. **The archives are not shipped; the art is.**

`menuart.py`, beside this file, does the extraction. It has no dependency beyond Pillow.

```
python menuart.py <archive> [<archive> ...] -o art.png
```

Archives go in **load order**. The tool applies the engine's own rule, so it returns what a player
would actually see.

## What it picks

| Order | Lump | Why |
|---|---|---|
| 1 | `M_DOOM` | A logo, drawn to be read small on a dark background. Survives the shrink. |
| 2 | `TITLEPIC` | A whole illustration. Recognisable at this size but its fine text is not. |
| - | neither | No file is written. The caller falls back to the text header. |

Exit code 0 means art was written, 1 means there was none, 2 means it could not reach the budget.
Use `--json` when a script needs the details.

## The size is fixed first, the budget second

This order matters and is the whole design:

1. Fit the slot, aspect kept. The slot is a UI decision and is never traded away.
2. Drop colours until the PNG fits the budget.

Reversing it produces an image smaller than its slot, which then gets stretched back and looks
worse than a posterised one of the right size. Resolution is what the eye reads; colour count is
what it forgives.

Defaults are `--height 36 --width 252 --budget 1024`, sized to the panel the art appears in. If the
panel changes, change these; do not compensate elsewhere.

## Choosing a slot height

Measured against the header text it replaces, at 1x, 2x and 3x that text's height:

| Slot | Result |
|---|---|
| 1x text height | Unreadable at any colour depth |
| 2x | Logos read; full-screen art does not |
| 3x | Everything legible, everything inside 1 KB |
| 4x | Wide logos break the budget and fall to 4 colours |

3x is the setting for a reason. Going bigger costs legibility, because the budget is then spent on
colour reduction instead of pixels.

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

## Verify by looking

Byte counts prove nothing about whether a picture decoded correctly. A wrong palette or a
misparsed picture produces a plausibly sized file of garbage. **Open the PNG.** Scale it up if you
must, but look at it before committing it.

## When to regenerate

- A new entry, variant or mix. Generate art for it.
- A file swapped for a newer release. Regenerate, since the art may have changed with it.
- The panel's dimensions changed. Regenerate everything at the new slot size.

Something with no art is not a failure. The text header is a designed fallback, not a degraded one,
and a bad picture is worse than the name it replaced.
