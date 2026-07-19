// [rc4l] Implementation of the pure freeform-menu layout math. No engine dependencies, so
// both the engine and the standalone test build compile this TU.
#include "features/freeform-menu/computation/freeform_layout_compute.h"

namespace
{
// [rc4l] Mirror of the engine's SCALEY macro: truncate f * scaleFac to an int.
int ScaleY(int f, float scaleFac)
{
	return (int)(f * scaleFac);
}
}

ZxScreenPos ComputeAnchoredPosition(int xpos, int ypos, int width, int height,
	int gravity, int anchor, int xPadding, int yPadding, bool withPadding,
	float scaleFac, int screenWidth, int screenHeight)
{
	ZxScreenPos pos;
	pos.x = ScaleY(xpos, scaleFac);
	pos.y = ScaleY(ypos, scaleFac);

	// Adjust for gravity (positions relative to a screen edge/center, unscaled screen size).
	if (gravity & ZX_GRAV_RIGHT)
		pos.x += screenWidth;
	else if (gravity & ZX_GRAV_CENTER_HORIZONTAL)
		pos.x += screenWidth / 2;

	if (gravity & ZX_GRAV_BOTTOM)
		pos.y += screenHeight;
	else if (gravity & ZX_GRAV_CENTER_VERTICAL)
		pos.y += screenHeight / 2;

	// Adjust for anchor (which edge/center of the item sits on the gravity point).
	if (anchor & ZX_GRAV_RIGHT)
		pos.x -= ScaleY(width, scaleFac);
	else if (anchor & ZX_GRAV_CENTER_HORIZONTAL)
		pos.x -= ScaleY(width, scaleFac) / 2;

	if (anchor & ZX_GRAV_BOTTOM)
		pos.y -= ScaleY(height, scaleFac);
	else if (anchor & ZX_GRAV_CENTER_VERTICAL)
		pos.y -= ScaleY(height, scaleFac) / 2;

	// Adjust for padding.
	if (withPadding)
	{
		pos.x += ScaleY(xPadding, scaleFac);
		pos.y += ScaleY(yPadding, scaleFac);
	}

	return pos;
}

ZxScreenOffset ComputeForegroundAnchorOffset(int anchor, int foregroundWidth,
	int foregroundHeight, int width, int height, float scaleFac)
{
	ZxScreenOffset offset;
	offset.dx = 0;
	offset.dy = 0;

	if (anchor & ZX_GRAV_LEFT)
		offset.dx = -ScaleY(foregroundWidth - width, scaleFac) / 2;
	else if (anchor & ZX_GRAV_RIGHT)
		offset.dx = ScaleY(foregroundWidth - width, scaleFac) / 2;

	if (anchor & ZX_GRAV_TOP)
		offset.dy = -ScaleY(foregroundHeight - height, scaleFac) / 2;
	else if (anchor & ZX_GRAV_BOTTOM)
		offset.dy = ScaleY(foregroundHeight - height, scaleFac) / 2;

	return offset;
}

ZxScrollBounds ComputeScrollBounds(int lowestScroll, int pagesize, bool center)
{
	ZxScrollBounds bounds;

	if (lowestScroll < pagesize)
	{
		// Everything fits: nothing to scroll, optionally center the short page.
		bounds.centeredOffset = center ? (pagesize - lowestScroll) / 2 : 0;
		bounds.lowestScroll = 0;
	}
	else
	{
		bounds.centeredOffset = 0;
		bounds.lowestScroll = lowestScroll - pagesize;
	}

	return bounds;
}

int ComputeClampScroll(int scrollPos, int lowestScroll)
{
	if (scrollPos < 0)
		return 0;
	if (scrollPos > lowestScroll)
		return lowestScroll;
	return scrollPos;
}

bool ComputePointInRect(int px, int py, int left, int top, int width, int height)
{
	return px >= left && px < left + width
		&& py >= top  && py < top + height;
}
