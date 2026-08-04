/*
** listmenu.cpp
** A simple menu consisting of a list of items
**
**---------------------------------------------------------------------------
** Copyright 2010 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "v_video.h"
#include "v_font.h"
#include "cmdlib.h"
#include "gstrings.h"
#include "g_level.h"
#include "gi.h"
#include "d_gui.h"
#include "d_event.h"
#include "menu/menu.h"
#include "v_palette.h"                                        // [rc4l] PalEntry for the update notice
#include "features/updater/zx_updater.h"                      // [rc4l] update-available state
#include "features/updater/computation/promptpanel_compute.h" // [rc4l] rounded chip geometry/gradient
#include "features/updater/computation/notice_compute.h"      // [rc4l] tested focus state machine
#include "features/panel-menu/computation/panelmenu_compute.h" // [rc4l] tested list-menu content extent

IMPLEMENT_CLASS(DListMenu)

//=============================================================================
//
//
//
//=============================================================================

DListMenu::DListMenu(DMenu *parent, FListMenuDescriptor *desc)
: DMenu(parent)
{
	mDesc = NULL;
	// [rc4l] Update-notice state; inert unless NoticeApplies(). See menu.h for why it lives here.
	mNoticeFocused = false;
	mNoticePrevSelected = 0;
	mNoticeLastMouseX = INT_MIN;
	mNoticeLastMouseY = INT_MIN;
	mNoticeL = mNoticeT = mNoticeR = mNoticeB = 0;
	if (desc != NULL) Init(parent, desc);
}

//=============================================================================
//
//
//
//=============================================================================

void DListMenu::Init(DMenu *parent, FListMenuDescriptor *desc)
{
	mParentMenu = parent;
	GC::WriteBarrier(this, parent);
	mDesc = desc;
	if (desc->mCenter)
	{
		int center = 160;
		for(unsigned i=0;i<mDesc->mItems.Size(); i++)
		{
			int xpos = mDesc->mItems[i]->GetX();
			int width = mDesc->mItems[i]->GetWidth();
			int curx = mDesc->mSelectOfsX;

			if (width > 0 && mDesc->mItems[i]->Selectable())
			{
				int left = 160 - (width - curx) / 2 - curx;
				if (left < center) center = left;
			}
		}
		for(unsigned i=0;i<mDesc->mItems.Size(); i++)
		{
			int width = mDesc->mItems[i]->GetWidth();

			if (width > 0)
			{
				mDesc->mItems[i]->SetX(center);
			}
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

FListMenuItem *DListMenu::GetItem(FName name)
{
	for(unsigned i=0;i<mDesc->mItems.Size(); i++)
	{
		FName nm = mDesc->mItems[i]->GetAction(NULL);
		if (nm == name) return mDesc->mItems[i];
	}
	return NULL;
}

//=============================================================================
//
//
//
//=============================================================================

bool DListMenu::Responder (event_t *ev)
{
	if (ev->type == EV_GUI_Event)
	{
		if (ev->subtype == EV_GUI_KeyDown)
		{
			int ch = tolower (ev->data1);

			for(unsigned i = mDesc->mSelectedItem + 1; i < mDesc->mItems.Size(); i++)
			{
				if (mDesc->mItems[i]->CheckHotkey(ch))
				{
					mDesc->mSelectedItem = i;
					S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
					return true;
				}
			}
			for(int i = 0; i < mDesc->mSelectedItem; i++)
			{
				if (mDesc->mItems[i]->CheckHotkey(ch))
				{
					mDesc->mSelectedItem = i;
					S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
					return true;
				}
			}
		}
	}
	return Super::Responder(ev);
}

//=============================================================================
//
//
//
//=============================================================================

bool DListMenu::MenuEvent (int mkey, bool fromcontroller)
{
	// [rc4l] The update chip gets first refusal on the main menu; everywhere else this is one
	// name compare. `handled` false means it declined and the list below owns the key.
	{
		bool noticeHandled = false;
		const bool r = NoticeMenuEvent(mkey, fromcontroller, noticeHandled);
		if (noticeHandled)
			return r;
	}

	int startedAt = mDesc->mSelectedItem;

	switch (mkey)
	{
	case MKEY_Up:
		do
		{
			if (--mDesc->mSelectedItem < 0) mDesc->mSelectedItem = mDesc->mItems.Size()-1;
		}
		while (!mDesc->mItems[mDesc->mSelectedItem]->Selectable() && mDesc->mSelectedItem != startedAt);
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
		return true;

	case MKEY_Down:
		do
		{
			if (++mDesc->mSelectedItem >= (int)mDesc->mItems.Size()) mDesc->mSelectedItem = 0;
		}
		while (!mDesc->mItems[mDesc->mSelectedItem]->Selectable() && mDesc->mSelectedItem != startedAt);
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
		return true;

	case MKEY_Enter:
		if (mDesc->mSelectedItem >= 0 && mDesc->mItems[mDesc->mSelectedItem]->Activate())
		{
			S_Sound (CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE);
		}
		return true;

	default:
		return Super::MenuEvent(mkey, fromcontroller);
	}
}

//=============================================================================
//
//
//
//=============================================================================

bool DListMenu::MouseEvent(int type, int x, int y)
{
	// [rc4l] Chip hit-test first, in RAW screen pixels -- it must run before the virtual-coordinate
	// conversion below, because the pill is drawn with DTA_CleanNoMove_1 and its rect is stored in
	// screen space. `handled` false means the pointer is not on the chip and the list owns it.
	{
		bool noticeHandled = false;
		const bool r = NoticeMouseEvent(type, x, y, noticeHandled);
		if (noticeHandled)
			return r;
	}

	int sel = -1;

	// convert x/y from screen to virtual coordinates, according to CleanX/Yfac use in DrawTexture
	x = ((x - (screen->GetWidth() / 2)) / CleanXfac) + 160;
	y = ((y - (screen->GetHeight() / 2)) / CleanYfac) + 100;

	if (mFocusControl != NULL)
	{
		mFocusControl->MouseEvent(type, x, y);
		return true;
	}
	else
	{
		if ((mDesc->mWLeft <= 0 || x > mDesc->mWLeft) &&
			(mDesc->mWRight <= 0 || x < mDesc->mWRight))
		{
			for(unsigned i=0;i<mDesc->mItems.Size(); i++)
			{
				if (mDesc->mItems[i]->CheckCoordinate(x, y))
				{
					if ((int)i != mDesc->mSelectedItem)
					{
						//S_Sound (CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
					}
					mDesc->mSelectedItem = i;
					mDesc->mItems[i]->MouseEvent(type, x, y);
					return true;
				}
			}
		}
	}
	mDesc->mSelectedItem = -1;
	return Super::MouseEvent(type, x, y);
}

//=============================================================================
//
//
//
//=============================================================================

void DListMenu::Ticker ()
{
	Super::Ticker();
	for(unsigned i=0;i<mDesc->mItems.Size(); i++)
	{
		mDesc->mItems[i]->Ticker();
	}
}

//=============================================================================
//
//
//
//=============================================================================

void DListMenu::Drawer ()
{
	for(unsigned i=0;i<mDesc->mItems.Size(); i++)
	{
		if (mDesc->mItems[i]->mEnabled) mDesc->mItems[i]->Drawer(mDesc->mSelectedItem == (int)i);
	}
	if (mDesc->mSelectedItem >= 0 && mDesc->mSelectedItem < (int)mDesc->mItems.Size())
		mDesc->mItems[mDesc->mSelectedItem]->DrawSelector(mDesc->mSelectOfsX, mDesc->mSelectOfsY, mDesc->mSelector);
	NoticeDrawer();   // [rc4l] no-op unless this is the main menu with an update pending
	Super::Drawer();
}

//=============================================================================
//
// [rc4l] The "update available" chip.
//
// Drawn by, and driven from, DListMenu itself rather than a subclass -- see the comment on the
// mNotice* fields in menu.h. Gated entirely on NoticeApplies(), so for every menu that is not the
// main menu, and for the main menu while no update is pending, all of this is a single bool test.
//
// The decision logic (which key does what, whether a mouse position should act) lives in tested
// computation units under features/updater/computation; these functions only apply the result.
//
//=============================================================================

bool DListMenu::NoticeApplies() const
{
	return mDesc != NULL
		&& mDesc->mMenuName == FName( "MainMenu" )
		&& zx::updater::IsAvailable();
}

void DListMenu::NoticeActivate()
{
	S_Sound(CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE);
	M_ConfirmDownloadRelease(zx::updater::Tag());
}

void DListMenu::NoticeFocusChip()
{
	if (!mNoticeFocused)
	{
		if (mDesc->mSelectedItem >= 0)
			mNoticePrevSelected = mDesc->mSelectedItem; // remember where we were, to restore on exit
		mNoticeFocused = true;
		mDesc->mSelectedItem = -1;                      // chip owns the selection; nothing in the list
	}
}

void DListMenu::NoticeDrawer()
{
	if (!NoticeApplies())
	{
		mNoticeR = 0;       // no chip -> no hit target
		mNoticeFocused = false;
		return;
	}

	// Compact pill in the bottom-right, drawn in the OPTIONS-menu style: SmallFont at the smaller
	// CleanXfac_1 scale, plus the option-menu blinking ConFont cursor -- much smaller than the big
	// main-menu patches/skull. Positioned in absolute screen pixels (DTA_CleanNoMove_1), so the chip
	// rect (mNotice*) is stored in screen pixels for mouse hit-testing.
	const char *text = "Update available";
	const int cx1 = CleanXfac_1, cy1 = CleanYfac_1;
	const int screenW = screen->GetWidth(), screenH = screen->GetHeight();
	const int textW = SmallFont->StringWidth(text) * cx1;
	const int textH = SmallFont->GetHeight() * cy1;
	const int padX = 4 * cx1, padY = 3 * cy1, margin = 6 * cx1;
	const int pw = textW + 2 * padX;
	const int ph = textH + 2 * padY;
	const int px = screenW - margin - pw;
	const int py = screenH - margin - ph;
	mNoticeL = px; mNoticeT = py; mNoticeR = px + pw; mNoticeB = py + ph;

	int radius = 3 * cy1;
	const int halfMin = (pw < ph ? pw : ph) / 2;
	if (radius > halfMin) radius = halfMin;

	// Rounded gradient panel (brighter when focused so it reads as selected).
	const zx::PanelColor topCol = mNoticeFocused ? zx::PanelColor{ 44, 46, 66, 244 } : zx::PanelColor{ 26, 28, 40, 224 };
	const zx::PanelColor botCol = mNoticeFocused ? zx::PanelColor{ 18, 19, 30, 250 } : zx::PanelColor{ 8, 9, 15, 236 };
	for (int row = 0; row < ph; ++row)
	{
		int inset = zx::ComputeRoundedInset(row, ph, radius);
		int rowW = pw - 2 * inset;
		if (rowW <= 0)
			continue;
		zx::PanelColor c = zx::ComputePanelGradient(row, ph, topCol, botCol);
		screen->Dim(PalEntry(c.r, c.g, c.b), c.a / 255.f, px + inset, py + row, rowW, 1);
	}

	const int color = mNoticeFocused ? OptionSettings.mFontColorSelection : CR_GOLD;
	screen->DrawText(SmallFont, color, px + padX, py + padY, text, DTA_CleanNoMove_1, true, TAG_DONE);

	// Focused: the option-menu selection cursor (blinking ConFont pointer) just left of the pill.
	if (mNoticeFocused && (DMenu::MenuTime % 8) < 6)
	{
		screen->DrawText(ConFont, OptionSettings.mFontColorSelection, px - 7 * cx1, py + padY, "\xd",
			DTA_CellX, 8 * cx1, DTA_CellY, 8 * cy1, TAG_DONE);
	}
}

// Returns the value MenuEvent should return; `handled` false means "fall through to the list".
bool DListMenu::NoticeMenuEvent(int mkey, bool fromcontroller, bool &handled)
{
	handled = true;

	if (mDesc == NULL || mDesc->mMenuName != FName( "MainMenu" ))
	{
		handled = false;
		return false;
	}

	// Map the engine key to the notice's vocabulary and run the tested state machine; this only
	// applies the result (focus/selection, sound, action).
	zx::updater::NoticeKey key;
	switch (mkey)
	{
	case MKEY_Left:  key = zx::updater::NoticeKey::Left;  break;
	case MKEY_Right: key = zx::updater::NoticeKey::Right; break;
	case MKEY_Up:    key = zx::updater::NoticeKey::Up;    break;
	case MKEY_Down:  key = zx::updater::NoticeKey::Down;  break;
	case MKEY_Enter: key = zx::updater::NoticeKey::Enter; break;
	case MKEY_Back:  key = zx::updater::NoticeKey::Back;  break;
	default:         key = zx::updater::NoticeKey::Other; break;
	}

	const zx::updater::NoticeState before = { mNoticeFocused, mDesc->mSelectedItem, mNoticePrevSelected };
	const zx::updater::NoticeStep step =
		zx::updater::ComputeNoticeKey(before, zx::updater::IsAvailable(), key);
	const bool focusChanged = step.state.focused != mNoticeFocused;

	mNoticeFocused = step.state.focused;
	mDesc->mSelectedItem = step.state.selected;
	mNoticePrevSelected = step.state.prevSelected;

	switch (step.action)
	{
	case zx::updater::NoticeAction::Activate:
		NoticeActivate();
		return true;
	case zx::updater::NoticeAction::Delegate:
		handled = false;      // let the list handle it; it plays its own cursor sound if it moves
		return false;
	default: // Handled
		if (focusChanged) // entering/leaving the chip
			S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
		return true;
	}
}

bool DListMenu::NoticeMouseEvent(int type, int x, int y, bool &handled)
{
	handled = true;

	if (mDesc == NULL || mDesc->mMenuName != FName( "MainMenu" ))
	{
		handled = false;
		return false;
	}

	// A pointer that isn't actually moving (or clicking) must have NO effect -- otherwise a parked
	// cursor keeps re-asserting the mouse selection every frame and fights the keyboard (tested gate).
	const bool clicked = (type == MOUSE_Release || type == MOUSE_Click ||
		type == MOUSE_Release2 || type == MOUSE_Click2);
	const bool acts = zx::updater::ComputeMouseActs(mNoticeLastMouseX, mNoticeLastMouseY, x, y, clicked);
	mNoticeLastMouseX = x;
	mNoticeLastMouseY = y;
	if (!acts)
		return true; // resting pointer: ignore so it can't override the keyboard

	if (zx::updater::IsAvailable() && mNoticeR > 0)
	{
		// mNotice* is stored in screen pixels (the pill draws with DTA_CleanNoMove_1), so hit-test raw.
		if (x >= mNoticeL && x <= mNoticeR && y >= mNoticeT && y <= mNoticeB)
		{
			if (type == MOUSE_Release)
				NoticeActivate();
			else
				NoticeFocusChip();  // moving onto the chip selects it (and clears the list)
			return true;            // consume so the base handler doesn't deselect the list underneath
		}
	}
	// Real movement/click away from the chip -> the list owns the selection again.
	mNoticeFocused = false;
	handled = false;
	return false;
}

//=============================================================================
//
// base class for menu items
//
//=============================================================================

FListMenuItem::~FListMenuItem()
{
}

bool FListMenuItem::CheckCoordinate(int x, int y)
{
	return false;
}

void FListMenuItem::Ticker()
{
}

void FListMenuItem::Drawer(bool selected)
{
}

bool FListMenuItem::Selectable()
{
	return false;
}

void FListMenuItem::DrawSelector(int xofs, int yofs, FTextureID tex)
{
	if (tex.isNull())
	{
		if ((DMenu::MenuTime%8) < 6)
		{
			screen->DrawText(ConFont, OptionSettings.mFontColorSelection,
				mXpos + xofs, mYpos + yofs, "\xd", DTA_Clean, true, TAG_DONE);
		}
	}
	else
	{
		screen->DrawTexture (TexMan(tex), mXpos + xofs, mYpos + yofs, DTA_Clean, true, TAG_DONE);
	}
}

bool FListMenuItem::Activate()
{
	return false;	// cannot be activated
}

FName FListMenuItem::GetAction(int *pparam)
{
	return mAction;
}

bool FListMenuItem::SetString(int i, const char *s)
{
	return false;
}

bool FListMenuItem::GetString(int i, char *s, int len)
{
	return false;
}

bool FListMenuItem::SetValue(int i, int value)
{
	return false;
}

bool FListMenuItem::GetValue(int i, int *pvalue)
{
	return false;
}

void FListMenuItem::Enable(bool on)
{
	mEnabled = on;
}

bool FListMenuItem::MenuEvent(int mkey, bool fromcontroller)
{
	return false;
}

bool FListMenuItem::MouseEvent(int type, int x, int y)
{
	return false;
}

bool FListMenuItem::CheckHotkey(int c) 
{ 
	return false; 
}

int FListMenuItem::GetWidth() 
{ 
	return 0; 
}


//=============================================================================
//
// static patch
//
//=============================================================================

FListMenuItemStaticPatch::FListMenuItemStaticPatch(int x, int y, FTextureID patch, bool centered)
: FListMenuItem(x, y)
{
	mTexture = patch;
	mCentered = centered;
}
	
void FListMenuItemStaticPatch::Drawer(bool selected)
{
	int x = mXpos;
	FTexture *tex = TexMan(mTexture);
	if (mYpos >= 0)
	{
		if (mCentered) x -= tex->GetScaledWidth()/2;
		screen->DrawTexture (tex, x, mYpos, DTA_Clean, true, TAG_DONE);
	}
	else
	{
		int x = (mXpos - 160) * CleanXfac + (SCREENWIDTH>>1);
		if (mCentered) x -= (tex->GetScaledWidth()*CleanXfac)/2;
		screen->DrawTexture (tex, x, -mYpos*CleanYfac, DTA_CleanNoMove, true, TAG_DONE);
	}
}

//=============================================================================
//
// static text
//
//=============================================================================

FListMenuItemStaticText::FListMenuItemStaticText(int x, int y, const char *text, FFont *font, EColorRange color, bool centered)
: FListMenuItem(x, y)
{
	mText = ncopystring(text);
	mFont = font;
	mColor = color;
	mCentered = centered;
}
	
void FListMenuItemStaticText::Drawer(bool selected)
{
	const char *text = mText;
	if (text != NULL)
	{
		if (*text == '$') text = GStrings(text+1);
		if (mYpos >= 0)
		{
			int x = mXpos;
			if (mCentered) x -= mFont->StringWidth(text)/2;
			screen->DrawText(mFont, mColor, x, mYpos, text, DTA_Clean, true, TAG_DONE);
		}
		else
		{
			int x = (mXpos - 160) * CleanXfac + (SCREENWIDTH>>1);
			if (mCentered) x -= (mFont->StringWidth(text)*CleanXfac)/2;
			screen->DrawText (mFont, mColor, x, -mYpos*CleanYfac, text, DTA_CleanNoMove, true, TAG_DONE);
		}
	}
}

FListMenuItemStaticText::~FListMenuItemStaticText()
{
	if (mText != NULL) delete [] mText;
}

//=============================================================================
//
// base class for selectable items
//
//=============================================================================

FListMenuItemSelectable::FListMenuItemSelectable(int x, int y, int height, FName action, int param)
: FListMenuItem(x, y, action)
{
	mHeight = height;
	mParam = param;
	mHotkey = 0;
}

bool FListMenuItemSelectable::CheckCoordinate(int x, int y)
{
	return mEnabled && y >= mYpos && y < mYpos + mHeight;	// no x check here
}

bool FListMenuItemSelectable::Selectable()
{
	return mEnabled;
}

bool FListMenuItemSelectable::Activate()
{
	M_SetMenu(mAction, mParam);
	return true;
}

FName FListMenuItemSelectable::GetAction(int *pparam)
{
	if (pparam != NULL) *pparam = mParam;
	return mAction;
}

bool FListMenuItemSelectable::CheckHotkey(int c) 
{ 
	return c == tolower(mHotkey); 
}

bool FListMenuItemSelectable::MouseEvent(int type, int x, int y)
{
	if (type == DMenu::MOUSE_Release)
	{
		if (DMenu::CurrentMenu->MenuEvent(MKEY_Enter, true))
		{
			return true;
		}
	}
	return false;
}

//=============================================================================
//
// text item
//
//=============================================================================

FListMenuItemText::FListMenuItemText(int x, int y, int height, int hotkey, const char *text, FFont *font, EColorRange color, EColorRange color2, FName child, int param)
: FListMenuItemSelectable(x, y, height, child, param)
{
	mText = ncopystring(text);
	mFont = font;
	mColor = color;
	mColorSelected = color2;
	mHotkey = hotkey;
}

FListMenuItemText::~FListMenuItemText()
{
	if (mText != NULL)
	{
		delete [] mText;
	}
}

void FListMenuItemText::Drawer(bool selected)
{
	const char *text = mText;
	if (text != NULL)
	{
		if (*text == '$') text = GStrings(text+1);
		screen->DrawText(mFont, selected ? mColorSelected : mColor, mXpos, mYpos, text, DTA_Clean, true, TAG_DONE);
	}
}

int FListMenuItemText::GetWidth() 
{ 
	const char *text = mText;
	if (text != NULL)
	{
		if (*text == '$') text = GStrings(text+1);
		return mFont->StringWidth(text); 
	}
	return 1;
}


//=============================================================================
//
// patch item
//
//=============================================================================

FListMenuItemPatch::FListMenuItemPatch(int x, int y, int height, int hotkey, FTextureID patch, FName child, int param)
: FListMenuItemSelectable(x, y, height, child, param)
{
	mHotkey = hotkey;
	mTexture = patch;
}

void FListMenuItemPatch::Drawer(bool selected)
{
	screen->DrawTexture (TexMan(mTexture), mXpos, mYpos, DTA_Clean, true, TAG_DONE);
}

int FListMenuItemPatch::GetWidth()
{
	return mTexture.isValid()
		? TexMan[mTexture]->GetScaledWidth()
		: 0;
}

// [rc4l] The base FListMenuItem::GetWidth returns 0, so a static patch (a menu's logo art) was
// invisible to anything measuring a menu's extent. Reporting the real width lets panel/backdrop
// code size itself from the descriptor instead of hardcoding per-game numbers.
int FListMenuItemStaticPatch::GetWidth()
{
	return mTexture.isValid()
		? TexMan[mTexture]->GetScaledWidth()
		: 0;
}

// [rc4l] How many rows of the patch are fully transparent before the artwork starts.
//
// A title patch is authored with slack above the letters -- Freedoom's M_DOOM has a lot of it -- so
// measuring the item's BOX puts far more visible space above the logo than below the rows beneath
// it, even when the surrounding padding is mathematically symmetric. Reading the ink instead makes
// the gap the player actually sees match on both sides, for any IWAD's art rather than a number
// tuned to one.
//
// Scanned once and cached: GetColumn decodes the texture, and the panel measures every item every
// frame.
int FListMenuItemStaticPatch::GetInkTop()
{
	if (mInkTop >= 0)
		return mInkTop;

	mInkTop = 0;
	if (!mTexture.isValid())
		return mInkTop;

	FTexture *tex = TexMan[mTexture];
	// An opaque texture has no leading gap by definition, and skipping it avoids decoding the
	// biggest textures for nothing.
	if (tex == NULL || !tex->bMasked)
		return mInkTop;

	const int h = tex->GetHeight(), w = tex->GetWidth();
	int top = h;
	for (int col = 0; col < w; ++col)
	{
		const FTexture::Span *spans;
		tex->GetColumn(col, &spans);
		// A zero Length terminates the column, so an all-transparent column contributes nothing.
		if (spans != NULL && spans[0].Length != 0 && spans[0].TopOffset < top)
		{
			top = spans[0].TopOffset;
			if (top == 0)
				break;			// cannot do better; stop decoding columns
		}
	}

	// Fully transparent art: leave the box alone rather than collapse the panel onto nothing.
	if (top >= h)
		return mInkTop;

	// The scan is in texture rows; the item is measured in scaled (menu) coordinates.
	mInkTop = tex->GetScaledHeight() > 0 && h > 0
		? Scale(top, tex->GetScaledHeight(), h)
		: top;
	return mInkTop;
}


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
		b.x = item->GetX();
		b.y = item->GetY();
		b.w = item->GetWidth();
		b.inkTop = item->GetInkTop();
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

	const int padV = 8;					// virtual px of breathing room around the content
	const int cx = CleanXfac, cy = CleanYfac;
	const int sw = screen->GetWidth(), sh = screen->GetHeight();
	// DTA_Clean maps the virtual page onto the screen scaled by Clean*fac and centred, so a virtual
	// coordinate becomes (v - centre) * fac + screenCentre.
	const int topPx    = (vTop    - padV - 100) * cy + sh / 2;
	const int bottomPx = (vBottom + padV - 100) * cy + sh / 2;
	// ComputePanelRect centres the panel on screen, so its half-width has to reach the further of
	// the two content edges from the virtual centre -- sizing it to the raw content width would
	// clip whichever side sticks out more.
	const int halfV    = MAX(160 - vLeft, vRight - 160);
	const int panelWpx = (2 * halfV + 2 * padV) * cx;

	zx::PanelRect r = zx::ComputePanelRect(sw, sh, panelWpx, topPx, bottomPx, 0, 6 * cy);
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

//=============================================================================
//
// [rc4l] DUpdateMainMenu -- retained ONLY so existing `Class "UpdateMainMenu"` lines still resolve.
//
// The "update available" chip used to live here, as a DListMenu subclass wired to the main menu by
// a `Class` line in menudef. That silently broke every mod that replaces the main menu:
// ReplaceMenu() refuses an override whose class does not match the existing descriptor's
// (CheckCompatible, menudef.cpp) and mods declare no class, so their menu was discarded. The chip
// now lives on DListMenu itself, gated on the descriptor being MainMenu -- see menu.h.
//
// This empty subclass stays because removing a class from the registry is a breaking change:
// PClass::FindClass() drives menudef's `Class` keyword, so any wad (or the workaround suggested to
// the reporter of the original bug) naming UpdateMainMenu would hard-error with "Unknown menu
// class". It inherits the chip from DListMenu like any other list menu, so it behaves identically.
//
//=============================================================================

class DUpdateMainMenu : public DListMenu
{
	DECLARE_CLASS(DUpdateMainMenu, DListMenu)
public:
	DUpdateMainMenu() { }
};

IMPLEMENT_CLASS(DUpdateMainMenu)
