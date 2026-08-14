// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] When a drag owns the pointer, and -- more importantly -- when it stops.
//
// THIS EXISTS BECAUSE THE RULE WAS INLINE AND GOT IT WRONG. Both text fields in the browser tracked
// their own drag with a bool: set on press, cleared on release, and while set they consumed every
// pointer event so a selection could continue past the edge of the box.
//
// That is correct only while the release actually arrives. It does not always -- focus can move by
// keyboard between the press and the release, and the release then goes somewhere else. The flag
// stays set, and the next click is eaten whole: the press is swallowed by a gesture that ended
// minutes ago, and the click that should have chosen something does nothing. The click after it
// works, which makes the whole thing read as a flaky menu rather than a rule with a hole in it.
//
// So the rule is written down here instead, where it can be swept:
//
//   A PRESS ALWAYS ENDS THE PREVIOUS DRAG, AND IS NEVER CONSUMED BY IT.
//
// A press is by definition not part of the gesture before it. Ending the old drag on press costs
// nothing real and turns a lost release into one wasted frame instead of one swallowed click.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_POINTERDRAG_COMPUTE_H
#define ZX_POINTERDRAG_COMPUTE_H

namespace zx
{

enum class PointerEvent
{
	Press,
	Move,
	Release,
};

struct DragOutcome
{
	bool dragging;		// what the caller should store
	bool consumed;		// whether the in-progress drag took this event

	DragOutcome() : dragging(false), consumed(false) {}
};

// Advance a drag by one pointer event.
//
// `consumed` means an in-progress gesture claimed it and the caller must not let anything else act
// on it. A press is never consumed -- see the header.
DragOutcome StepDrag(bool dragging, PointerEvent event);

// A press that landed on something draggable. Separate from StepDrag because only the caller knows
// whether the thing under the pointer is worth dragging, and it decides that AFTER the press has
// ended whatever came before.
bool BeginDrag();

} // namespace zx

#endif // ZX_POINTERDRAG_COMPUTE_H
