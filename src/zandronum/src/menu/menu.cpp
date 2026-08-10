/*
** menu.cpp
** Menu base class and global interface
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

#include "doomdef.h"
#include "doomstat.h"
#include "c_dispatch.h"
#include "d_gui.h"
#include "d_player.h"
#include "g_level.h"
#include "c_console.h"
#include "c_bind.h"
#include "s_sound.h"
#include "p_tick.h"
#include "g_game.h"
#include "c_cvars.h"
#include "d_event.h"
#include "v_video.h"
#include "hu_stuff.h"
#include "gi.h"
#include "v_palette.h"
#include "i_input.h"
#include "gameconfigfile.h"
#include "gstrings.h"
#include "r_utility.h"
#include "menu/menu.h"
#include "features/crashreport/zx_crashreport.h"
#include "textures/textures.h"
// [BB] New #includes.
#include "chat.h"
#include "d_netinf.h"
#include "campaign.h"
#include "team.h"
#include "cooperative.h"
#include "deathmatch.h"
#include "cl_main.h"
#include "cl_demo.h"
#include "cl_commands.h"
#include "network/cl_auth.h"
#include "features/server-browser/zx_joinserver.h" // [rc4l] a finished download redirects to the browser
#include "features/server-hosting/zx_hosting.h" // [rc4l] starting a game closes a server we are running
#include "features/global-header/zx_globalheader.h" // [rc4l] the tab bar drawn over every menu

//
// Todo: Move these elsewhere
//
CVAR (Float, mouse_sensitivity, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool, show_messages, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool, show_obituaries, true, CVAR_ARCHIVE)


CVAR (Float, snd_menuvolume, 0.6f, CVAR_ARCHIVE)
CVAR(Int, m_use_mouse, 1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Int, m_show_backbutton, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

DMenu *DMenu::CurrentMenu;
int DMenu::MenuTime;

FGameStartup GameStartupInfo;
EMenuState		menuactive;
bool			M_DemoNoPlay;
FButtonStatus	MenuButtons[NUM_MKEYS];
int				MenuButtonTickers[NUM_MKEYS];
bool			MenuButtonOrigin[NUM_MKEYS];
int				BackbuttonTime;
fixed_t			BackbuttonAlpha;
static bool		MenuEnabled = true;

// [AK] Are we in the server setup menu?
static DMenu	*ServerSetupMenu = NULL;
static bool		ServerMenuEnabled = false;

#define KEY_REPEAT_DELAY	(TICRATE*5/12)
#define KEY_REPEAT_RATE		(3)

//============================================================================
//
// DMenu base class
//
//============================================================================

IMPLEMENT_POINTY_CLASS (DMenu)
	DECLARE_POINTER(mParentMenu)
END_POINTERS

DMenu::DMenu(DMenu *parent) 
{
	mParentMenu = parent;
	mMouseCapture = false;
	mBackbuttonSelected = false;
	GC::WriteBarrier(this, parent);
}
	
bool DMenu::Responder (event_t *ev) 
{ 
	bool res = false;
	if (ev->type == EV_GUI_Event)
	{
		if (ev->subtype == EV_GUI_LButtonDown)
		{
			res = MouseEventBack(MOUSE_Click, ev->data1, ev->data2);
			// make the menu's mouse handler believe that the current coordinate is outside the valid range
			if (res) ev->data2 = -1;	
			res |= MouseEvent(MOUSE_Click, ev->data1, ev->data2);
			if (res)
			{
				SetCapture();
			}
			
		}
		else if (ev->subtype == EV_GUI_RButtonDown)
		{
			res = MouseEventBack(MOUSE_Click2, ev->data1, ev->data2);
			// make the menu's mouse handler believe that the current coordinate is outside the valid range
			if (res) ev->data2 = -1;
			res |= MouseEvent(MOUSE_Click2, ev->data1, ev->data2);
			if (res)
			{
				SetCapture();
			}
		}
		else if (ev->subtype == EV_GUI_MouseMove)
		{
			BackbuttonTime = BACKBUTTON_TIME;
			if (mMouseCapture || m_use_mouse == 1)
			{
				res = MouseEventBack(MOUSE_Move, ev->data1, ev->data2);
				if (res) ev->data2 = -1;	
				res |= MouseEvent(MOUSE_Move, ev->data1, ev->data2);
			}
		}
		else if (ev->subtype == EV_GUI_LButtonUp)
		{
			if (mMouseCapture)
			{
				ReleaseCapture();
				res = MouseEventBack(MOUSE_Release, ev->data1, ev->data2);
				if (res) ev->data2 = -1;	
				res |= MouseEvent(MOUSE_Release, ev->data1, ev->data2);
			}
		}
		else if (ev->subtype == EV_GUI_RButtonUp)
		{
			if (mMouseCapture)
			{
				ReleaseCapture();
				res = MouseEventBack(MOUSE_Release2, ev->data1, ev->data2);
				if (res) ev->data2 = -1;
				res |= MouseEvent(MOUSE_Release2, ev->data1, ev->data2);
			}
		}
	}
	return false;
}

//=============================================================================
//
//
//
//=============================================================================

bool DMenu::MenuEvent (int mkey, bool fromcontroller)
{
	switch (mkey)
	{
	case MKEY_Back:
	{
		Close();
		S_Sound (CHAN_VOICE | CHAN_UI, 
			DMenu::CurrentMenu != NULL? "menu/backup" : "menu/clear", snd_menuvolume, ATTN_NONE);

		return true;
	}
	}
	return false;
}

//=============================================================================
//
//
//
//=============================================================================

void DMenu::Close ()
{
	assert(DMenu::CurrentMenu == this);

	// [AK] Check if we're closing the server setup menu.
	if ( DMenu::CurrentMenu == ServerSetupMenu )
	{
		ServerSetupMenu = NULL;
		ServerMenuEnabled = false;
	}

	DMenu::CurrentMenu = mParentMenu;
	Destroy();
	if (DMenu::CurrentMenu != NULL)
	{
		GC::WriteBarrier(DMenu::CurrentMenu);
	}
	else
	{
		M_ClearMenus ();
	}
}

//=============================================================================
//
//
//
//=============================================================================

bool DMenu::MouseEvent(int type, int x, int y)
{
	return true;
}

//=============================================================================
//
// [rc4l] Menus opt IN to handing Up to the global tab bar. Silence means "I keep my arrows".
//
//=============================================================================

bool DMenu::AtTopRow()
{
	return false;
}

//=============================================================================
//
//
//
//=============================================================================

//=============================================================================
//
// [rc4l] Where the back button's top-left corner goes, in real pixels.
//
// Its home is the top-left of the screen, and the global header now owns that corner: the arrows sat
// on the bar, or in the band of game above it, depending on the window's shape. The bar is chrome
// that spans the whole width, so the button moves out from under it rather than fighting it, and
// takes the same padding off that edge that it now takes off the left one.
//
// Shared by the drawing and the hit test on purpose. They were already two copies of this sum, and
// a back button you can see but not click is the classic result of letting them drift.
static void M_BackButtonOrigin(int w, int h, int &x, int &y)
{
	const int pad = 2 * CleanXfac;

	x = (m_show_backbutton & 1) ? screen->GetWidth() - w : pad;
	y = (m_show_backbutton & 2) ? screen->GetHeight() - h
		: zx::GlobalHeader_ScreenBottom() + 2 * CleanYfac;
}

bool DMenu::MouseEventBack(int type, int x, int y)
{
	if (m_show_backbutton >= 0)
	{
		FTexture *tex = TexMan(gameinfo.mBackButton);
		if (tex != NULL)
		{
			const int w = tex->GetScaledWidth() * CleanXfac;
			const int h = tex->GetScaledHeight() * CleanYfac;

			int ox, oy;
			M_BackButtonOrigin(w, h, ox, oy);

			mBackbuttonSelected = (x >= ox && x < ox + w && y >= oy && y < oy + h);
			if (mBackbuttonSelected && type == MOUSE_Release)
			{
				if (m_use_mouse == 2) mBackbuttonSelected = false;
				MenuEvent(MKEY_Back, true);
			}
			return mBackbuttonSelected;
		}
	}
	return false;
}

//=============================================================================
//
//
//
//=============================================================================

void DMenu::SetCapture()
{
	if (!mMouseCapture)
	{
		mMouseCapture = true;
		I_SetMouseCapture();
	}
}

void DMenu::ReleaseCapture()
{
	if (mMouseCapture)
	{
		mMouseCapture = false;
		I_ReleaseMouseCapture();
	}
}

//=============================================================================
//
//
//
//=============================================================================

void DMenu::Ticker () 
{
}

void DMenu::Drawer () 
{
	if (this == DMenu::CurrentMenu && BackbuttonAlpha > 0 && m_show_backbutton >= 0 && m_use_mouse)
	{
		FTexture *tex = TexMan(gameinfo.mBackButton);
		int w = tex->GetScaledWidth() * CleanXfac;
		int h = tex->GetScaledHeight() * CleanYfac;
		int x, y;
		M_BackButtonOrigin(w, h, x, y);
		if (mBackbuttonSelected && (mMouseCapture || m_use_mouse == 1))
		{
			screen->DrawTexture(tex, x, y, DTA_CleanNoMove, true, DTA_ColorOverlay, MAKEARGB(40, 255,255,255), TAG_DONE);
		}
		else
		{
			screen->DrawTexture(tex, x, y, DTA_CleanNoMove, true, DTA_Alpha, BackbuttonAlpha, TAG_DONE);
		}
	}
}

bool DMenu::DimAllowed()
{
	return true;
}

bool DMenu::TranslateKeyboardEvents()
{
	return true;
}

//=============================================================================
//
// [rc4l] DEnterKey moved here from optionmenuitems.h so freeform menus (and
// everything else) can see it via menu.h. Bodies verbatim from Q-Zandronum.
//
//=============================================================================

IMPLEMENT_ABSTRACT_CLASS(DEnterKey)

DEnterKey::DEnterKey(DMenu *parent, int *keyptr)
 : DMenu(parent)
{
	pKey = keyptr;
	SetMenuMessage(1);
	menuactive = MENU_WaitKey;	// There should be a better way to disable GUI capture...
}

bool DEnterKey::TranslateKeyboardEvents()
{
	return false;
}

void DEnterKey::SetMenuMessage(int which)
{
	if (mParentMenu->IsKindOf(RUNTIME_CLASS(DOptionMenu)))
	{
		DOptionMenu *m = barrier_cast<DOptionMenu*>(mParentMenu);
		FListMenuItem *it = m->GetItem(NAME_Controlmessage);
		if (it != NULL)
		{
			it->SetValue(0, which);
		}
	}
}

bool DEnterKey::Responder(event_t *ev)
{
	if (ev->type == EV_KeyDown)
	{
		*pKey = ev->data1;
		menuactive = MENU_On;
		SetMenuMessage(0);
		Close();
		mParentMenu->MenuEvent((ev->data1 == KEY_ESCAPE)? MKEY_Abort : MKEY_Input, 0);
		return true;
	}
	return false;
}

void DEnterKey::Drawer()
{
	mParentMenu->Drawer();
}

//=============================================================================
//
//
//
//=============================================================================

// [rc4l] Let go of every menu key the framework thinks is held.
//
// M_Responder latches a key on the way down and unlatches it on the way up, and M_Ticker repeats
// whatever is still latched. A menu that turns TranslateKeyboardEvents off mid-press -- which is what
// happens the moment focus lands in the server browser's search box -- never gets the matching
// release, because the release is no longer being translated. The button then repeats forever and
// shoves the focus around while the player is trying to type.
//
// Opening the menu already did exactly this loop for the same reason; it just needed a name so
// anything taking the keyboard can say so.
void M_ReleaseMenuButtons ()
{
	for (int i = 0; i < NUM_MKEYS; ++i)
	{
		MenuButtons[i].ReleaseKey(0);
		MenuButtonTickers[i] = 0;
	}
}

bool M_StartControlPanel (bool makeSound)
{
	// intro might call this repeatedly
	if (DMenu::CurrentMenu != NULL)
		return false;

	ResetButtonStates ();
	M_ReleaseMenuButtons ();

	C_HideConsole ();				// [RH] Make sure console goes bye bye.
	menuactive = MENU_On;
	// Pause sound effects before we play the menu switch sound.
	// That way, it won't be paused.
	P_CheckTickerPaused ();

	if (makeSound)
	{
		S_Sound (CHAN_VOICE | CHAN_UI, "menu/activate", snd_menuvolume, ATTN_NONE);
	}

	// [rc4l] A download finished while the player was elsewhere, so its join is waiting. The band on
	// screen says "Open the Menu", and this is opening the menu -- so it goes where the band
	// promised, rather than to a main menu the player then has to navigate out of.
	//
	// It used to hang off MKEY_Back instead, which acted on the OPPOSITE gesture to the one being
	// asked for: the band said open, and the jump happened when you backed out. Backing out is the
	// one input that reliably means "I am leaving this", and giving it a second, invisible meaning
	// is how Escape stopped doing what Escape does.
	// [rc4l] Two reasons to open somewhere other than the main menu, and they answer the same way: a
	// join that finished while the player was in the game, or a browser they were in when they left.
	if (zx::ConsumeJoinReady() || zx::GlobalHeader_ResumeBrowser())
	{
		M_SetMenu("ZA_Browser", -1);
		return true;
	}
	BackbuttonTime = 0;
	BackbuttonAlpha = 0;

	// [BB] Don't change the displayed menu status when a demo is played.
	if ( CLIENTDEMO_IsPlaying( ) == false )
		PLAYER_SetStatus( &players[consoleplayer], PLAYERSTATUS_INMENU, true, SETPLAYERSTATUS_CLIENTSENDSUPDATE );

	return false;
}

//=============================================================================
//
//
//
//=============================================================================

void M_ActivateMenu(DMenu *menu)
{
	if (menuactive == MENU_Off) menuactive = MENU_On;
	if (DMenu::CurrentMenu != NULL) DMenu::CurrentMenu->ReleaseCapture();
	DMenu::CurrentMenu = menu;
	GC::WriteBarrier(DMenu::CurrentMenu);
}

//=============================================================================
//
//
//
//=============================================================================

void M_SetMenu(FName menu, int param)
{
	// some menus need some special treatment
	switch (menu)
	{
	case NAME_Episodemenu:
		// sent from the player class menu
		GameStartupInfo.Skill = -1;
		GameStartupInfo.Episode = -1;
		GameStartupInfo.PlayerClass = 
			param == -1000? NULL :
			param == -1? "Random" : GetPrintableDisplayName(PlayerClasses[param].Type);
		break;

	case NAME_Skillmenu:
		// sent from the episode menu

		if ((gameinfo.flags & GI_SHAREWARE) && param > 0)
		{
			// Only Doom and Heretic have multi-episode shareware versions.
			M_StartMessage(GStrings("SWSTRING"), 1);
			return;
		}

		GameStartupInfo.Episode = param;

		// [BB] Special handling for bot episodes.
		if ( AllEpisodes[GameStartupInfo.Episode].bBotEpisode )
		{
			FMenuDescriptor **desc = MenuDescriptors.CheckKey ( NAME_BotSkillMenu );
			if ( (desc != NULL) && ( (*desc)->mType == MDESC_ListMenu ) )
			{
				FEpisode *epi = &AllEpisodes[GameStartupInfo.Episode];
				FListMenuDescriptor *ld = static_cast<FListMenuDescriptor*>(*desc);
				// [BB] If we already added a title, we have to delete the old one.
				// This is a very fragile check and assumes that the BotSkillMenu definition
				// of menudef.za wasn't changed and still has 6 entries.
				if ( ld->mItems.Size() == 7 )
					ld->mItems.Pop();

				if ( epi->BotSkillTitle.IsNotEmpty() )
				{
					if ( epi->bBotSkillFullText )
						ld->mItems.Push ( new FListMenuItemStaticText(160, 1, epi->BotSkillTitle, BigFont, CR_RED, true) );
					else
					{
						FTextureID tex = TexMan.CheckForTexture ( epi->BotSkillTitle, FTexture::TEX_MiscPatch );
						ld->mItems.Push ( new FListMenuItemStaticPatch(160, 1, tex, true) );
					}
				}
			}
			M_SetMenu(NAME_BotSkillMenu, -1);
			return;
		}
		else
			M_StartupSkillMenu(&GameStartupInfo);	// needs player class name from class menu (later)
		break;

	case NAME_StartgameConfirm:
	{
		// sent from the skill menu for a skill that needs to be confirmed
		GameStartupInfo.Skill = param;

		const char *msg = AllSkills[param].MustConfirmText;
		if (*msg==0) msg = GStrings("NIGHTMARE");
		M_StartMessage (msg, 0, NAME_StartgameConfirmed);
		return;
	}

	case NAME_Startgame:
		// sent either from skill menu or confirmation screen. Skill gets only set if sent from skill menu
		// Now we can finally start the game. Ugh...
		GameStartupInfo.Skill = param;
	// [BB] Sneak in the bot skill with fall through to avoid copy and paste.
	case NAME_ChooseBotSkill:
		if ( menu == NAME_ChooseBotSkill )
		{
			botskill = param;
			if ( param == 4 )
			{								  
				M_StartMessage ( "are you sure? you'll prolly get\nyour ass whooped!\n\npress y or n.", 0, NAME_StartgameConfirmed );
				return;
			}
		}
	case NAME_StartgameConfirmed:

		// [rc4l] A server of ours is a thing other people are standing in, and starting a game takes
		// it away from them without a word. Everywhere else that ends a hosted server asks first --
		// the HOST tab's STOP button, and now joining someone else's server -- so this asks too.
		//
		// Placed on Confirmed rather than earlier so it is the LAST question, after the skill
		// prompts: being asked about your server and then still having to pick a skill would leave
		// the answer stale by the time anything happened.
		if ( zx::HostCurrentState( ) != zx::HostState::Idle )
		{
			M_StartMessage( "starting a game closes the server\nyou are running, and disconnects\n"
				"anyone playing on it.\n\npress y or n.", 0, NAME_FuaStopHostAndStartgame );
			return;
		}

		// Falls through: the server is gone, so this is now an ordinary start.
	case NAME_FuaStopHostAndStartgame:

		// [rc4l] Stop before the game starts, not after. G_DeferedInitNew below does not come back
		// here, so a stop deferred past it would never run and the server would outlive the menu.
		zx::HostStop( );

		// [BC/BB] Put us back in single player mode, and reset our dmflags.
		{
			UCVarValue	Val;
			NETWORK_SetState( NETSTATE_SINGLE );
			Val.Int = 0;
			dmflags.ForceSet( Val, CVAR_Int );
			dmflags2.ForceSet( Val, CVAR_Int );

			// Assume normal mode for going through the menu.
			Val.Bool = false;

			deathmatch.ForceSet( Val, CVAR_Bool );
			teamgame.ForceSet( Val, CVAR_Bool );
			survival.ForceSet( Val, CVAR_Bool );
			invasion.ForceSet( Val, CVAR_Bool );

			// Turn campaign mode back on.
			CAMPAIGN_EnableCampaign( );
		}

		G_DeferedInitNew (&GameStartupInfo);
		if (gamestate == GS_FULLCONSOLE)
		{
			gamestate = GS_HIDECONSOLE;
			gameaction = ga_newgame;
		}
		M_ClearMenus ();
		return;

	case NAME_Savegamemenu:
		// [BB] !multiplayer -> ( NETWORK_GetState( ) == NETSTATE_SINGLE )
		if (!usergame || (players[consoleplayer].health <= 0 && ( NETWORK_GetState( ) == NETSTATE_SINGLE ))|| gamestate != GS_LEVEL)
		{
			// cannot save outside the game.
			M_StartMessage (GStrings("SAVEDEAD"), 1);
			return;
		}
		break;

	case NAME_ZA_ServerSetupMenu:
		// [TP] Make the server setup menu redirect to RCON login if not logged in yet
		if (( NETWORK_GetState() == NETSTATE_CLIENT ) && ( CLIENT_HasRCONAccess() == false ))
			menu = NAME_ZA_RconLoginMenu;
		break;

	case NAME_ZA_LoginMenu:
		// [AK] Prevent the login menu from opening if the client is already logged in.
		if (( NETWORK_GetState() == NETSTATE_CLIENT ) && ( CLIENT_IsLoggedIn()))
		{
			M_StartMessage( "You are already logged in.\n\npress a key.", 1 );
			return;
		}

#ifdef WIN32
		FBaseCVar *usernameCVar = FindCVar( "menu_authusername", nullptr );

		// [AK] Set the username in the login menu to the client's default username when the menu's opened
		// for the first time (i.e. "menu_authusername" hasn't been changed yet).
		if (( usernameCVar != nullptr ) && ( strlen( usernameCVar->GetGenericRep( CVAR_String ).String ) == 0 ))
		{
			UCVarValue val;
			val.String = login_default_user.GetGenericRep( CVAR_String ).String;
			usernameCVar->SetGenericRep( val, CVAR_String );
		}
#endif

		break;
	}

	// End of special checks

	FMenuDescriptor **desc = MenuDescriptors.CheckKey(menu);
	if (desc != NULL)
	{
		// [BB] netgame -> ( NETWORK_GetState( ) == NETSTATE_CLIENT )
		//
		// [rc4l] Restricted to save/load. This used to refuse ANY descriptor carrying a
		// NetgameMessage, and that is data a MOD owns: a pk3's own MENUDEF replaces ours wholesale,
		// so the flag arrives from whatever the mod inherited or copied, not from a decision that
		// this menu is unsafe online. Total conversions route their own setup through those menus --
		// MM8BDM refuses to let a connected player open its join/class flow and tells them they
		// "can't start a new game", which is neither true nor actionable -- and nothing we ship can
		// fix it, because the mod's definitions win over ours.
		//
		// Saving and loading are the two where a client genuinely cannot proceed: the server owns
		// the game state, so both would fail or desync no matter what the menu says. Those keep the
		// refusal, by NAME, where a mod cannot accidentally opt out of it either.
		//
		// Everything else opens. Opening a menu is harmless; the actions inside it that cannot work
		// as a client are already refused where they are performed -- G_DeferedInitNew is
		// server-driven, and NAME_Savegamemenu has its own SAVEDEAD check above.
		const bool bClientOnlyMenu = ( menu == NAME_Savegamemenu ) || ( menu == NAME_Loadgamemenu );
		if ( bClientOnlyMenu && (*desc)->mNetgameMessage.IsNotEmpty() && ( NETWORK_GetState( ) == NETSTATE_CLIENT ) && !demoplayback)
		{
			M_StartMessage((*desc)->mNetgameMessage, 1);
			return;
		}

		if ((*desc)->mType == MDESC_ListMenu)
		{
			FListMenuDescriptor *ld = static_cast<FListMenuDescriptor*>(*desc);
			if (ld->mAutoselect >= 0 && ld->mAutoselect < (int)ld->mItems.Size())
			{
				// recursively activate the autoselected item without ever creating this menu.
				ld->mItems[ld->mAutoselect]->Activate();
			}
			else
			{
				// [rc4l] Stock: no special-casing for the main menu here. The "update available"
				// chip is drawn by DListMenu itself when the descriptor is MainMenu (see menu.h), so
				// it survives whatever class the descriptor names -- or names none, which is what
				// keeps the main menu replaceable by mods.
				const PClass *cls = ld->mClass == NULL? RUNTIME_CLASS(DListMenu) : ld->mClass;

				DListMenu *newmenu = (DListMenu *)cls->CreateNew();
				newmenu->Init(DMenu::CurrentMenu, ld);
				M_ActivateMenu(newmenu);
			}
		}
		else if ((*desc)->mType == MDESC_FreeformMenu)
		{
			FFreeformMenuDescriptor *ld = static_cast<FFreeformMenuDescriptor*>(*desc);
			const PClass *cls = ld->mClass == NULL? RUNTIME_CLASS(DFreeformMenu) : ld->mClass;

			// [TP]
			if ( ld->mNetgameOnly && ( NETWORK_GetState() != NETSTATE_CLIENT ) )
			{
				M_StartMessage( "You must be in a netgame to use this.\n\npress a key.", 1 );
				return;
			}

			if (ld->mAutoselect >= 0 && ld->mAutoselect < (int)ld->mItems.Size())
			{
				// recursively activate the autoselected item without ever creating this menu.
				ld->mItems[ld->mAutoselect]->Activate();
			}
			else
			{
				DFreeformMenu *newmenu = (DFreeformMenu*)cls->CreateNew();
				newmenu->Init(DMenu::CurrentMenu, ld);
				M_ActivateMenu(newmenu);
			}
		}
		else if ((*desc)->mType == MDESC_OptionsMenu)
		{
			FOptionMenuDescriptor *ld = static_cast<FOptionMenuDescriptor*>(*desc);
			const PClass *cls = ld->mClass == NULL? RUNTIME_CLASS(DOptionMenu) : ld->mClass;

			// [TP]
			if ( ld->mNetgameOnly && ( NETWORK_GetState() != NETSTATE_CLIENT ) )
			{
				M_StartMessage( "You must be in a netgame to use this.\n\npress a key.", 1 );
				return;
			}

			// [AK] Prevent clients without RCON access from opening this menu.
			if ( ld->mRequiresRCON )
			{
				if (( NETWORK_GetState() == NETSTATE_CLIENT ) && ( CLIENT_HasRCONAccess() == false ))
				{
					M_StartMessage( "You must have RCON access to use this menu.\n\npress a key.", 1 );
					return;
				}

				ServerMenuEnabled = true;
			}

			DOptionMenu *newmenu = (DOptionMenu *)cls->CreateNew();
			newmenu->Init(DMenu::CurrentMenu, ld);

			// [AK] Check if we're opening the server setup menu.
			if ( menu == NAME_ZA_ServerSetupMenu )
				ServerSetupMenu = newmenu;

			M_ActivateMenu(newmenu);
		}
		return;
	}
	else
	{
		const PClass *menuclass = PClass::FindClass(menu);
		if (menuclass != NULL)
		{
			if (menuclass->IsDescendantOf(RUNTIME_CLASS(DMenu)))
			{
				DMenu *newmenu = (DMenu*)menuclass->CreateNew();
				newmenu->mParentMenu = DMenu::CurrentMenu;
				M_ActivateMenu(newmenu);
				return;
			}
		}
	}
	Printf("Attempting to open menu of unknown type '%s'\n", menu.GetChars());
}

//=============================================================================
//
//
//
//=============================================================================

bool M_Responder (event_t *ev) 
{ 
	int ch = 0;
	bool keyup = false;
	int mkey = NUM_MKEYS;
	bool fromcontroller = true;

	// [BB] chatmodeon -> CHAT_GetChatMode( )
	if (CHAT_GetChatMode( ))
	{
		return false;
	}

	if (DMenu::CurrentMenu != NULL && menuactive != MENU_Off) 
	{
		// There are a few input sources we are interested in:
		//
		// EV_KeyDown / EV_KeyUp : joysticks/gamepads/controllers
		// EV_GUI_KeyDown / EV_GUI_KeyUp : the keyboard
		// EV_GUI_Char : printable characters, which we want in string input mode
		//
		// This code previously listened for EV_GUI_KeyRepeat to handle repeating
		// in the menus, but that doesn't work with gamepads, so now we combine
		// the multiple inputs into buttons and handle the repetition manually.
		if (ev->type == EV_GUI_Event)
		{
			fromcontroller = false;
			if (ev->subtype == EV_GUI_KeyRepeat)
			{
				// [rc4l] A menu that asked for RAW keys gets the OS's repeats.
				//
				// This returned true unconditionally, so a held key was eaten here and no menu ever
				// saw a repeat. That is right for menu NAVIGATION -- the repetition below is done by
				// hand so gamepads behave like keyboards -- and wrong for a text field, which is not
				// navigating anything and wants exactly what the OS sends. Holding left in the search
				// box or on the hosting form moved the caret one character and then stopped.
				//
				// TranslateKeyboardEvents is already the question "does this menu want raw keys",
				// and a menu answers no only while a field has focus, so this hands repeats to the
				// one case that needs them and changes nothing for every other menu.
				if (!DMenu::CurrentMenu->TranslateKeyboardEvents())
					return DMenu::CurrentMenu->Responder(ev);

				// We do our own key repeat handling but still want to eat the
				// OS's repeated keys.
				return true;
			}
			else if (ev->subtype == EV_GUI_BackButtonDown || ev->subtype == EV_GUI_BackButtonUp)
			{
				mkey = MKEY_Back;
				keyup = ev->subtype == EV_GUI_BackButtonUp;
			}
			else if (ev->subtype != EV_GUI_KeyDown && ev->subtype != EV_GUI_KeyUp)
			{
				// do we want mouse input?
				if (ev->subtype >= EV_GUI_FirstMouseEvent && ev->subtype <= EV_GUI_LastMouseEvent)
				{
						if (!m_use_mouse)
							return true;

						// [rc4l] The tab bar gets first refusal on anything over it. It is drawn on
						// top, so it has to be asked first, or the menu underneath answers for
						// pixels the player can see belong to the bar.
						if (ev->subtype == EV_GUI_MouseMove)
						{
							if (zx::GlobalHeader_MouseMove(ev->data1, ev->data2))
								return true;
						}
						else if (ev->subtype == EV_GUI_LButtonDown)
						{
							if (zx::GlobalHeader_MouseClick(ev->data1, ev->data2))
								return true;
						}
				}

				// pass everything else on to the current menu
				return DMenu::CurrentMenu->Responder(ev);
			}
			else if (DMenu::CurrentMenu->TranslateKeyboardEvents())
			{
				ch = ev->data1;
				keyup = ev->subtype == EV_GUI_KeyUp;
				switch (ch)
				{
				case GK_BACK:			mkey = MKEY_Back;		break;
				case GK_ESCAPE:			mkey = MKEY_Back;		break;
				case GK_RETURN:			mkey = MKEY_Enter;		break;
				case GK_UP:				mkey = MKEY_Up;			break;
				case GK_DOWN:			mkey = MKEY_Down;		break;
				case GK_LEFT:			mkey = MKEY_Left;		break;
				case GK_RIGHT:			mkey = MKEY_Right;		break;
				case GK_BACKSPACE:		mkey = MKEY_Clear;		break;
				case GK_PGUP:			mkey = MKEY_PageUp;		break;
				case GK_PGDN:			mkey = MKEY_PageDown;	break;
				default:
					if (!keyup)
					{
						return DMenu::CurrentMenu->Responder(ev);
					}
					break;
				}
			}
		}
		else if (menuactive != MENU_WaitKey && (ev->type == EV_KeyDown || ev->type == EV_KeyUp))
		{
			keyup = ev->type == EV_KeyUp;

			ch = ev->data1;
			switch (ch)
			{
			case KEY_JOY1:
			case KEY_PAD_A:
				mkey = MKEY_Enter;
				break;

			case KEY_JOY2:
			case KEY_PAD_B:
				mkey = MKEY_Back;
				break;

			case KEY_JOY3:
			case KEY_PAD_X:
				mkey = MKEY_Clear;
				break;

			case KEY_JOY5:
			case KEY_PAD_LSHOULDER:
				mkey = MKEY_PageUp;
				break;

			case KEY_JOY6:
			case KEY_PAD_RSHOULDER:
				mkey = MKEY_PageDown;
				break;

			case KEY_PAD_DPAD_UP:
			case KEY_PAD_LTHUMB_UP:
			case KEY_JOYAXIS1MINUS:
			case KEY_JOYPOV1_UP:
				mkey = MKEY_Up;
				break;

			case KEY_PAD_DPAD_DOWN:
			case KEY_PAD_LTHUMB_DOWN:
			case KEY_JOYAXIS1PLUS:
			case KEY_JOYPOV1_DOWN:
				mkey = MKEY_Down;
				break;

			case KEY_PAD_DPAD_LEFT:
			case KEY_PAD_LTHUMB_LEFT:
			case KEY_JOYAXIS2MINUS:
			case KEY_JOYPOV1_LEFT:
				mkey = MKEY_Left;
				break;

			case KEY_PAD_DPAD_RIGHT:
			case KEY_PAD_LTHUMB_RIGHT:
			case KEY_JOYAXIS2PLUS:
			case KEY_JOYPOV1_RIGHT:
				mkey = MKEY_Right;
				break;
			}
		}

		if (mkey != NUM_MKEYS)
		{
			if (keyup)
			{
				MenuButtons[mkey].ReleaseKey(ch);
				return false;
			}
			else
			{
				MenuButtons[mkey].PressKey(ch);
				MenuButtonOrigin[mkey] = fromcontroller;
				if (mkey <= MKEY_PageDown)
				{
					MenuButtonTickers[mkey] = KEY_REPEAT_DELAY;
				}
				// [rc4l] While the tab bar holds the arrows, the menu underneath sees none of them.
				//
				// That is what having focus means, and routing the key to both would move the bar's
				// cursor and the menu's selection at the same time -- two carets, one keypress.
				// [rc4l] ESCAPE MEANS ESCAPE, and it is dealt with before the bar gets a look in.
				//
				// It used to be one of the bar's cases, which spent the keypress handing the arrows
				// back: a player who had walked up to the bar then had to press Escape twice, once
				// to leave the bar and once to leave the menu. Escape is the one key that must never
				// be absorbed by something the player did not ask to be in, so the bar's focus is
				// dropped here as a side effect and the key carries on to the menu regardless.
				if (mkey == MKEY_Back)
				{
					zx::GlobalHeader_ReleaseFocus();
				}
				else if (zx::GlobalHeader_HasFocus())
				{
					// While the tab bar holds the arrows, the menu underneath sees none of them.
					// That is what having focus means, and routing the key to both would move the
					// bar's cursor and the menu's selection at once: two carets, one keypress.
					bool handled = false;
					switch (mkey)
					{
					case MKEY_Left:		handled = zx::GlobalHeader_NavLeft();	break;
					case MKEY_Right:	handled = zx::GlobalHeader_NavRight();	break;
					case MKEY_Down:		handled = zx::GlobalHeader_NavDown();	break;
					case MKEY_Enter:	handled = zx::GlobalHeader_Activate();	break;

					// Up is already at the top of everything.
					case MKEY_Up:		handled = true;							break;
					default:											break;
					}

					if (handled)
						return true;
				}
				// [rc4l] Up off the top row of a menu is how the keyboard reaches the bar.
				//
				// Asked here rather than inside each menu's Up because the bar sits above ALL of
				// them: putting the question in one place is what stops the answer drifting apart
				// menu by menu, and a mod's own menu inherits the behaviour without knowing.
				//
				// It costs the wrap from the top row to the bottom one, which is the trade every
				// game with a tab strip makes: there is now something above the first row, so Up
				// goes to it.
				else if (mkey == MKEY_Up && DMenu::CurrentMenu->AtTopRow())
				{
					zx::GlobalHeader_TakeFocus();
					S_Sound(CHAN_VOICE | CHAN_UI, "menu/cursor", snd_menuvolume, ATTN_NONE);
					return true;
				}

				DMenu::CurrentMenu->MenuEvent(mkey, fromcontroller);
				return true;
			}
		}
		return DMenu::CurrentMenu->Responder(ev) || !keyup;
	}
	else if (MenuEnabled)
	{
		if (ev->type == EV_KeyDown)
		{
			// Pop-up menu?
			if (ev->data1 == KEY_ESCAPE)
			{
				// [rc4l] ASK whether the panel already routed us, rather than deciding again.
				//
				// Both did, and both consumed the waiting join to find out. M_StartControlPanel got
				// there first and opened the browser, which left the flag clear, so the test here
				// came out false and the main menu was set straight over the top. Escape stopped
				// honouring the "Open the Menu" band it had honoured a moment before, and the cause
				// was never in this branch: it was that the question was asked twice.
				if (!M_StartControlPanel(true))
					M_SetMenu(NAME_Mainmenu, -1);
				return true;
			}
			// If devparm is set, pressing F1 always takes a screenshot no matter
			// what it's bound to. (for those who don't bother to read the docs)
			if (devparm && ev->data1 == KEY_F1)
			{
				G_ScreenShot(NULL);
				return true;
			}
			return false;
		}
		else if (ev->type == EV_GUI_Event && ev->subtype == EV_GUI_LButtonDown && 
				 ConsoleState != c_down && m_use_mouse)
		{
			// [rc4l] A download finished while the player was away, so its join is waiting. Opening
			// the menu is the one deliberate "I am ready to stop playing" gesture there is, so it is
			// what we hang this on -- no new key to bind, nothing to conflict with, and impossible to
			// trigger mid-fight by accident. They land on the list and press JOIN themselves. The
			// panel owns that decision now; this only supplies the default when it did not act.
			if (!M_StartControlPanel(true))
				M_SetMenu(NAME_Mainmenu, -1);
			return true;
		}
	}
	return false;
}

//=============================================================================
//
//
//
//=============================================================================

void M_Ticker (void) 
{
	DMenu::MenuTime++;
	if (DMenu::CurrentMenu != NULL && menuactive != MENU_Off) 
	{
		DMenu::CurrentMenu->Ticker();

		for (int i = 0; i < NUM_MKEYS; ++i)
		{
			if (MenuButtons[i].bDown)
			{
				if (MenuButtonTickers[i] > 0 &&	--MenuButtonTickers[i] <= 0)
				{
					MenuButtonTickers[i] = KEY_REPEAT_RATE;
					DMenu::CurrentMenu->MenuEvent(i, MenuButtonOrigin[i]);
				}
			}
		}
		if (BackbuttonTime > 0)
		{
			if (BackbuttonAlpha < FRACUNIT) BackbuttonAlpha += FRACUNIT/10;
			BackbuttonTime--;
		}
		else
		{
			if (BackbuttonAlpha > 0) BackbuttonAlpha -= FRACUNIT/10;
			if (BackbuttonAlpha < 0) BackbuttonAlpha = 0;
		}
	}
}

//=============================================================================
//
//
//
//=============================================================================

void M_Drawer (void) 
{
	player_t *player = &players[consoleplayer];
	AActor *camera = player->camera;
	PalEntry fade = 0;

	if (!screen->Accel2D && camera != NULL && (gamestate == GS_LEVEL || gamestate == GS_TITLELEVEL))
	{
		if (camera->player != NULL)
		{
			player = camera->player;
		}
		fade = PalEntry (BYTE(player->BlendA*255), BYTE(player->BlendR*255), BYTE(player->BlendG*255), BYTE(player->BlendB*255));
	}


	if (DMenu::CurrentMenu != NULL && menuactive != MENU_Off) 
	{
		if (DMenu::CurrentMenu->DimAllowed())
		{
			screen->Dim(fade);
			V_SetBorderNeedRefresh();
		}
		DMenu::CurrentMenu->Drawer();

		// [rc4l] The global tab bar, drawn AFTER the menu and over every one of them.
		//
		// Here rather than in a menu descriptor because MENUDEF is replaced wholesale by any total
		// conversion, and a route to online play that a mod can delete by accident is not a route.
		// Everything below is shifted down by GlobalHeader_MenuOffsetY() to make room.
		zx::GlobalHeader_Draw();
	}
}

//=============================================================================
//
//
//
//=============================================================================

void M_ClearMenus ()
{
	M_DemoNoPlay = false;

	// [rc4l] Asked BEFORE the teardown, while there is still a menu to ask about: which section the
	// player was in when they left, so the next Escape puts them back there instead of at the main
	// menu. Leaving a screen is not the same as choosing a different one, and being returned
	// somewhere else is a second thing to undo before you are where you already were.
	zx::GlobalHeader_NoteMenusClosing();

	if (DMenu::CurrentMenu != NULL)
	{
		DMenu::CurrentMenu->Destroy();
		DMenu::CurrentMenu = NULL;
	}
	D_SendPendingUserinfoChanges(); // [TP]
	V_SetBorderNeedRefresh();
	menuactive = MENU_Off;

	// [rc4l] The bar does not keep the arrows across a closed menu. Escape has to land on the first
	// row of the main menu every time, and a bar still holding focus from last time would swallow
	// the arrows of a menu the player has only just opened.
	zx::GlobalHeader_ReleaseFocus();

	// [AK] If we're not in a menu, then we're obviously not in the server setup menu.
	ServerSetupMenu = NULL;
	ServerMenuEnabled = false;

	// [BB] Don't change the displayed menu status when a demo is played.
	if ( CLIENTDEMO_IsPlaying( ) == false )
		PLAYER_SetStatus( &players[consoleplayer], PLAYERSTATUS_INMENU, false, SETPLAYERSTATUS_CLIENTSENDSUPDATE );
}

//=============================================================================
//
//
//
//=============================================================================

void M_Init (void) 
{
	M_ParseMenuDefs();
	M_CreateMenus();
}


//=============================================================================
//
//
//
//=============================================================================

void M_EnableMenu (bool on) 
{
	MenuEnabled = on;
}

//=============================================================================
//
// [AK] Returns true if we're in the server setup menu or its submenus.
//
//=============================================================================

bool M_InServerSetupMenu (void)
{
	return ServerMenuEnabled;
}

//=============================================================================
//
// [AK] Returns true if the given name points to a valid menu, or false otherwise.
//
//=============================================================================

bool M_IsValidMenu( const char *name )
{
	if (( name == nullptr ) || ( strlen( name ) == 0 ))
		return false;

	if ( MenuDescriptors.CheckKey( name ) == nullptr )
	{
		const PClass *menuClass = PClass::FindClass( name );

		if (( menuClass == nullptr ) || ( menuClass->IsDescendantOf( RUNTIME_CLASS( DMenu )) == false ))
			return false;
	}

	return true;
}

//=============================================================================
//
// [RH] Most menus can now be accessed directly
// through console commands.
//
//=============================================================================

CCMD (menu_main)
{
	// [rc4l] Same redirect as the escape key: opening the main menu is opening the main menu however
	// the player got there, and a waiting join should not depend on which route they took. Which is
	// why the decision lives in one place and this asks it rather than repeating it.
	if (!M_StartControlPanel(true))
		M_SetMenu(NAME_Mainmenu, -1);
}

CCMD (menu_load)
{	// F3
	M_StartControlPanel (true);
	M_SetMenu(NAME_Loadgamemenu, -1);
}

CCMD (menu_save)
{	// F2
	M_StartControlPanel (true);
	M_SetMenu(NAME_Savegamemenu, -1);
}

CCMD (menu_help)
{	// F1
	M_StartControlPanel (true);
	M_SetMenu(NAME_Readthismenu, -1);
}

CCMD (menu_game)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_Playerclassmenu, -1);	// The playerclass menu is the first in the 'start game' chain
}
								
CCMD (menu_options)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_Optionsmenu, -1);
}

CCMD (menu_player)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_Playermenu, -1);
}

CCMD (menu_messages)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_MessageOptions, -1);
}

CCMD (menu_automap)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_AutomapOptions, -1);
}

CCMD (menu_scoreboard)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_ScoreboardOptions, -1);
}

CCMD (menu_mapcolors)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_MapColorMenu, -1);
}

CCMD (menu_keys)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_CustomizeControls, -1);
}

CCMD (menu_gameplay)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_GameplayOptions, -1);
}

CCMD (menu_compatibility)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_CompatibilityOptions, -1);
}

CCMD (menu_mouse)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_MouseOptions, -1);
}

CCMD (menu_joystick)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_JoystickOptions, -1);
}

CCMD (menu_sound)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_SoundOptions, -1);
}

CCMD (menu_advsound)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_AdvSoundOptions, -1);
}

CCMD (menu_modreplayer)
{
	M_StartControlPanel(true);
	M_SetMenu(NAME_ModReplayerOptions, -1);
}

CCMD (menu_display)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_VideoOptions, -1);
}

CCMD (menu_video)
{
	M_StartControlPanel (true);
	M_SetMenu(NAME_VideoModeMenu, -1);
}



CCMD (openmenu)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: openmenu \"menu_name\"");
		return;
	}
	M_StartControlPanel (true);
	M_SetMenu(argv[1], -1);
}

CCMD (closemenu)
{
	M_ClearMenus();
}

//
//		Toggle messages on/off
//
CCMD (togglemessages)
{
	if (show_messages)
	{
		Printf (128, "%s\n", GStrings("MSGOFF"));
		show_messages = false;
	}
	else
	{
		Printf (128, "%s\n", GStrings("MSGON"));
		show_messages = true;
	}
}

EXTERN_CVAR (Int, screenblocks)

CCMD (sizedown)
{
	screenblocks = screenblocks - 1;
	S_Sound (CHAN_VOICE | CHAN_UI, "menu/change", snd_menuvolume, ATTN_NONE);
}

CCMD (sizeup)
{
	screenblocks = screenblocks + 1;
	S_Sound (CHAN_VOICE | CHAN_UI, "menu/change", snd_menuvolume, ATTN_NONE);
}

CCMD(menuconsole)
{
	M_ClearMenus();
	C_ToggleConsole();
}

CCMD(reset2defaults)
{
	C_SetDefaultBindings ();
	C_SetCVarsToDefaults ();
	R_SetViewSize (screenblocks);
}

CCMD(reset2saved)
{
	GameConfig->DoGlobalSetup ();
	GameConfig->DoGameSetup (gameinfo.ConfigName);
	GameConfig->DoModSetup (gameinfo.ConfigName);
	R_SetViewSize (screenblocks);
}
