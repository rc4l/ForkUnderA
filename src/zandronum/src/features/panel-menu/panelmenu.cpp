// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] DFUAPanelListMenu -- see features/panel-menu/README.md.
//
// Lives here rather than in menu/listmenu.cpp because it is entirely ours: nothing upstream draws a
// panel behind menu items, so none of this merges with anything on a re-sync. What HAS to stay in
// the vendored tree is only the handful of accessors on FListMenuItem it reads (GetDrawnX/GetDrawnY,
// and StaticPatch's GetWidth), enumerated in the feature README.

#include "v_video.h"
#include "v_font.h"
#include "v_palette.h"
#include "gi.h"
#include "menu/menu.h"
#include "features/panel-menu/computation/panelmenu_compute.h"
#include "features/updater/computation/promptpanel_compute.h"


//=============================================================================
//
// [rc4l] DFUAPanelListMenu -- a ListMenu that draws the rounded gradient panel behind its own
// content, so a menu reads as a card floating over the title screen instead of loose art and text
// on a busy background. Same visual language as the updater's "update available" chip and the
// open-link dialog, reusing their tested geometry/gradient math.
//
// Opt in from menudef with `Class "FUAPanelListMenu"`. The panel is measured from the descriptor's
// own items, so a menu can add, remove or reposition rows -- and each game can use its own logo and
// coordinates -- without touching this code.
//
//=============================================================================

class DFUAPanelListMenu : public DListMenu
{
	DECLARE_CLASS(DFUAPanelListMenu, DListMenu)
public:
	void Drawer();
};
IMPLEMENT_CLASS(DFUAPanelListMenu)


void DFUAPanelListMenu::Drawer()
{
	// Content extent in the 320x200 virtual page the items are drawn in (DTA_Clean). The decision
	// itself is in features/panel-menu/computation so it can be tested off-engine; this loop only
	// flattens the item list into the values it wants.
	TArray<zx::MenuItemBox> boxes;
	for (unsigned i = 0; i < mDesc->mItems.Size(); ++i)
	{
		FListMenuItem *item = mDesc->mItems[i];
		if (!item->mEnabled)
			continue;
		zx::MenuItemBox b;
		b.x = item->GetDrawnX();
		b.y = item->GetDrawnY();
		b.w = item->GetWidth();
		b.h = item->GetDrawnHeight();
		boxes.Push(b);
	}

	const zx::MenuExtent ext = zx::ComputeListMenuExtent(
		boxes.Size() ? &boxes[0] : NULL, (int)boxes.Size(), mDesc->mSelectOfsX, mDesc->mLinespacing);

	// Nothing measurable (a menu of items that all report width 0) -- draw it unpanelled rather
	// than guess at a rectangle.
	if (!ext.valid)
	{
		Super::Drawer();
		return;
	}
	const int vLeft = ext.left, vRight = ext.right, vTop = ext.top, vBottom = ext.bottom;

	const int padV = 8;					// virtual px of breathing room above the content
	// [rc4l] The bottom gets a full row of clearance rather than padV, so the block of rows reads as
	// sitting ON something instead of resting on the panel's edge. Measured in the descriptor's own
	// linespacing, so it tracks whatever font and spacing the menu actually uses -- a menu with
	// bigger rows gets a proportionally bigger foot. Falls back to padV for a descriptor that
	// declares no linespacing (nothing in the tree does, but the extent already tolerates it).
	const int padBottomV = mDesc->mLinespacing > 0 ? mDesc->mLinespacing : padV;
	const int cx = CleanXfac, cy = CleanYfac;
	const int sw = screen->GetWidth(), sh = screen->GetHeight();
	// DTA_Clean maps the virtual page onto the screen scaled by Clean*fac and centred, so a virtual
	// coordinate becomes (v - centre) * fac + screenCentre.
	const int topPx    = (vTop    - padV        - 100) * cy + sh / 2;
	const int bottomPx = (vBottom + padBottomV  - 100) * cy + sh / 2;
	// ComputePanelRect centres the panel on screen, so its half-width has to reach the further of
	// the two content edges from the virtual centre -- sizing it to the raw content width would
	// clip whichever side sticks out more.
	const int halfV    = MAX(160 - vLeft, vRight - 160);
	const int panelWpx = (2 * halfV + 2 * padV) * cx;

	// [rc4l] Keep the card off the screen edges no matter how large its content is.
	//
	// The panel is sized from what it contains, and what it contains is not ours: FUANewGameMenu
	// draws StaticPatch "M_DOOM", and a mod is free to replace that lump with anything. MM8BDM's is a
	// full-width banner where Doom's is 159x37, so the computed extent ran past the screen, got
	// clamped to it, and the "card floating over the title screen" became an opaque sheet covering
	// everything.
	//
	// A card has to read as a card, so the margin wins over fitting the content: oversized artwork
	// gets clipped by the panel rather than allowed to inflate it. Anything that big was already
	// going to overflow the 320x200 page it was authored against.
	const zx::PanelBounds bounds = zx::ComputePanelBounds(sw, sh, panelWpx, topPx, bottomPx);

	zx::PanelRect r = zx::ComputePanelRect(sw, sh, bounds.w, bounds.top, bounds.bottom, 0, 6 * cy);
	const zx::PanelColor topCol = { 26, 28, 40, 236 };
	const zx::PanelColor botCol = { 8, 9, 15, 248 };
	for (int row = 0; row < r.h; ++row)
	{
		const int inset = zx::ComputeRoundedInset(row, r.h, r.radius);
		const int rowW = r.w - 2 * inset;
		if (rowW <= 0)
			continue;
		const zx::PanelColor c = zx::ComputePanelGradient(row, r.h, topCol, botCol);
		screen->Dim(PalEntry(c.r, c.g, c.b), c.a / 255.f, r.x + inset, r.y + row, rowW, 1);
	}

	Super::Drawer();					// the logo and rows, on top of the panel
}
