// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/pointerdrag_compute.h"

namespace zx
{

DragOutcome StepDrag(bool dragging, PointerEvent event)
{
	DragOutcome out;

	switch (event)
	{
	case PointerEvent::Press:
		// [rc4l] Always ends the previous gesture, and is never consumed by it. This is the whole
		// point of the unit: a stale flag must cost a frame, not a click.
		out.dragging = false;
		out.consumed = false;
		break;

	case PointerEvent::Move:
		// Only meaningful while something is being dragged. A move with no gesture behind it belongs
		// to hover, which is somebody else's business.
		out.dragging = dragging;
		out.consumed = dragging;
		break;

	case PointerEvent::Release:
		// Ends the gesture, and is consumed only if there was one -- otherwise a release nobody
		// asked for would be swallowed before the control under it could see it.
		out.dragging = false;
		out.consumed = dragging;
		break;
	}

	return out;
}

bool BeginDrag()
{
	return true;
}

} // namespace zx
