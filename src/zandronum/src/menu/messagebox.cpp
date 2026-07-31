/*
** messagebox.cpp
** Confirmation, notification screns
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

#include "menu/menu.h"
#include "d_event.h"
#include "d_gui.h"
#include "v_video.h"
#include "v_text.h"
#include "d_main.h"
#include "gstrings.h"
#include "gi.h"
#include "i_video.h"
#include "st_start.h"
#include "c_dispatch.h"
#include "g_game.h"
#include "i_system.h"                                       // [rc4l] I_OpenURL
#include "gitinfo.h"                                        // [rc4l] GIT_DESCRIPTION for the version diff
#include "v_palette.h"                                      // [rc4l] PalEntry for the prompt panel
#include "features/updater/computation/openurl_compute.h"   // [rc4l] zx::IsOpenableURL scheme gate
#include "features/updater/computation/promptpanel_compute.h" // [rc4l] rounded panel geometry/gradient
#include "features/updater/computation/release_url_compute.h" // [rc4l] OS-correct download URL


extern FSaveGameNode *quickSaveSlot;

class DMessageBoxMenu : public DMenu
{
	DECLARE_CLASS(DMessageBoxMenu, DMenu)

protected: // [rc4l] protected (was private) so DOpenURLMenu can size its panel and draw the cursor.
	FBrokenLines *mMessage;
	int mMessageMode;
	int messageSelection;
	int mMouseLeft, mMouseRight, mMouseY;
	FName mAction;

public:

	DMessageBoxMenu(DMenu *parent = NULL, const char *message = NULL, int messagemode = 0, bool playsound = false, FName action = NAME_None);
	void Destroy();
	void Init(DMenu *parent, const char *message, int messagemode, bool playsound = false);
	void Drawer();
	bool Responder(event_t *ev);
	bool MenuEvent(int mkey, bool fromcontroller);
	bool MouseEvent(int type, int x, int y);
	void CloseSound();
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DMessageBoxMenu)

//=============================================================================
//
//
//
//=============================================================================

DMessageBoxMenu::DMessageBoxMenu(DMenu *parent, const char *message, int messagemode, bool playsound, FName action)
: DMenu(parent)
{
	mAction = action;
	messageSelection = 0;
	mMouseLeft = 140;
	mMouseY = INT_MIN;
	int mr1 = 170 + SmallFont->StringWidth(GStrings["TXT_YES"]);
	int mr2 = 170 + SmallFont->StringWidth(GStrings["TXT_NO"]);
	mMouseRight = MAX(mr1, mr2);

	Init(parent, message, messagemode, playsound);
}

//=============================================================================
//
//
//
//=============================================================================

void DMessageBoxMenu::Init(DMenu *parent, const char *message, int messagemode, bool playsound)
{
	mParentMenu = parent;
	if (message != NULL) 
	{
		if (*message == '$') message = GStrings(message+1);
		mMessage = V_BreakLines(SmallFont, 300, message);
	}
	else mMessage = NULL;
	mMessageMode = messagemode;
	if (playsound)
	{
		S_StopSound (CHAN_VOICE);
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/prompt", snd_menuvolume, ATTN_NONE);
	}
}

//=============================================================================
//
//
//
//=============================================================================

void DMessageBoxMenu::Destroy()
{
	if (mMessage != NULL) V_FreeBrokenLines(mMessage);
	mMessage = NULL;
}

//=============================================================================
//
//
//
//=============================================================================

void DMessageBoxMenu::CloseSound()
{
	S_Sound (CHAN_VOICE | CHAN_UI, 
		DMenu::CurrentMenu != NULL? "menu/backup" : "menu/dismiss", snd_menuvolume, ATTN_NONE);
}

//=============================================================================
//
//
//
//=============================================================================

void DMessageBoxMenu::HandleResult(bool res)
{
	if (mParentMenu != NULL)
	{
		if (mMessageMode == 0)
		{
			if (mAction == NAME_None) 
			{
				mParentMenu->MenuEvent(res? MKEY_MBYes : MKEY_MBNo, false);
				Close();
			}
			else 
			{
				Close();
				if (res) M_SetMenu(mAction, -1);
			}
			CloseSound();
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

void DMessageBoxMenu::Drawer ()
{
	int i, y;
	PalEntry fade = 0;

	int fontheight = SmallFont->GetHeight();
	//V_SetBorderNeedRefresh();
	//ST_SetNeedRefresh();

	y = 100;

	if (mMessage != NULL)
	{
		for (i = 0; mMessage[i].Width >= 0; i++)
			y -= SmallFont->GetHeight () / 2;

		for (i = 0; mMessage[i].Width >= 0; i++)
		{
			screen->DrawText (SmallFont, CR_UNTRANSLATED, 160 - mMessage[i].Width/2, y, mMessage[i].Text,
				DTA_Clean, true, TAG_DONE);
			y += fontheight;
		}
	}

	if (mMessageMode == 0)
	{
		y += fontheight;
		mMouseY = y;
		screen->DrawText(SmallFont, 
			messageSelection == 0? OptionSettings.mFontColorSelection : OptionSettings.mFontColor, 
			160, y, GStrings["TXT_YES"], DTA_Clean, true, TAG_DONE);
		screen->DrawText(SmallFont, 
			messageSelection == 1? OptionSettings.mFontColorSelection : OptionSettings.mFontColor, 
			160, y + fontheight + 1, GStrings["TXT_NO"], DTA_Clean, true, TAG_DONE);

		if (messageSelection >= 0)
		{
			if ((DMenu::MenuTime%8) < 6)
			{
				screen->DrawText(ConFont, OptionSettings.mFontColorSelection,
					(150 - 160) * CleanXfac + screen->GetWidth() / 2,
					(y + (fontheight + 1) * messageSelection - 100 + fontheight/2 - 5) * CleanYfac + screen->GetHeight() / 2,
					"\xd",
					DTA_CellX, 8 * CleanXfac,
					DTA_CellY, 8 * CleanYfac,
					TAG_DONE);
			}
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

bool DMessageBoxMenu::Responder(event_t *ev)
{
	if (ev->type == EV_GUI_Event && ev->subtype == EV_GUI_KeyDown)
	{
		if (mMessageMode == 0)
		{
			int ch = tolower(ev->data1);
			if (ch == 'n' || ch == ' ') 
			{
				HandleResult(false);		
				return true;
			}
			else if (ch == 'y') 
			{
				HandleResult(true);
				return true;
			}
		}
		else
		{
			Close();
			return true;
		}
		return false;
	}
	else if (ev->type == EV_KeyDown)
	{
		Close();
		return true;
	}
	return Super::Responder(ev);
}

//=============================================================================
//
//
//
//=============================================================================

bool DMessageBoxMenu::MenuEvent(int mkey, bool fromcontroller)
{
	if (mMessageMode == 0)
	{
		if (mkey == MKEY_Up || mkey == MKEY_Down)
		{
			S_Sound (CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
			messageSelection = !messageSelection;
			return true;
		}
		else if (mkey == MKEY_Enter)
		{
			// 0 is yes, 1 is no
			HandleResult(!messageSelection);
			return true;
		}
		else if (mkey == MKEY_Back)
		{
			HandleResult(false);
			return true;
		}
		return false;
	}
	else
	{
		Close();
		CloseSound();
		return true;
	}
}

//=============================================================================
//
//
//
//=============================================================================

bool DMessageBoxMenu::MouseEvent(int type, int x, int y)
{
	if (mMessageMode == 1)
	{
		if (type == MOUSE_Click)
		{
			return MenuEvent(MKEY_Enter, true);
		}
		return false;
	}
	else
	{
		int sel = -1;
		int fh = SmallFont->GetHeight() + 1;

		// convert x/y from screen to virtual coordinates, according to CleanX/Yfac use in DrawTexture
		x = ((x - (screen->GetWidth() / 2)) / CleanXfac) + 160;
		y = ((y - (screen->GetHeight() / 2)) / CleanYfac) + 100;

		if (x >= mMouseLeft && x <= mMouseRight && y >= mMouseY && y < mMouseY + 2 * fh)
		{
			sel = y >= mMouseY + fh;
		}
		if (sel != -1 && sel != messageSelection)
		{
			//S_Sound (CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
		}
		messageSelection = sel;
		if (type == MOUSE_Release)
		{
			return MenuEvent(MKEY_Enter, true);
		}
		return true;
	}
}

//=============================================================================
//
//
//
//=============================================================================
//=============================================================================
//
//
//
//=============================================================================

class DQuitMenu : public DMessageBoxMenu
{
	DECLARE_CLASS(DQuitMenu, DMessageBoxMenu)

public:

	DQuitMenu(bool playsound = false);
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DQuitMenu)

//=============================================================================
//
//
//
//=============================================================================

DQuitMenu::DQuitMenu(bool playsound)
{
	int messageindex = gametic % gameinfo.quitmessages.Size();
	FString EndString;
	const char *msg = gameinfo.quitmessages[messageindex];
	if (msg[0] == '$') 
	{
		if (msg[1] == '*')
		{
			EndString = GStrings(msg+2);
		}
		else
		{
			EndString.Format("%s\n\n%s", GStrings(msg+1), GStrings("DOSY"));
		}
	}
	else EndString = gameinfo.quitmessages[messageindex];

	Init(NULL, EndString, 0, playsound);
}

//=============================================================================
//
//
//
//=============================================================================

void DQuitMenu::HandleResult(bool res)
{
	if (res)
	{
		// [BB] !netgame -> ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		if ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		{
			if (gameinfo.quitSound.IsNotEmpty())
			{
				S_Sound (CHAN_VOICE | CHAN_UI, gameinfo.quitSound, snd_menuvolume, ATTN_NONE);
				I_WaitVBL (105);
			}
		}
		ST_Endoom();
	}
	else
	{
		Close();
		CloseSound();
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (menu_quit)
{	// F10
	M_StartControlPanel (true);
	DMenu *newmenu = new DQuitMenu(false);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}



//=============================================================================
//
//
//
//=============================================================================
//=============================================================================
//
//
//
//=============================================================================

class DEndGameMenu : public DMessageBoxMenu
{
	DECLARE_CLASS(DEndGameMenu, DMessageBoxMenu)

public:

	DEndGameMenu(bool playsound = false);
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DEndGameMenu)

//=============================================================================
//
//
//
//=============================================================================

DEndGameMenu::DEndGameMenu(bool playsound)
{
	// [BB] netgame -> ( NETWORK_GetState( ) != NETSTATE_SINGLE )
	Init(NULL, GStrings(( NETWORK_GetState( ) != NETSTATE_SINGLE ) ? "NETEND" : "ENDGAME"), 0, playsound);
}

//=============================================================================
//
//
//
//=============================================================================

void DEndGameMenu::HandleResult(bool res)
{
	if (res)
	{
		M_ClearMenus ();
		// [BB] !netgame -> ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		if ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		{
			D_StartTitle ();
		}
	}
	else
	{
		Close();
		CloseSound();
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (menu_endgame)
{	// F7
	if (!usergame)
	{
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE);
		return;
	}
		
	//M_StartControlPanel (true);
	S_Sound (CHAN_VOICE | CHAN_UI, "menu/activate", snd_menuvolume, ATTN_NONE);
	DMenu *newmenu = new DEndGameMenu(false);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}

//=============================================================================
//
//
//
//=============================================================================
//=============================================================================
//
//
//
//=============================================================================

class DQuickSaveMenu : public DMessageBoxMenu
{
	DECLARE_CLASS(DQuickSaveMenu, DMessageBoxMenu)

public:

	DQuickSaveMenu(bool playsound = false);
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DQuickSaveMenu)

//=============================================================================
//
//
//
//=============================================================================

DQuickSaveMenu::DQuickSaveMenu(bool playsound)
{
	FString tempstring;

	tempstring.Format(GStrings("QSPROMPT"), quickSaveSlot->Title);
	Init(NULL, tempstring, 0, playsound);
}

//=============================================================================
//
//
//
//=============================================================================

void DQuickSaveMenu::HandleResult(bool res)
{
	if (res)
	{
		G_SaveGame (quickSaveSlot->Filename.GetChars(), quickSaveSlot->Title);
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/dismiss", snd_menuvolume, ATTN_NONE);
		M_ClearMenus();
	}
	else
	{
		Close();
		CloseSound();
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (quicksave)
{	// F6
	// [BB] !multiplayer -> ( NETWORK_GetState( ) == NETSTATE_SINGLE )
	if (!usergame || (players[consoleplayer].health <= 0 && ( NETWORK_GetState( ) == NETSTATE_SINGLE )))
	{
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/invalid", snd_menuvolume, ATTN_NONE);
		return;
	}

	if (gamestate != GS_LEVEL)
		return;
		
	S_Sound (CHAN_VOICE | CHAN_UI, "menu/activate", snd_menuvolume, ATTN_NONE);
	if (quickSaveSlot == NULL)
	{
		M_StartControlPanel(false);
		M_SetMenu(NAME_Savegamemenu);
		return;
	}
	DMenu *newmenu = new DQuickSaveMenu(false);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}

//=============================================================================
//
//
//
//=============================================================================
//=============================================================================
//
//
//
//=============================================================================

class DQuickLoadMenu : public DMessageBoxMenu
{
	DECLARE_CLASS(DQuickLoadMenu, DMessageBoxMenu)

public:

	DQuickLoadMenu(bool playsound = false);
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DQuickLoadMenu)

//=============================================================================
//
//
//
//=============================================================================

DQuickLoadMenu::DQuickLoadMenu(bool playsound)
{
	FString tempstring;

	tempstring.Format(GStrings("QLPROMPT"), quickSaveSlot->Title);
	Init(NULL, tempstring, 0, playsound);
}

//=============================================================================
//
//
//
//=============================================================================

void DQuickLoadMenu::HandleResult(bool res)
{
	if (res)
	{
		G_LoadGame (quickSaveSlot->Filename.GetChars());
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/dismiss", snd_menuvolume, ATTN_NONE);
		M_ClearMenus();
	}
	else
	{
		Close();
		CloseSound();
	}
}

//=============================================================================
//
//
//
//=============================================================================

CCMD (quickload)
{	// F9
	M_StartControlPanel (true);

	// [BB] netgame -> ( NETWORK_GetState( ) != NETSTATE_SINGLE )
	if ( NETWORK_GetState( ) != NETSTATE_SINGLE )
	{
		M_StartMessage (GStrings("QLOADNET"), 1);
		return;
	}
		
	if (quickSaveSlot == NULL)
	{
		M_StartControlPanel(false);
		// signal that whatever gets loaded should be the new quicksave
		quickSaveSlot = (FSaveGameNode *)1;
		M_SetMenu(NAME_Loadgamemenu);
		return;
	}
	DMenu *newmenu = new DQuickLoadMenu(false);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}

//=============================================================================
//
//
//
//=============================================================================

void M_StartMessage(const char *message, int messagemode, FName action)
{
	if (DMenu::CurrentMenu == NULL) 
	{
		// only play a sound if no menu was active before
		M_StartControlPanel(menuactive == MENU_Off);
	}
	DMenu *newmenu = new DMessageBoxMenu(DMenu::CurrentMenu, message, messagemode, false, action);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}

//=============================================================================
//
// [rc4l] "Open this link?" confirmation. Same yes/no shape as DQuitMenu et al., but Yes runs C code
// (I_OpenURL) instead of dispatching a menu name. The URL is a member so HandleResult can act on it;
// the prompt text that contains the URL is built by M_ConfirmOpenURL, never supplied by the caller,
// so a mod invoking `openurl` can never show one link and open another.
//
//=============================================================================

class DOpenURLMenu : public DMessageBoxMenu
{
	DECLARE_CLASS(DOpenURLMenu, DMessageBoxMenu)

	FString mURL;
	FBrokenLines *mCaptionLines;   // the question, e.g. "Open this link in your web browser?"
	FBrokenLines *mLinkLines;      // the URL itself, drawn in a distinct colour for readability
	FBrokenLines *mFooterLines;    // the "This will leave ZandroX." note

	int CountLines(FBrokenLines *lines) const;
	void DrawBlock(FBrokenLines *lines, int color, int &y, int fh) const;

public:

	// All args defaulted so the class is default-constructible, which DObject's IMPLEMENT_CLASS
	// requires (matches DQuitMenu et al.). The real call always passes a validated url + captions.
	DOpenURLMenu(const char *url = NULL, const char *caption = NULL, const char *footer = NULL,
		bool playsound = false);
	virtual void Destroy();
	virtual void Drawer();
	virtual void HandleResult(bool res);
};

IMPLEMENT_CLASS(DOpenURLMenu)

DOpenURLMenu::DOpenURLMenu(const char *url, const char *caption, const char *footer, bool playsound)
{
	mURL = (url != NULL) ? url : "";
	if (caption == NULL) caption = "Open this link in your web browser?";

	// Break the parts separately (same 300px width the base uses) so the URL is its own block and can
	// be coloured independently. mMessage stays NULL -- we fully custom-draw. The footer is optional:
	// a NULL/empty footer draws no bottom line (so a self-explanatory caption isn't echoed twice).
	mCaptionLines = V_BreakLines(SmallFont, 300, caption);
	mLinkLines    = V_BreakLines(SmallFont, 300, mURL.GetChars());
	mFooterLines  = (footer != NULL && footer[0] != '\0') ? V_BreakLines(SmallFont, 300, footer) : NULL;

	Init(NULL, NULL, 0, playsound);
}

void DOpenURLMenu::Destroy()
{
	if (mCaptionLines != NULL) V_FreeBrokenLines(mCaptionLines);
	if (mLinkLines != NULL)    V_FreeBrokenLines(mLinkLines);
	if (mFooterLines != NULL)  V_FreeBrokenLines(mFooterLines);
	mCaptionLines = mLinkLines = mFooterLines = NULL;
	Super::Destroy();
}

int DOpenURLMenu::CountLines(FBrokenLines *lines) const
{
	int n = 0;
	if (lines != NULL)
		while (lines[n].Width >= 0) ++n;
	return n;
}

// Draw one wrapped block centred horizontally at x=160 (320x200 virtual space, DTA_Clean), advancing
// y by one font height per line. Colour applies to the whole block.
void DOpenURLMenu::DrawBlock(FBrokenLines *lines, int color, int &y, int fh) const
{
	int n = CountLines(lines);
	for (int i = 0; i < n; ++i)
	{
		screen->DrawText(SmallFont, color, 160 - lines[i].Width / 2, y, lines[i].Text,
			DTA_Clean, true, TAG_DONE);
		y += fh;
	}
}

void DOpenURLMenu::Drawer()
{
	const int fh = SmallFont->GetHeight();
	const int nCap = CountLines(mCaptionLines);
	const int nLink = CountLines(mLinkLines);
	const int nFoot = CountLines(mFooterLines);
	const int nGaps = 1 + (nFoot > 0 ? 1 : 0);             // caption/link gap always; link/footer only if a footer
	const int totalTextLines = nCap + nLink + nFoot + nGaps;

	// Vertical layout mirrors DMessageBoxMenu::Drawer: centre the text block, then Yes/No below it.
	int blockTop = 100 - totalTextLines * (fh / 2);
	int textBottom = blockTop + totalTextLines * fh;
	int yYes = textBottom + fh;                            // base does one `y += fontheight` here
	int contentBottom = yYes + fh + 1 + fh;               // bottom of the "No" line

	// Widest rendered line across every block plus the Yes/No options -> panel width.
	int maxW = SmallFont->StringWidth(GStrings["TXT_YES"]);
	int noW = SmallFont->StringWidth(GStrings["TXT_NO"]);
	if (noW > maxW) maxW = noW;
	FBrokenLines *all[3] = { mCaptionLines, mLinkLines, mFooterLines };
	for (int b = 0; b < 3; ++b)
	{
		int n = CountLines(all[b]);
		for (int i = 0; i < n; ++i)
			if (all[b][i].Width > maxW) maxW = all[b][i].Width;
	}

	// --- background panel (rounded, dark vertical gradient) so the text reads over any screen -------
	const int cx = CleanXfac, cy = CleanYfac;
	const int screenW = screen->GetWidth(), screenH = screen->GetHeight();
	const int originY = (screenH - 200 * cy) / 2;
	const int padXv = 16, padYv = 10;                     // padding in 320x200 virtual pixels
	const int panelWpx = (maxW + 2 * padXv) * cx;
	const int contentTopPx = originY + blockTop * cy;
	const int contentBottomPx = originY + contentBottom * cy;

	zx::PanelRect r = zx::ComputePanelRect(screenW, screenH, panelWpx,
		contentTopPx, contentBottomPx, padYv * cy, 6 * cy);
	const zx::PanelColor topCol = { 26, 28, 40, 236 };
	const zx::PanelColor botCol = { 8, 9, 15, 248 };
	for (int row = 0; row < r.h; ++row)
	{
		int inset = zx::ComputeRoundedInset(row, r.h, r.radius);
		int rowW = r.w - 2 * inset;
		if (rowW <= 0)
			continue;
		zx::PanelColor c = zx::ComputePanelGradient(row, r.h, topCol, botCol);
		screen->Dim(PalEntry(c.r, c.g, c.b), c.a / 255.f, r.x + inset, r.y + row, rowW, 1);
	}

	// --- text: caption (white), the URL (gold, distinct) and the footer note (white) ---------------
	int y = blockTop;
	DrawBlock(mCaptionLines, CR_WHITE, y, fh);
	y += fh;                                               // gap
	DrawBlock(mLinkLines, CR_GOLD, y, fh);
	if (nFoot > 0)
	{
		y += fh;                                           // gap
		DrawBlock(mFooterLines, CR_WHITE, y, fh);
	}

	// --- Yes / No + blinking selection cursor (same as DMessageBoxMenu::Drawer) ---------------------
	mMouseY = yYes;
	screen->DrawText(SmallFont, messageSelection == 0 ? OptionSettings.mFontColorSelection : OptionSettings.mFontColor,
		160, yYes, GStrings["TXT_YES"], DTA_Clean, true, TAG_DONE);
	screen->DrawText(SmallFont, messageSelection == 1 ? OptionSettings.mFontColorSelection : OptionSettings.mFontColor,
		160, yYes + fh + 1, GStrings["TXT_NO"], DTA_Clean, true, TAG_DONE);
	if (messageSelection >= 0 && (DMenu::MenuTime % 8) < 6)
	{
		screen->DrawText(ConFont, OptionSettings.mFontColorSelection,
			(150 - 160) * CleanXfac + screenW / 2,
			(yYes + (fh + 1) * messageSelection - 100 + fh / 2 - 5) * CleanYfac + screenH / 2,
			"\xd", DTA_CellX, 8 * CleanXfac, DTA_CellY, 8 * CleanYfac, TAG_DONE);
	}
}

void DOpenURLMenu::HandleResult(bool res)
{
	if (res)
	{
		I_OpenURL(mURL.GetChars());
		S_Sound(CHAN_VOICE | CHAN_UI, "menu/dismiss", snd_menuvolume, ATTN_NONE);
		M_ClearMenus();
	}
	else
	{
		Close();
		CloseSound();
	}
}

//=============================================================================
//
// [rc4l] Shared launcher for the URL confirmation dialogs below. `url` is validated up front (the
// same allowlist I_OpenURL re-checks); the caption/footer are supplied by us, never the caller, so
// the shown link always matches what Yes opens.
//
//=============================================================================

static void M_StartOpenURLMenu(const char *url, const char *caption, const char *footer)
{
	if (!zx::IsOpenableURL(url))
	{
		Printf("openurl: refusing to open %s (only http/https URLs are allowed)\n",
			url != NULL ? url : "(null)");
		return;
	}
	if (DMenu::CurrentMenu == NULL)
		M_StartControlPanel(menuactive == MENU_Off);
	DMenu *newmenu = new DOpenURLMenu(url, caption, footer);
	newmenu->mParentMenu = DMenu::CurrentMenu;
	M_ActivateMenu(newmenu);
}

// The one sanctioned path to I_OpenURL from menudef/CCMD/a mod: shows the FULL URL and opens it on Yes.
void M_ConfirmOpenURL(const char *url)
{
	// No footer -- the caption already says it opens in the browser, so a bottom line would just echo it.
	M_StartOpenURLMenu(url, "Open this link in your web browser?", NULL);
}

// [rc4l] Host platform for the download-asset URL, chosen at compile time. Feeds
// zx::ComputeReleaseDownloadURL so "Download the update" lands on the file for THIS OS.
static zx::ReleasePlatform M_HostReleasePlatform()
{
#if defined(_WIN32)
	return zx::ReleasePlatform::Windows;
#elif defined(__APPLE__)
	return zx::ReleasePlatform::MacOS;
#elif defined(__linux__)
	return zx::ReleasePlatform::Linux;
#else
	return zx::ReleasePlatform::Unknown;
#endif
}

// [rc4l] Confirm-and-open the OS-correct download for a release `tag` (e.g. "v0.1.19"). The update
// check supplies the tag; here we build the direct asset URL for the running platform and show it.
void M_ConfirmDownloadRelease(const char *tag)
{
	char url[512];
	if (!zx::ComputeReleaseDownloadURL(url, sizeof url, "https://github.com/rc4l/ZandroX", tag,
			M_HostReleasePlatform()))
	{
		Printf("download: could not build a release URL for tag '%s'\n", tag != NULL ? tag : "(null)");
		return;
	}

	// Caption compares the running build to the offered release, e.g. "v0.1.18  ->  v0.1.19".
	// GIT_DESCRIPTION is a git-describe like "v0.1.18-37-g..." (Zandronum's GetGitDescription() returns
	// the hg hash instead, so use the macro release.yml versions from); show just its clean tag.
	char current[64];
	FString caption = "A new ZandroX version is available!";
	if (zx::ExtractVersionTag(GIT_DESCRIPTION, current, sizeof current))
		caption.AppendFormat("\n%s  ->  %s", current, tag);

	M_StartOpenURLMenu(url, caption.GetChars(), "This will open a link in your web browser.");
}

CCMD(openurl)
{
	if (argv.argc() < 2)
	{
		Printf("usage: openurl <http(s)-url>\n");
		return;
	}
	M_ConfirmOpenURL(argv[1]);
}

CCMD(download_release)
{
	if (argv.argc() < 2)
	{
		Printf("usage: download_release <tag>   (e.g. download_release v0.1.19)\n");
		return;
	}
	M_ConfirmDownloadRelease(argv[1]);
}

