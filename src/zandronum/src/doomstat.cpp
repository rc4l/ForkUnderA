//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Marisa Heit
// Copyright 2002-2016 Christoph Oelckers
// Copyright 2012-2024 Zandronum Development Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
// DESCRIPTION:
//      Put all global state variables here.
//
//-----------------------------------------------------------------------------



#include "stringtable.h"
#include "doomstat.h"
#include "c_cvars.h"
#include "i_system.h"
#include "g_level.h"
#include "p_local.h"
// [WS] New includes
#include "sv_commands.h"

int SaveVersion;

// Localizable strings
FStringTable	GStrings;

// Game speed
EGameSpeed		GameSpeed = SPEED_Normal;

// Show developer messages if true.
CVAR (Bool, developer, false, 0)

// [RH] Feature control cvars
CVAR (Bool, var_friction, true, CVAR_SERVERINFO);
CVAR (Bool, var_pushers, true, CVAR_SERVERINFO);

// [WS] Changed CVAR to CUSTOM_CVAR as we need to send clients the updated state of this.
// [AK] Added CVAR_GAMEPLAYSETTING.
CUSTOM_CVAR (Bool, alwaysapplydmflags, false, CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)
{
	SERVER_SettingChanged( self, false );
}

// [AK] Added CVAR_GAMEPLAYSETTING.
CUSTOM_CVAR (Float, teamdamage, 0.f, CVAR_SERVERINFO | CVAR_GAMEPLAYSETTING)
{
	level.teamdamage = self;
}

CUSTOM_CVAR (String, language, "auto", CVAR_ARCHIVE)
{
	SetLanguageIDs ();
	GStrings.LoadStrings (false);
	if (level.info != NULL) level.LevelName = level.info->LookupLevelName();
}

// [RH] Network arbitrator
int Net_Arbitrator = 0;

int NextSkill = -1;

int SinglePlayerClass[MAXPLAYERS];

bool ToggleFullscreen;
int BorderTopRefresh;

