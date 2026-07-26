// [rc4l] Custom drawer for the crash-consent OptionMenu (menudef "CrashConsentMenu"). The prompt
// is shown on the launch after a crash, on top of whatever screen was up (title/help), where its
// plain text collided with the background and was unreadable. This subclass paints a solid,
// rounded, vertically-gradient panel behind the menu so the text always reads, then defers to the
// stock DOptionMenu::Drawer for the title/items. The geometry + gradient math is the pure,
// unit-tested zx::*Consent* helper; this file only feeds it screen/font metrics and issues Dim().
//
// Registered unconditionally (menudef always parses `Class`, independent of the sentry gate).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "menu/menu.h"
#include "v_video.h"
#include "v_font.h"
#include "v_palette.h"
#include "features/crashreport/computation/consent_panel_compute.h"

class DCrashConsentMenu : public DOptionMenu
{
	DECLARE_CLASS(DCrashConsentMenu, DOptionMenu)
public:
	void Drawer();
};

IMPLEMENT_CLASS(DCrashConsentMenu)

void DCrashConsentMenu::Drawer()
{
	const int cx = CleanXfac_1;
	const int cy = CleanYfac_1;

	// Mirror DOptionMenu::Drawer's vertical layout so the panel wraps exactly what it will draw.
	int pos = mDesc->mPosition;
	int firstY;
	if (pos <= 0)
		firstY = (BigFont != NULL && mDesc->mTitle.IsNotEmpty()) ? (-pos + BigFont->GetHeight()) : -pos;
	else
		firstY = pos;
	int itemsTop = firstY * cy;
	int fontHeight = OptionSettings.mLinespacing * cy;
	int itemsBottom = itemsTop + (int)mDesc->mItems.Size() * fontHeight;

	// The title (when present) is drawn near the top at 10*cy in BigFont; start the panel above it.
	bool hasTitle = (pos <= 0 && BigFont != NULL && mDesc->mTitle.IsNotEmpty());
	int contentTop = hasTitle ? (8 * cy) : itemsTop;

	// Width: the clean text area (all centred SmallFont lines fit inside 320 virtual units), lightly
	// inset, so the panel is guaranteed to cover the text and stays centred on any aspect ratio.
	int panelW = 312 * cx;

	zx::ConsentPanelRect r = zx::ComputeConsentPanelRect(
		screen->GetWidth(), screen->GetHeight(), panelW,
		contentTop, itemsBottom, 12 * cy, 12 * cy);

	// Dark, near-opaque vertical gradient (a touch lighter at the top for depth).
	zx::ConsentPanelColor top = { 26, 28, 40, 236 };
	zx::ConsentPanelColor bot = { 8, 9, 15, 248 };

	for (int row = 0; row < r.h; ++row)
	{
		int inset = zx::ComputeRoundedInset(row, r.h, r.radius);
		int rowW = r.w - 2 * inset;
		if (rowW <= 0)
			continue;
		zx::ConsentPanelColor c = zx::ComputeConsentGradient(row, r.h, top, bot);
		screen->Dim(PalEntry(c.r, c.g, c.b), c.a / 255.f, r.x + inset, r.y + row, rowW, 1);
	}

	DOptionMenu::Drawer();
}
