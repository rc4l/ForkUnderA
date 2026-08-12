---
name: menu-widgets
description: Building a reusable control in a custom-drawn menu — sliders, pills, choice rows and their hit rects, disabled states, and when to pick one shape over another. Use whenever adding a setting to a panel, or when a control needs a disabled, locked or read-only state.
---

# Reusable controls in a custom-drawn menu

A control here is a **function that draws and registers itself**, keyed by an id. Write it once and
call it; a second one is a call, not a copy.

```cpp
int DrawSlider( const char *id, const char *label, int x, int y, int labelW,
                int minV, int maxV, int value, const char *valueText, const char *tip );
```

Returning the y below what it drew is what lets a panel stack controls without anybody computing
positions twice.

## Pick the shape from the data

| data | control |
|---|---|
| two or three named options | **pills** — one row, all visible |
| many options, no order | pills that **wrap** — still all visible |
| an ordered range, more than a handful | **slider** with step buttons at the ends |
| a number with a natural unit | slider, with the value NAMED not numbered |

A slider is right when the thing being chosen has an **order** — that is what a track shows and a
row of chips does not. Pills are right when the options are peers.

Two rules learned the hard way:

- **Do not hide options behind a modal or a "more" button.** People do not click what they cannot
  see, so a hidden setting is a setting that does not exist for most players. Defaults matter most
  precisely because of this.
- **Wrap rather than degrade.** A row of pills too wide for the column should break onto another
  line, not fall back to a list. Everything stays visible and four options cost two lines instead of
  four. Wrap through the same layout helper the rest of the panel uses, so one place decides how a
  row of measured things becomes lines.

## Value text, not raw numbers

The control takes the string to show, because what a value MEANS is the caller's business — zero of
one thing is "Unlimited" and zero of another is "Off". Measure the value column against the **widest
string it could ever show**, not the current one, or the track resizes as the value changes and the
knob appears to move twice.

Where the stops are not evenly spaced, the slider moves an **index** and a compute unit owns the
mapping in both directions. The control stays linear; the meaning stays testable.

## Hit rects come from the draw

Register a control's clickable rectangle **as it draws**, into a list cleared every frame.

```cpp
g_Rects.Clear();          // at the top of the panel draw
...
g_Rects.Push( rect );     // inside each control's draw
```

This gives three things for free: a control scrolled out of view registers nothing and is correctly
unclickable; a control that is not drawn cannot be clicked; and the hit test can never disagree with
the drawing about where something is.

It also means **"nothing happened when I clicked"** is usually "nothing drew". Check that first.

Test step buttons **before** the track: they sit at the track's ends and a generous track hitbox
otherwise swallows them, which is exactly what makes a button look decorative.

Dragging needs the id captured on press and kept until release, tracked even when the pointer leaves
the row — that is what makes a drag feel like one — and dropped when the control stops being drawn.

## Disabled looks different in KIND, not in brightness

A dim version of an ordinary control reads as *an option you have not hovered yet*, not as *not
available*. Push it nearly to the panel's own colour, flat, with the label in a dark grey, and drop
whatever says "live" — a glow, a halo, a highlight.

Three further rules:

- **Do not hide a control that cannot be used.** The row is the only place that can say WHY, and a
  control that vanishes reads as a panel that forgot it. A slider whose ceiling equals its floor is
  inert through its own end-stop rules and needs no special case.
- **Dim the whole row.** A bright readout beside two greyed steps and a dead track claims the value
  is still yours to change when nothing else on the row does.
- **Only disable what is actually excluded.** When one setting locks another, the *compatible*
  option in the locked group stays lit and pressable — greying it out says the whole axis is
  unavailable when what is unavailable is changing away from it.

A locked control registers **no hit rect**. That is what makes it unpressable in every direction at
once — click, hover and keyboard — rather than three separate checks that can disagree.

## Mutually exclusive settings

When two controls cannot both be off their default, enforce it **symmetrically** and in a pure unit:
whichever is already off its default locks the other, so there is no order to remember. Decide the
tie deliberately and write down which one wins and why.

Apply the forced value at the **one place that answers "what is in force"**, not at the draw. Every
consumer — the file list, the plan, the config key, the thing that actually launches — comes through
that place, and a lock only the pills knew about will show one thing while doing another. Never
write the forced value back over the player's stored choice; it should come back when the lock
lifts.

## Layout

- Measure a shared label column across **every** control that uses it, or one wider label overlaps
  its own control while the rest keep a gap in front of them.
- The label can share the control's row. That is a line back per setting, in a column that has none
  to spare.
- One exception is allowed when a label is wider than the column can carry: give that row a header
  line and leave the shared column sized to the labels that fit. Do not size the column to the
  outlier — every other control then indents to line up with nothing.
- Pills may break the shared column deliberately: they wrap over many rows, so every pixel of indent
  is charged to all of them. Sliders keep the column because they are one control repeated and a
  ragged left edge on three of those reads as a mistake.
- Put the control people came for **first**. The setting with the most answers, and the one whose
  row count grows as content is added, does not belong at the bottom of a scrolling region.
