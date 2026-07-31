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
	Super::Drawer();
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


//=============================================================================
//
// [rc4l] DUpdateMainMenu -- the main menu with a bottom-right "update available" notice.
//
// A DListMenu subclass wired to the main menu via `Class "UpdateMainMenu"` in menudef. When the
// updater state says a newer release exists (zx::updater::IsAvailable()), it draws a small rounded
// chip in the bottom-right corner -- same rounded-panel language as the open-link dialog. The chip is
// reached with Right (Left/Up/Down/Back leave it) or by clicking it; activating it opens the
// OS-correct download confirmation (M_ConfirmDownloadRelease). With no update pending the menu behaves
// exactly like a stock DListMenu.
//
//=============================================================================

class DUpdateMainMenu : public DListMenu
{
	DECLARE_CLASS(DUpdateMainMenu, DListMenu)

	bool mNoticeFocused;
	int mPrevSelected;   // list item that was selected before the chip took focus, to restore on exit
	int mLastMouseX, mLastMouseY; // last mouse position, so a still pointer can't fight the keyboard
	// Chip rectangle in 320x200 virtual coords, cached from the last Drawer for mouse hit-testing.
	int mChipL, mChipT, mChipR, mChipB;

	void Activate();
	void FocusChip();    // move focus to the chip, remembering (and clearing) the list selection

public:
	DUpdateMainMenu() : mNoticeFocused(false), mPrevSelected(0), mLastMouseX(INT_MIN), mLastMouseY(INT_MIN),
		mChipL(0), mChipT(0), mChipR(0), mChipB(0) {}
	void Drawer();
	bool MenuEvent(int mkey, bool fromcontroller);
	bool MouseEvent(int type, int x, int y);
};

IMPLEMENT_CLASS(DUpdateMainMenu)

void DUpdateMainMenu::Activate()
{
	S_Sound(CHAN_VOICE | CHAN_UI, "menu/choose", snd_menuvolume, ATTN_NONE);
	M_ConfirmDownloadRelease(zx::updater::Tag());
}

void DUpdateMainMenu::FocusChip()
{
	if (!mNoticeFocused)
	{
		if (mDesc->mSelectedItem >= 0)
			mPrevSelected = mDesc->mSelectedItem; // remember where we were, to restore on exit
		mNoticeFocused = true;
		mDesc->mSelectedItem = -1;                // chip owns the selection; nothing in the list
	}
}

void DUpdateMainMenu::Drawer()
{
	DListMenu::Drawer();

	if (!zx::updater::IsAvailable())
	{
		mChipR = 0; // no chip -> no hit target
		return;
	}

	// Chip layout in 320x200 virtual space, anchored to the bottom-right with a small margin.
	const char *text = "Update available";
	const int th = SmallFont->GetHeight();
	const int tw = SmallFont->StringWidth(text);
	const int padX = 6, padY = 3, margin = 5;
	mChipR = 320 - margin;
	mChipB = 200 - margin;
	mChipL = mChipR - (tw + 2 * padX);
	mChipT = mChipB - (th + 2 * padY);

	// Rounded gradient panel, drawn in screen pixels (like the message-box panel). Not centred -- the
	// chip lives in the corner -- so we drive ComputeRoundedInset/Gradient directly over our own rect.
	const int cx = CleanXfac, cy = CleanYfac;
	const int originX = (screen->GetWidth() - 320 * cx) / 2;
	const int originY = (screen->GetHeight() - 200 * cy) / 2;
	const int px = originX + mChipL * cx;
	const int py = originY + mChipT * cy;
	const int pw = (mChipR - mChipL) * cx;
	const int ph = (mChipB - mChipT) * cy;
	int radius = 4 * cy;
	const int halfMin = (pw < ph ? pw : ph) / 2;
	if (radius > halfMin) radius = halfMin;

	// Brighter when focused so it reads as selected.
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
	const int textX = mChipL + padX;
	const int textY = mChipT + padY;
	screen->DrawText(SmallFont, color, textX, textY, text, DTA_Clean, true, TAG_DONE);

	// When focused, draw the SAME selection cursor the list items use (the menu's mSelector, e.g. the
	// Doom skull), positioned by the descriptor's offsets relative to the text -- so the chip highlights
	// exactly like every other main-menu option.
	if (mNoticeFocused)
	{
		const int selx = textX + mDesc->mSelectOfsX;
		const int sely = textY + mDesc->mSelectOfsY;
		if (mDesc->mSelector.isNull())
		{
			if ((DMenu::MenuTime % 8) < 6)
				screen->DrawText(ConFont, OptionSettings.mFontColorSelection, selx, sely, "\xd",
					DTA_Clean, true, TAG_DONE);
		}
		else
		{
			screen->DrawTexture(TexMan(mDesc->mSelector), selx, sely, DTA_Clean, true, TAG_DONE);
		}
	}
}

bool DUpdateMainMenu::MenuEvent(int mkey, bool fromcontroller)
{
	if (!zx::updater::IsAvailable())
	{
		mNoticeFocused = false;
		return DListMenu::MenuEvent(mkey, fromcontroller);
	}

	if (mNoticeFocused)
	{
		switch (mkey)
		{
		case MKEY_Enter:
			Activate();
			return true;
		case MKEY_Right:
			return true; // already at the rightmost element
		case MKEY_Left:
		case MKEY_Back:
			// Hand focus back to the list at the item we left from, so the cursor doesn't jump.
			mNoticeFocused = false;
			mDesc->mSelectedItem = mPrevSelected;
			S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
			return true;
		case MKEY_Up:
		case MKEY_Down:
			mNoticeFocused = false; // step back into the list and let it move from -1
			return DListMenu::MenuEvent(mkey, fromcontroller);
		default:
			return true; // swallow everything else while the chip holds focus
		}
	}

	if (mkey == MKEY_Right)
	{
		FocusChip();
		S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
		return true;
	}
	return DListMenu::MenuEvent(mkey, fromcontroller);
}

bool DUpdateMainMenu::MouseEvent(int type, int x, int y)
{
	// A pointer that isn't actually moving (or clicking) must have NO effect -- otherwise a parked
	// cursor keeps re-asserting the mouse selection every frame and fights the keyboard. Only real
	// movement or a click drives selection; everything else is ignored.
	const bool moved = (x != mLastMouseX || y != mLastMouseY);
	const bool clicked = (type == MOUSE_Release || type == MOUSE_Click ||
		type == MOUSE_Release2 || type == MOUSE_Click2);
	mLastMouseX = x;
	mLastMouseY = y;
	if (!moved && !clicked)
		return true; // resting pointer: ignore so it can't override the keyboard

	if (zx::updater::IsAvailable() && mChipR > 0)
	{
		// Convert to the same 320x200 virtual space the chip rect is stored in.
		int vx = ((x - (screen->GetWidth() / 2)) / CleanXfac) + 160;
		int vy = ((y - (screen->GetHeight() / 2)) / CleanYfac) + 100;
		if (vx >= mChipL && vx <= mChipR && vy >= mChipT && vy <= mChipB)
		{
			if (type == MOUSE_Release)
				Activate();
			else
				FocusChip();      // moving onto the chip selects it (and clears the list)
			return true;          // consume so the base handler doesn't deselect the list underneath
		}
	}
	// Real movement/click away from the chip -> the list owns the selection again.
	mNoticeFocused = false;
	return DListMenu::MouseEvent(type, x, y);
}
