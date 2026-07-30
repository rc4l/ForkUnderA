/*
** gi.cpp
** Holds same game-dependant info
**
**---------------------------------------------------------------------------
** Copyright 1998-2006 Randy Heit
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

#include <stdlib.h>
#include "info.h"
#include "gi.h"
#include "m_fixed.h"
#include "v_palette.h"
#include "sc_man.h"
#include "w_wad.h"
#include "i_system.h"
#include "v_video.h"
#include "g_level.h"

gameinfo_t gameinfo;

// [ZandroX] uzdoom@a1cc548af: normforwardmove/normsidemove GAMEINFO keys set
// these console-turbo movement defaults (defined in g_game.cpp).
extern float normforwardmove[2], normsidemove[2];

const char *GameNames[17] =
{
	NULL, "Doom", "Heretic", NULL, "Hexen", NULL, NULL, NULL, "Strife", NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Chex"
};


static gameborder_t DoomBorder =
{
	8, 8,
	"brdr_tl", "brdr_t", "brdr_tr",
	"brdr_l",			 "brdr_r",
	"brdr_bl", "brdr_b", "brdr_br"
};

static gameborder_t HereticBorder =
{
	4, 16,
	"bordtl", "bordt", "bordtr",
	"bordl",           "bordr",
	"bordbl", "bordb", "bordbr"
};

static gameborder_t StrifeBorder =
{
	8, 8,
	"brdr_tl", "brdr_t", "brdr_tr",
	"brdr_l",			 "brdr_r",
	"brdr_bl", "brdr_b", "brdr_br"
};

// Custom GAMEINFO ------------------------------------------------------------

const char* GameInfoBorders[] =
{
	"DoomBorder",
	"HereticBorder",
	"StrifeBorder",
	NULL
};

#define GAMEINFOKEY_CSTRING(key, variable, length) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		if(strlen(sc.String) > length) \
		{ \
			sc.ScriptError("Value for '%s' can not be longer than %d characters.", #key, length); \
		} \
		strcpy(gameinfo.key, sc.String); \
	}

#define GAMEINFOKEY_STRINGARRAY(key, variable, length, clear) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		if (clear) gameinfo.key.Clear(); \
		do \
		{ \
			sc.MustGetToken(TK_StringConst); \
			if(length > 0 && strlen(sc.String) > length) \
			{ \
				sc.ScriptError("Value for '%s' can not be longer than %d characters.", #key, length); \
			} \
			gameinfo.key[gameinfo.key.Reserve(1)] = sc.String; \
		} \
		while (sc.CheckToken(',')); \
	}

#define GAMEINFOKEY_STRING(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		gameinfo.key = sc.String; \
	}

#define GAMEINFOKEY_INT(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetNumber(); \
 		gameinfo.key = sc.Number; \
	}

#define GAMEINFOKEY_FLOAT(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetFloat(); \
		gameinfo.key = static_cast<float> (sc.Float); \
	}

#define GAMEINFOKEY_FIXED(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetFloat(); \
		gameinfo.key = static_cast<int> (sc.Float*double(FRACUNIT)); \
	}

#define GAMEINFOKEY_COLOR(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		FString color = sc.String; \
		FString colorName = V_GetColorStringByName(color); \
		if(!colorName.IsEmpty()) \
			color = colorName; \
		gameinfo.key = V_GetColorFromString(NULL, color); \
	}

#define GAMEINFOKEY_BOOL(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		if(sc.CheckToken(TK_False)) \
			gameinfo.key = false; \
		else \
		{ \
			sc.MustGetToken(TK_True); \
			gameinfo.key = true; \
		} \
	}

#define GAMEINFOKEY_FONT(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		gameinfo.key.fontname = sc.String; \
		if (sc.CheckToken(',')) { \
			sc.MustGetToken(TK_StringConst); \
			gameinfo.key.color = sc.String; \
		} else { \
			gameinfo.key.color = NAME_None; \
		} \
	}

#define GAMEINFOKEY_PATCH(key, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		gameinfo.key.fontname = sc.String; \
		gameinfo.key.color = NAME_Null; \
	}

#define GAMEINFOKEY_MUSIC(key, order, variable) \
	else if(nextKey.CompareNoCase(variable) == 0) \
	{ \
		sc.MustGetToken(TK_StringConst); \
		gameinfo.order = 0; \
		char *colon = strchr (sc.String, ':'); \
		if (colon) \
		{ \
			gameinfo.order = atoi(colon+1); \
			*colon = 0; \
		} \
		gameinfo.key = sc.String; \
	}


// [rc4l] Known-but-unhandled GAMEINFO keys (see the equivalent map manifest in g_mapinfo.cpp).
// Consulted at the "ignore unknown keys" fallback so these are classified with provenance instead
// of silently dropped. PARSE-ONLY = benign value inert on this base; NOT-PORTABLE = needs the
// ZScript VM / a menu path we don't share.
namespace
{
	enum ZXGiClass { ZXGI_PARSEONLY, ZXGI_UNSUPPORTED };
	struct ZXUnhandledGameInfoKey { const char *name; ZXGiClass cls; const char *sha; };
	const ZXUnhandledGameInfoKey ZXUnhandledGameInfoKeys[] =
	{
		// PARSE-ONLY (menu/statscreen/renderer values inert here)
		{ "usepausestring",			ZXGI_PARSEONLY,   "c98042ed0" },
		{ "cheatKey",				ZXGI_PARSEONLY,   "a1cc548af" },
		{ "easyKey",				ZXGI_PARSEONLY,   "a1cc548af" },
		{ "menuslidercolor",		ZXGI_PARSEONLY,   "a1cc548af" },
		{ "menusliderbackcolor",	ZXGI_PARSEONLY,   "a2b8ad79e" },
		{ "statscreen_authorfont",	ZXGI_PARSEONLY,   "3e9921696" },
		{ "statscreen_contentfont",	ZXGI_PARSEONLY,   "2fd170b06" },
		{ "bloodsplatdecaldistance",ZXGI_PARSEONLY,   "47f6f4cb1" },
		{ "bluramount",				ZXGI_PARSEONLY,   "a1cc548af" },
		{ "forcenogfxsubstitution",	ZXGI_PARSEONLY,   "ba13a540e" },
		{ "forcetextinmenus",		ZXGI_PARSEONLY,   "2874a36fb" },
		// NOT-PORTABLE (ZScript class references / VM event handlers)
		{ "statusbarclass",			ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "althudclass",			ZXGI_UNSUPPORTED, "a7f2df4fe" },
		{ "MessageBoxClass",		ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "HelpMenuClass",			ZXGI_UNSUPPORTED, "58e66f480" },
		{ "MenuDelegateClass",		ZXGI_UNSUPPORTED, "58e66f480" },
		{ "defaultconversationmenuclass",ZXGI_UNSUPPORTED,"a1cc548af" },
		{ "eventhandlers",			ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "addeventhandlers",		ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "statscreen_single",		ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "statscreen_coop",		ZXGI_UNSUPPORTED, "a1cc548af" },
		{ "statscreen_dm",			ZXGI_UNSUPPORTED, "a1cc548af" },
	};

	bool ZX_ReportUnhandledGameInfo(FScanner &sc, const char *key)
	{
		for (const ZXUnhandledGameInfoKey &k : ZXUnhandledGameInfoKeys)
		{
			if (stricmp(key, k.name) != 0) continue;
			static TArray<FName> logged;
			FName fn(key);
			for (unsigned i = 0; i < logged.Size(); i++) if (logged[i] == fn) return true;
			logged.Push(fn);
			if (k.cls == ZXGI_PARSEONLY)
				sc.ScriptMessage("GAMEINFO '%s' parsed but not yet wired in this port (uzdoom@%s)\n", k.name, k.sha);
			else
				sc.ScriptMessage("GAMEINFO '%s' not supported in this port (uzdoom@%s)\n", k.name, k.sha);
			return true;
		}
		return false;
	}
}

void FMapInfoParser::ParseGameInfo()
{
	sc.MustGetToken('{');
	while(sc.GetToken())
	{
		if (sc.TokenType == '}') return;

		sc.TokenMustBe(TK_Identifier);
		FString nextKey = sc.String;
		sc.MustGetToken('=');

		if (nextKey.CompareNoCase("weaponslot") == 0)
		{
			sc.MustGetToken(TK_IntConst);
			if (sc.Number < 0 || sc.Number >= 10)
			{
				sc.ScriptError("Weapon slot index must be in range [0..9].\n");
			}
			int i = sc.Number;
			gameinfo.DefaultWeaponSlots[i].Clear();
			sc.MustGetToken(',');
			do
			{
				sc.MustGetString();
				FName val = sc.String;
				gameinfo.DefaultWeaponSlots[i].Push(val);

			}
			while (sc.CheckToken(','));
		}
		else if(nextKey.CompareNoCase("border") == 0)
		{
			if(sc.CheckToken(TK_Identifier))
			{
				switch(sc.MustMatchString(GameInfoBorders))
				{
					default:
						gameinfo.border = &DoomBorder;
						break;
					case 1:
						gameinfo.border = &HereticBorder;
						break;
					case 2:
						gameinfo.border = &StrifeBorder;
						break;
				}
			}
			else
			{
				// border = {size, offset, tr, t, tl, r, l ,br, b, bl};
				char *graphics[8] = {DoomBorder.tr, DoomBorder.t, DoomBorder.tl, DoomBorder.r, DoomBorder.l, DoomBorder.br, DoomBorder.b, DoomBorder.bl};
				sc.MustGetToken(TK_IntConst);
				DoomBorder.offset = sc.Number;
				sc.MustGetToken(',');
				sc.MustGetToken(TK_IntConst);
				DoomBorder.size = sc.Number;
				for(int i = 0;i < 8;i++)
				{
					sc.MustGetToken(',');
					sc.MustGetToken(TK_StringConst);
					int len = int(strlen(sc.String));
					if(len > 8)
						sc.ScriptError("Border graphic can not be more than 8 characters long.\n");
					memcpy(graphics[i], sc.String, len);
					if(len < 8) // end with a null byte if the string is less than 8 chars.
						graphics[i][len] = 0;
				}
			}
		}
		else if(nextKey.CompareNoCase("armoricons") == 0)
		{
			sc.MustGetToken(TK_StringConst);
			strncpy(gameinfo.ArmorIcon1, sc.String, 8);
			gameinfo.ArmorIcon1[8] = 0;
			if (sc.CheckToken(','))
			{
				sc.MustGetToken(TK_FloatConst);
				gameinfo.Armor2Percent = FLOAT2FIXED(sc.Float);
				sc.MustGetToken(',');
				sc.MustGetToken(TK_StringConst);
				strncpy(gameinfo.ArmorIcon2, sc.String, 8);
				gameinfo.ArmorIcon2[8] = 0;
			}
		}
		else if(nextKey.CompareNoCase("maparrow") == 0)
		{
			sc.MustGetToken(TK_StringConst);
			gameinfo.mMapArrow = sc.String;
			if (sc.CheckToken(','))
			{
				sc.MustGetToken(TK_StringConst);
				gameinfo.mCheatMapArrow = sc.String;
			}
			else gameinfo.mCheatMapArrow = "";
		}
		// [ZandroX] uzdoom@a1cc548af: default walk/run movement speeds.
		else if(nextKey.CompareNoCase("normforwardmove") == 0)
		{
			sc.MustGetToken(TK_IntConst);
			normforwardmove[0] = float(sc.Number);
			sc.MustGetToken(',');
			sc.MustGetToken(TK_IntConst);
			normforwardmove[1] = float(sc.Number);
		}
		else if(nextKey.CompareNoCase("normsidemove") == 0)
		{
			sc.MustGetToken(TK_IntConst);
			normsidemove[0] = float(sc.Number);
			sc.MustGetToken(',');
			sc.MustGetToken(TK_IntConst);
			normsidemove[1] = float(sc.Number);
		}
		// [AK] Adds or removes a set of custom data that will be used by a custom column.
		else if (( nextKey.CompareNoCase( "addcustomdata" ) == 0 ) || ( nextKey.CompareNoCase( "removecustomdata" ) == 0 ))
		{
			const bool bAddingData = ( nextKey.CompareNoCase( "addcustomdata" ) == 0 );
			sc.MustGetToken( TK_StringConst );

			if ( sc.StringLen == 0 )
				sc.ScriptError( "Got an empty string for a name." );

			FName Name = sc.String;

			if ( bAddingData )
			{
				// [AK] Don't allow the same data to be defined more than once.
				if ( gameinfo.CustomPlayerData.CheckKey( Name ) != NULL )
					sc.ScriptError( "Custom data '%s' is already defined.", Name.GetChars( ));

				sc.MustGetToken( ',' );
				gameinfo.CustomPlayerData.Insert( Name, PlayerData( sc, gameinfo.CustomPlayerData.CountUsed( )));
			}
			else
			{
				// [AK] Make sure that the data is already defined.
				if ( gameinfo.CustomPlayerData.CheckKey( Name ) == NULL )
					sc.ScriptError( "Custom data '%s' isn't defined.", Name.GetChars( ));

				gameinfo.CustomPlayerData.Remove( Name );
			}
		}
		// Insert valid keys here.
		GAMEINFOKEY_CSTRING(titlePage, "titlePage", 8)
		GAMEINFOKEY_STRINGARRAY(creditPages, "addcreditPage", 8, false)
		GAMEINFOKEY_STRINGARRAY(creditPages, "CreditPage", 8, true)
		GAMEINFOKEY_STRINGARRAY(PlayerClasses, "addplayerclasses", 0, false)
		GAMEINFOKEY_STRINGARRAY(PlayerClasses, "playerclasses", 0, true)
		GAMEINFOKEY_MUSIC(titleMusic, titleOrder, "titleMusic")
		GAMEINFOKEY_FLOAT(titleTime, "titleTime")
		GAMEINFOKEY_FLOAT(advisoryTime, "advisoryTime")
		GAMEINFOKEY_FLOAT(pageTime, "pageTime")
		GAMEINFOKEY_STRING(chatSound, "chatSound")
		GAMEINFOKEY_MUSIC(finaleMusic, finaleOrder, "finaleMusic")
		GAMEINFOKEY_CSTRING(finaleFlat, "finaleFlat", 8)
		GAMEINFOKEY_STRINGARRAY(finalePages, "finalePage", 8, true)
		GAMEINFOKEY_STRINGARRAY(infoPages, "addinfoPage", 8, false)
		GAMEINFOKEY_STRINGARRAY(infoPages, "infoPage", 8, true)
		GAMEINFOKEY_CSTRING(PauseSign, "pausesign", 8)
		GAMEINFOKEY_STRING(quitSound, "quitSound")
		GAMEINFOKEY_CSTRING(borderFlat, "borderFlat", 8)
		GAMEINFOKEY_FIXED(telefogheight, "telefogheight")
		GAMEINFOKEY_FIXED(gibfactor, "gibfactor")
		GAMEINFOKEY_INT(defKickback, "defKickback")
		GAMEINFOKEY_CSTRING(SkyFlatName, "SkyFlatName", 8)
		GAMEINFOKEY_STRING(translator, "translator")
		GAMEINFOKEY_COLOR(pickupcolor, "pickupcolor")
		GAMEINFOKEY_COLOR(defaultbloodcolor, "defaultbloodcolor")
		GAMEINFOKEY_COLOR(defaultbloodparticlecolor, "defaultbloodparticlecolor")
		GAMEINFOKEY_STRING(backpacktype, "backpacktype")
		GAMEINFOKEY_STRING(statusbar, "statusbar")
		GAMEINFOKEY_MUSIC(intermissionMusic, intermissionOrder, "intermissionMusic")
		GAMEINFOKEY_STRING(CursorPic, "CursorPic")
		GAMEINFOKEY_BOOL(noloopfinalemusic, "noloopfinalemusic")
		GAMEINFOKEY_BOOL(drawreadthis, "drawreadthis")
		GAMEINFOKEY_BOOL(swapmenu, "swapmenu")
		GAMEINFOKEY_BOOL(intermissioncounter, "intermissioncounter")
		GAMEINFOKEY_BOOL(hidepartimes, "hidepartimes")
		GAMEINFOKEY_BOOL(dontcrunchcorpses, "dontcrunchcorpses")
		GAMEINFOKEY_BOOL(nightmarefast, "nightmarefast")
		GAMEINFOKEY_COLOR(dimcolor, "dimcolor")
		GAMEINFOKEY_FLOAT(dimamount, "dimamount")
		GAMEINFOKEY_INT(definventorymaxamount, "definventorymaxamount")
		GAMEINFOKEY_INT(defaultrespawntime, "defaultrespawntime")
		GAMEINFOKEY_INT(defaultrespawntime, "defaultrespawntime")
		GAMEINFOKEY_INT(defaultdropstyle, "defaultdropstyle")
		GAMEINFOKEY_CSTRING(Endoom, "endoom", 8)
		GAMEINFOKEY_INT(player5start, "player5start")
		GAMEINFOKEY_STRINGARRAY(quitmessages, "addquitmessages", 0, false)
		GAMEINFOKEY_STRINGARRAY(quitmessages, "quitmessages", 0, true)
		GAMEINFOKEY_STRING(mTitleColor, "menufontcolor_title")
		GAMEINFOKEY_STRING(mFontColor, "menufontcolor_label")
		GAMEINFOKEY_STRING(mFontColorValue, "menufontcolor_value")
		GAMEINFOKEY_STRING(mFontColorMore, "menufontcolor_action")
		GAMEINFOKEY_STRING(mFontColorHeader, "menufontcolor_header")
		GAMEINFOKEY_STRING(mFontColorHighlight, "menufontcolor_highlight")
		GAMEINFOKEY_STRING(mFontColorSelection, "menufontcolor_selection")
		GAMEINFOKEY_CSTRING(mBackButton, "menubackbutton", 8)
		GAMEINFOKEY_INT(TextScreenX, "textscreenx")
		GAMEINFOKEY_INT(TextScreenY, "textscreeny")
		GAMEINFOKEY_STRING(DefaultEndSequence, "defaultendsequence")
		GAMEINFOKEY_FONT(mStatscreenMapNameFont, "statscreen_mapnamefont")
		GAMEINFOKEY_FONT(mStatscreenFinishedFont, "statscreen_finishedfont")
		GAMEINFOKEY_FONT(mStatscreenEnteringFont, "statscreen_enteringfont")
		GAMEINFOKEY_PATCH(mStatscreenFinishedFont, "statscreen_finishedpatch")
		GAMEINFOKEY_PATCH(mStatscreenEnteringFont, "statscreen_enteringpatch")
		GAMEINFOKEY_BOOL(norandomplayerclass, "norandomplayerclass")
		GAMEINFOKEY_BOOL(forcekillscripts, "forcekillscripts") // [JM] Force kill scripts on thing death. (MF7_NOKILLSCRIPTS overrides.)

		// [AK] Forces actors to trigger GAMEEVENT_ACTOR_SPAWNED or GAMEVENT_ACTOR_DAMAGED unless they
		// enabled the STFL_NOSPAWNEVENTSCRIPT or STFL_NODAMAGEEVENTSCRIPT flags respectively.
		GAMEINFOKEY_BOOL(bForceSpawnEventScripts, "forcespawneventscripts")
		GAMEINFOKEY_BOOL(bForceDamageEventScripts, "forcedamageeventscripts")

		// [TRSR] Allows the triggering of GAMEEVENT_DOMINATION_CONTEST.
		GAMEINFOKEY_BOOL(bAllowDominationContestScripts, "allowdominationcontestscripts")

		else
		{
			// [rc4l] Classify known-but-unhandled UZDoom gameinfo keys with provenance; genuinely
			// unknown keys stay silently ignored as before.
			ZX_ReportUnhandledGameInfo(sc, nextKey);
			// ignore unkown keys.
			sc.UnGet();
			SkipToNext();
		}
	}
}

const char *gameinfo_t::GetFinalePage(unsigned int num) const
{
	if (finalePages.Size() == 0) return "-NOFLAT-";
	else if (num < 1 || num > finalePages.Size()) return finalePages[0];
	else return finalePages[num-1];
}
