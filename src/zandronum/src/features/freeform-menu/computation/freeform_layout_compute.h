// [rc4l] Pure freeform-menu layout math, extracted from freeformmenu.cpp so it can be
// unit-tested without linking the engine. Covers gravity/anchor screen positioning, the
// scroll-range/centering computation, scroll clamping and mouse hit-testing. The screen
// scale factor and screen size come in as plain args (the engine's SCALEY macro is
// ScaleFac multiplication). Gravity bits mirror EGravity in menu/menu.h; the caller
// static_asserts they stay in sync. Implementation in freeform_layout_compute.cpp.
#ifndef ZX_FREEFORM_LAYOUT_COMPUTE_H
#define ZX_FREEFORM_LAYOUT_COMPUTE_H

// [rc4l] Gravity/anchor bits, mirroring EGravity in menu/menu.h.
enum
{
	ZX_GRAV_CENTER_HORIZONTAL = 1,
	ZX_GRAV_CENTER_VERTICAL   = 2,
	ZX_GRAV_LEFT              = 4,
	ZX_GRAV_RIGHT             = 8,
	ZX_GRAV_TOP               = 16,
	ZX_GRAV_BOTTOM            = 32,
};

// [rc4l] A scaled on-screen position in pixels.
struct ZxScreenPos
{
	int x;
	int y;
};

// [rc4l] A scaled on-screen offset in pixels.
struct ZxScreenOffset
{
	int dx;
	int dy;
};

// [rc4l] Resolve an item's top-left draw position from its logical position, size, gravity
// and anchor, applying the screen scale factor exactly as the engine's SCALEY macro does.
ZxScreenPos ComputeAnchoredPosition(int xpos, int ypos, int width, int height,
	int gravity, int anchor, int xPadding, int yPadding, bool withPadding,
	float scaleFac, int screenWidth, int screenHeight);

// [rc4l] Extra offset that recenters a selected foreground texture over its item when the
// anchor pins one edge, so a larger/smaller highlight stays centered on the item.
ZxScreenOffset ComputeForegroundAnchorOffset(int anchor, int foregroundWidth,
	int foregroundHeight, int width, int height, float scaleFac);

// [rc4l] The scrollable range and centering offset for a page.
struct ZxScrollBounds
{
	int lowestScroll;   // [rc4l] max scroll position; 0 when everything fits
	int centeredOffset; // [rc4l] vertical offset that centers a short page (0 otherwise)
};

// [rc4l] Given the lowest item extent, the visible page size and whether the menu centers
// short pages, compute the scroll range and centering offset.
ZxScrollBounds ComputeScrollBounds(int lowestScroll, int pagesize, bool center);

// [rc4l] Clamp a scroll position into the valid [0, lowestScroll] range.
int ComputeClampScroll(int scrollPos, int lowestScroll);

// [rc4l] True when a point lies inside the [left, left+width) x [top, top+height) rectangle.
bool ComputePointInRect(int px, int py, int left, int top, int width, int height);

#endif // ZX_FREEFORM_LAYOUT_COMPUTE_H
