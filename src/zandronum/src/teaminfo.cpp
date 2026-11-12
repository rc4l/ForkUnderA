/*
** teaminfo.cpp
** Implementation of the TEAMINFO lump.
**
**---------------------------------------------------------------------------
** Copyright 2007-2008 Christopher Westley
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

// HEADER FILES ------------------------------------------------------------

#include "d_player.h"
#include "i_system.h"
#include "sc_man.h"
#include "teaminfo.h"
#include "v_video.h"
#include "v_palette.h"
#include "v_font.h"
#include "w_wad.h"

// [CW] New includes.
#include "team.h"
#include "version.h"
#include "computation/musicspec_compute.h"	// [rc4l] ComputeMusicSpec

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

void TEAMINFO_Init ();
void TEAMINFO_ParseTeam (FScanner &sc);

// [CW] See 'TEAM_CheckIfValid' in 'team.cpp'.

//bool TEAMINFO_IsValidTeam (int team);

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

TArray <TEAMINFO> teams;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static const char *keywords_teaminfo [] = {
	"PLAYERCOLOR",
	"TEXTCOLOR",
	"LOGO",
	"FLAGITEM",
	"SKULLITEM",
	"RAILCOLOR",
	"PLAYERSTARTTHINGNUMBER",
	"SMALLFLAGHUDICON",
	"SMALLSKULLHUDICON",
	"LARGEFLAGHUDICON",
	"LARGESKULLHUDICON",
	"WINNERPIC",
	"LOSERPIC",
	"WINNERTHEME",
	"LOSERTHEME",
	"ALLOWCUSTOMPLAYERCOLOR",
	NULL
};

// CODE --------------------------------------------------------------------

//==========================================================================
//
// TEAMINFO_Init
//
//==========================================================================

void TEAMINFO_Init ()
{
	int lastlump = 0, lump;

	while ((lump = Wads.FindLump ("TEAMINFO", &lastlump)) != -1)
	{
		FScanner sc(lump);
		while (sc.GetString ())
		{
			if (sc.Compare("CLEARTEAMS"))
				teams.Clear ();
			else if (sc.Compare("TEAM"))
				TEAMINFO_ParseTeam (sc);
			else 
				sc.ScriptError ("Unknown command %s in TEAMINFO", sc.String);
		}
	}

	if (teams.Size () < 2)
		I_FatalError ("At least two teams must be defined in TEAMINFO");

	if ( teams.Size( ) > MAX_TEAMS )
		I_FatalError ( "Only %d teams can be defined in TEAMINFO", MAX_TEAMS );

	// [BB] Now that teams.Size() is known, make sure that sv_maxteams is in its allowed bounds.
	sv_maxteams = sv_maxteams.GetGenericRep( CVAR_Int ).Int;
}

// [rc4l] Adapted from zandronum@59b494f63d585a9c5067c77ff228bb7fe2994cc0: a theme as MAPINFO writes music, "name:order" or the name then its track.
static void teaminfo_ParseTheme( FScanner &sc, FString &name, int &order )
{
	sc.MustGetString( );
	const zx::MusicSpec spec = zx::ComputeMusicSpec( sc.String );
	name = spec.name.c_str( );
	order = spec.order;
	if ( !spec.hasOrder && sc.CheckNumber( ))
		order = sc.Number;
}

//==========================================================================
//
// TEAMINFO_ParseTeam
//
//==========================================================================

void TEAMINFO_ParseTeam (FScanner &sc)
{
	TEAMINFO team;
	// [BB] Initialize some values.
	team.bCustomPlayerColorAllowed = false;
	team.winnerthemeorder = 0;
	team.loserthemeorder = 0;

	int i;
	char *color;

	sc.MustGetString ();
	team.Name = sc.String;

	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		switch(i = sc.MatchString (keywords_teaminfo))
		{
		case 0:
			sc.MustGetString ();
			color = sc.String;
			team.lPlayerColor = V_GetColor (NULL, color);
			break;

		case 1:
			sc.MustGetString();
			team.TextColor = '[';
			team.TextColor += sc.String;
			team.TextColor += ']';
			break;

		case 2:
			sc.MustGetString( );
			team.Logo = sc.String;
			break;

		case 3:
			sc.MustGetString( );
			team.FlagItem = sc.String;
			break;

		case 4:
			sc.MustGetString( );
			team.SkullItem = sc.String;
			break;

		case 5:
			sc.MustGetString( );
			team.lRailColor = V_GetColorFromString( NULL, sc.String );
			break;

		case 6:
			sc.MustGetNumber( );
			team.ulPlayerStartThingNumber = sc.Number;
			break;

		case 7:
			sc.MustGetString( );
			team.SmallFlagHUDIcon = sc.String;
			break;

		case 8:
			sc.MustGetString( );
			team.SmallSkullHUDIcon = sc.String;
			break;

		case 9:
			sc.MustGetString( );
			team.LargeFlagHUDIcon = sc.String;
			break;

		case 10:
			sc.MustGetString( );
			team.LargeSkullHUDIcon = sc.String;
			break;

		case 11:
			sc.MustGetString( );
			team.WinnerPic = sc.String;
			break;

		case 12:
			sc.MustGetString( );
			team.LoserPic = sc.String;
			break;

		case 13:
			teaminfo_ParseTheme( sc, team.WinnerTheme, team.winnerthemeorder );
			break;

		case 14:
			teaminfo_ParseTheme( sc, team.LoserTheme, team.loserthemeorder );
			break;

		case 15:
			team.bCustomPlayerColorAllowed = true;
			break;

		default:
			I_FatalError( "Unknown option '%s', on line %d in TEAMINFO.", sc.String, sc.Line );
			break;
		}
	}

	teams.Push (team);
}

/*

// [CW] See 'TEAM_CheckIfValid' in 'team.cpp'.

//==========================================================================
//
// TEAMINFO_IsValidTeam
//
//==========================================================================

bool TEAMINFO_IsValidTeam (int team)
{
	if (team < 0 || team >= (signed)teams.Size ())
	{
		return false;
	}

	return true;
}

// [CW] See 'TEAM_GetTextColor' in 'team.cpp'.

//==========================================================================
//
// TEAMINFO :: GetTextColor
//
//==========================================================================

int TEAMINFO::GetTextColor () const
{
	if (textcolor.IsEmpty())
	{
		return CR_UNTRANSLATED;
	}
	const BYTE *cp = (const BYTE *)textcolor.GetChars();
	int color = V_ParseFontColor(cp, 0, 0);
	if (color == CR_UNDEFINED)
	{
		Printf("Undefined color '%s' in definition of team %s\n", textcolor.GetChars (), name.GetChars ());
		color = CR_UNTRANSLATED;
	}
	return color;
}

*/
