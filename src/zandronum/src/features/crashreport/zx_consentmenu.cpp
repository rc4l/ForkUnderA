// [rc4l] Custom drawer for the crash-consent OptionMenu (menudef "CrashConsentMenu"). The prompt
// is shown on the launch after a crash, on top of whatever screen was up (title/help), where its
// plain text collided with the background and was unreadable. This subclass measures the prompt's
// text extents, paints a solid, rounded, vertically-gradient panel sized to wrap them with padding
// and centred on screen, draws the title itself, then defers to the stock DOptionMenu::Drawer for
// the (centred) items. All items in this menu are centred (FOptionMenuItemSubmenu/StaticText pass
// center=true), so the panel -- centred on screen -- contains them symmetrically. The geometry +
// gradient math is the pure, unit-tested zx::*Consent* helper; this file only feeds it metrics.
//
// Registered unconditionally (menudef always parses `Class`, independent of the sentry gate).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "menu/menu.h"
#include "v_video.h"
#include "v_text.h"
#include "v_font.h"
#include "v_palette.h"
#include "gstrings.h"
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

	const char *title = mDesc->mTitle;
	if (title != NULL && title[0] == '$')
		title = GStrings(title + 1);
	const bool hasTitle = (BigFont != NULL && title != NULL && title[0] != '\0');
	const int titleW = hasTitle ? BigFont->StringWidth(title) * cx : 0;
	const int titleH = hasTitle ? BigFont->GetHeight() * cy : 0;

	const int lineH = OptionSettings.mLinespacing * cy;
	const int nItems = (int)mDesc->mItems.Size();

	// Widest rendered line (items draw in SmallFont, centred on screen). Drives the panel width.
	int bodyW = 0;
	for (int i = 0; i < nItems; ++i)
	{
		const char *lbl = mDesc->mItems[i]->GetLabel();
		if (lbl != NULL && lbl[0] == '$')
			lbl = GStrings(lbl + 1);
		if (lbl != NULL && lbl[0] != '\0')
		{
			int w = SmallFont->StringWidth(lbl) * cx;
			if (w > bodyW)
				bodyW = w;
		}
	}
	const int contentW = titleW > bodyW ? titleW : bodyW;

	const int gap = hasTitle ? (lineH / 2) : 0;      // breathing room under the title
	const int contentH = titleH + gap + nItems * lineH;

	const int padX = 26 * cx;                         // includes room for the selection arrow
	const int padY = 18 * cy;
	const int panelW = contentW + 2 * padX;
	const int panelH = contentH + 2 * padY;

	const int panelTop = zx::ComputeCenteredTop(screen->GetHeight(), panelH);
	const int contentTop = panelTop + padY;
	const int contentBottom = contentTop + contentH;

	zx::ConsentPanelRect r = zx::ComputeConsentPanelRect(
		screen->GetWidth(), screen->GetHeight(), panelW, contentTop, contentBottom, padY, 14 * cy);

	// Dark, near-opaque vertical gradient (a touch lighter at the top for depth).
	const zx::ConsentPanelColor top = { 26, 28, 40, 236 };
	const zx::ConsentPanelColor bot = { 8, 9, 15, 248 };
	for (int row = 0; row < r.h; ++row)
	{
		int inset = zx::ComputeRoundedInset(row, r.h, r.radius);
		int rowW = r.w - 2 * inset;
		if (rowW <= 0)
			continue;
		zx::ConsentPanelColor c = zx::ComputeConsentGradient(row, r.h, top, bot);
		screen->Dim(PalEntry(c.r, c.g, c.b), c.a / 255.f, r.x + inset, r.y + row, rowW, 1);
	}

	// Draw the title ourselves, centred at the top of the panel; the stock drawer skips its own
	// title because we set mPosition > 0 below (its title path only runs when mPosition <= 0).
	if (hasTitle)
	{
		screen->DrawText(BigFont, OptionSettings.mTitleColor,
			(screen->GetWidth() - titleW) / 2, contentTop,
			title, DTA_CleanNoMove_1, true, TAG_DONE);
	}

	// Position the items just under the title and let the stock drawer render them (centred).
	mDesc->mPosition = (contentTop + titleH + gap) / cy;
	DOptionMenu::Drawer();
}
