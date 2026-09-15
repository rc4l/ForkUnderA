---
name: menu-scrolling
description: Scrolling regions in a custom-drawn menu — measuring content height, scrollbars, clipping that does not reach text, and avoiding layout shift. Use whenever a panel region can hold more than it shows, or when content jumps, clips wrongly, or scrolls past its own end.
---

# Scrolling regions in a custom-drawn menu

A scrolling region is a viewport, a content height, and an offset. Getting any of the three from a
different source than the draw is how a region scrolls past its own end.

## Measure what will actually be drawn

The content height must come from the **same helpers the draw uses**. If the draw wraps a list
through a layout function, the height must call that function too — not re-derive the line count
with its own arithmetic.

Two measurements of one layout is exactly how a region ends up able to scroll past its content, or
to stop short of it. When a helper takes a width, check both callers pass the **same** one: a
panel's left edge and a column's left edge are different numbers, and measuring against the wrong
one gives a line count the draw does not agree with.

## Clipping does not reach text

The clip helper masks fills, **not** text. A row half past the boundary therefore has its box cut
and its label drawn whole — bright letters hanging below a region that has visibly ended.

So a region masks its own text by **skipping**: draw the row's box either way (a sliver of a row is
what says the list goes on), and draw its text only when the row fits entirely.

```cpp
if ( !RowIntersectsView( y, h ) ) continue;   // not drawn at all
const bool bWhole = RowFullyVisible( y, h );  // text waits for this
```

Both tests belong to the region, and the hit test must use the same rule as the draw or something
unclickable will look clickable.

## Scrollbars

Draw one only when the content exceeds the view — a region that fits needs no bar, and one that
appears and disappears is noise. Geometry comes from a shared compute unit used by **both** the draw
and the drag hit test; the two working it out separately is how clicking a bar comes to jump
somewhere the thumb is not.

Two bars in one panel should end on the same line. Different bottoms read as one of them being cut
short, and the eye is right — usually one region really has been given less room than its neighbour
for no reason.

## Layout shift is a bug, not a cosmetic

If picking a setting changes how tall something **above** the controls is, everything below moves
while the pointer is still over the control that was just clicked. The next click lands on a
different setting.

Three ways out, in order of preference:

1. **Reorder.** Put the controls above the thing that grows. Nothing the growing thing does can push
   them, no space is reserved, and the controls sit at the top of the scroll region where they are
   reachable without scrolling at all. This is almost always the right answer.
2. **Reserve** the maximum the region can reach — but only if you can compute that maximum, and know
   the cost: blank lines on every draw, in exchange for a shift that happens only in the moment
   after a click.
3. Accept the shift. Rarely right for anything interactive.

If you do reserve, spend the slack **after** the block's last line rather than in the middle of it,
so a summary hugs the thing it summarises and the gap reads as section spacing.

## Affordances that do not survive contact

An edge shadow or fade to say "there is more this way" is appealing and needs the region to actually
stop at its boundary. Where text is drawn outside the clip (see above), the shadow covers the
boundary while the spilt text sits below all of it, which looks worse than no shadow. Fix the
spilling first or do not add the shadow.

Keep the affordances that work: the scrollbar, a half-cut row at the edge, and content that visibly
continues.

## The window follows the selection ONLY when the selection moved

Deriving the visible window from the selected row is right, and doing it on **every frame** is the
bug it turns into. "Keep the selection visible" is satisfied by the window it already had, so any
scroll that does not also select — the wheel, a scrollbar drag — is undone before the next frame
draws. Both inputs appear completely dead, and the scrolling code looks correct in isolation.

Keep a flag, set it where the selection changes, and consult it in the draw:

```cpp
if ( mReveal ) { mFirst = ComputeRowWindow( total, rows, mSelected, mFirst ).first; mReveal = false; }
mFirst = ComputeRestoredScroll( mFirst, total, rows );   // clamped every frame, revealed only on demand
```

The clamp still runs unconditionally — content comes and goes while a menu is open — but the reveal
does not. Taking a working menu's scrolling maths without this flag is how a list ships unscrollable.

## A drawn scrollbar is not a scrollbar

Drawing the bar and wiring the grab are two jobs, and the first one looks finished. Test the bar by
dragging it before believing it works; a thumb that renders and moves with the keyboard proves
nothing about the pointer.

The bar must be hit-tested **before** the rows, or a row's box swallows the clicks meant for the
thumb. Give it a few pixels of slack either side — a four-pixel target is a target nobody hits — and
keep tracking the pointer once the button is down even when it wanders off the bar, which is what
dragging means everywhere else.

## Scroll state

- **Reset the offset when the selection changes.** A short panel left scrolled from a taller one
  draws past its own content, and since hit rects are registered by the draw, nothing is drawn and
  therefore nothing is clickable — a control that appears fine and ignores every click.
- Clamp the offset whenever the content height changes, not only on input.
- Regions scroll independently. Sharing one offset between a list that grows with content and a form
  that never does drags the form off screen as the list fills.
- Mouse wheel and keyboard both move it; the keyboard should also scroll a focused control into view
  rather than letting focus land somewhere invisible.
