---
name: menu-keyboard-focus
description: Adding keyboard navigation to a custom-drawn menu region — one focus position, a pure nav unit, the movement/traversal split, and keeping the focus glow anchored. Use whenever a new control, row or panel needs to be reachable by arrow keys, or when arrow keys behave inconsistently between regions.
---

# Keyboard navigation in a custom-drawn menu

Our menus draw themselves and therefore route their own keys. Every screen that does this has the
same shape, and departing from it is where the bugs come from.

## One position, never several booleans

**A region's focus is one value.** Not an int for the field plus a bool for the row plus a bool for
the button.

Three variables for one position is an invariant nobody enforces: at most one may be set. Every
place that moves focus then has to remember to clear the others, and eventually one does not. The
symptoms are always the same pair — two things glowing at once, and a key that does nothing because
an earlier test matched first and returned.

```cpp
enum class ThingSlot { List, Field, Choice, Action, ..., Away };
struct ThingFocusPos { ThingSlot slot; int index; };   // index means something only for some slots
```

`Away` is not a slot on the panel: it is how the unit says *hand the arrows back to whoever owns
the region above me*.

## The decision is a pure unit

Put the map in `features/<name>/computation/<x>focus_compute.{h,cpp}` with a test per rule, and
write the traversal diagram in the header comment. The diagram is the thing people read; keep it
small enough to hold in your head.

```cpp
ThingNavResult ComputeThingNav(ThingFocusPos pos, ThingNavKey key, <what exists>);
ThingFocusPos  ClampThingFocus(ThingFocusPos pos, <what exists>);
```

**Movement and traversal are separate answers.** A key that moves a selection does not change
focus, and a key that changes focus does not move a selection. Return both from one call:

```cpp
struct ThingNavResult {
    ThingFocusPos pos;   // where focus ends up, unchanged when the key did something else
    int rowStep;         // -1 / +1 / 0
    int choiceStep;
    bool caret;          // the key belongs to a text caret; hand it to the field
};
```

Returning only one of them is what makes a caller invent its own rule for the overlap, and two
callers will invent two.

## What exists is an argument, not an assumption

Pass in what is currently on screen — how many fields, whether the settings are open, whether a
second button is beside the first. A focus that can land on something not drawn is the
invisible-but-reachable bug in its keyboard form.

`Clamp` exists because a panel can close underneath a focus that was legitimate when it was set.
Call it before reading the position, not only when moving.

## Rules that are easy to get wrong

- **An empty region is not enterable.** Down into a list with no rows must not move focus there;
  everything downstream reads a selection that would not exist.
- **Left and right belong to the caret inside a text field.** A field that jumped to another control
  when you tried to move through what you typed is unusable. The field decides *when* it finally
  gives the key back (it is the only thing that knows the caret has nowhere left to go); the focus
  unit decides *where* that lands, so the destination is written once.
- **A control that only a pointer can reach is unreachable** for anyone on a keyboard or a pad. If
  you draw it, give it a slot.
- Direction should mean something spatially. If a thing is below, `Down` gets there.

## The glow follows focus, it does not lead it

The focus marker is anchored, not positioned: each control that is currently focused calls

```cpp
FocusAnchor( <owner>, centreX, centreY );
```

**while it draws**, and the marker travels to the last anchor requested this frame. That single
indirection is what makes crossing from a row to a button slide instead of jump, and it means a
control that scrolled out of view stops anchoring and the glow does not point at nothing.

Three consequences worth knowing:

- Anchor from the **draw**, not from the key handler. Anchoring where you moved to means guessing at
  geometry the draw already knows.
- A region that does not own the arrows must not anchor. Two markers on screen is one of them lying.
- The marker is shared with the chrome above the panel. If an outer bar has the arrows, inner
  regions stay dark.

## Mouse and keyboard must agree

Anything clickable is also a focus destination, and clicking it **sets focus there**. Otherwise the
next arrow key jumps somewhere unrelated — the player's mental cursor is where they clicked.

The reverse holds too: hit rects are registered by the **draw**, so a control that was not drawn
this frame cannot be clicked. That is usually right (a scrolled-away row should not be clickable)
and is worth remembering when a click seems to do nothing — check whether the thing drew at all
before suspecting the hit test.

Hover and focus are different states and must look different. Hover follows the pointer; focus is
where the keyboard is. If hover is "brighter" and focus is also "brighter", the pointer impersonates
the cursor. Give focus a mark of a different KIND.

## Verify

Drive it with the keyboard only, from a cold start, and try to reach every control. Then click each
control and press an arrow, and check the next thing focused is the neighbour of the thing you
clicked. The two halves are separately easy and jointly the whole feature.
