// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/join-screen/joinscreen.h"

#include "features/join-screen/computation/joinscreen_compute.h"
#include "features/server-browser/zx_joinserver.h"
#include "features/net-session/netrole.h"

#include "c_cvars.h"
#include "cl_main.h"
#include "d_main.h"
#include "gi.h"
#include "i_system.h"
#include "network.h"
#include "textures/textures.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"

#include <string>

CVAR( Bool, fua_joinscreen, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

namespace zx
{

namespace
{

bool g_Peek = false;
float g_Shown = 0.f;
unsigned int g_LastMs = 0;

ConnectionPhase PhaseOf( int state )
{
	switch ( state )
	{
	case CTS_ATTEMPTINGCONNECTION:		return ConnectionPhase::Connecting;
	case CTS_ATTEMPTINGAUTHENTICATION:	return ConnectionPhase::Authenticating;
	case CTS_REQUESTINGSNAPSHOT:		return ConnectionPhase::RequestingSnapshot;
	case CTS_RECEIVINGSNAPSHOT:			return ConnectionPhase::ReceivingSnapshot;
	case CTS_ACTIVE:					return ConnectionPhase::Active;
	default:							return ConnectionPhase::Disconnected;
	}
}

JoinStage CurrentStage( )
{
	JoinScreenInputs in;
	in.enabled = fua_joinscreen;
	in.clientMode = net::IsClient( );
	in.phase = PhaseOf( CLIENT_GetConnectionState( ));
	in.joinInFlight = IsJoinInFlight( );
	return ComputeJoinStage( in );
}

// [Doruk-Aksoy] The browser's name for a join it started, otherwise the address a typed `connect` went to.
std::string ServerName( )
{
	const char *joining = JoiningServerName( );
	if (( joining != NULL ) && ( *joining != 0 ))
		return joining;
	return net::IsClient( ) ? std::string( CLIENT_GetServerAddress( ).ToString( )) : std::string( );
}

void DrawCentered( const char *text, int y, EColorRange color, uint32 overlay = 0 )
{
	const int x = ( screen->GetWidth( ) - SmallFont->StringWidth( text ) * CleanXfac_1 ) / 2;
	screen->DrawText( SmallFont, color, x, y, text, DTA_CleanNoMove_1, true, DTA_ColorOverlay, overlay, TAG_DONE );
}

// [Doruk-Aksoy] The mod's title page filling the window, the same way the title loop shows it, or black when it has none.
void DrawBackground( )
{
	const FTextureID page = TexMan.CheckForTexture( gameinfo.TitlePage, FTexture::TEX_MiscPatch );
	if ( page.isValid( ))
	{
		screen->DrawTexture( TexMan[page], 0, 0, DTA_Fullscreen, true, DTA_Masked, false, DTA_BilinearFilter, true, TAG_DONE );
		screen->FillBorder( NULL );
	}
	else
		screen->Clear( 0, 0, screen->GetWidth( ), screen->GetHeight( ), 0, MAKEARGB( 255, 0, 0, 0 ));
}

} // namespace

bool JoinScreen_Draw( )
{
	const JoinStage stage = CurrentStage( );
	if ( stage == JoinStage::None )
	{
		g_Peek = false;
		g_Shown = 0.f;
		g_LastMs = 0;
		return false;
	}
	if ( g_Peek || ( screen == NULL ) || ( SmallFont == NULL ))
		return false;

	const unsigned int now = I_MSTime( );
	const int elapsed = ( g_LastMs == 0 ) ? 0 : static_cast<int>( now - g_LastMs );
	g_LastMs = now;
	g_Shown = ComputeJoinProgressStep( g_Shown, ComputeJoinStageProgress( stage ), elapsed );

	const int w = screen->GetWidth( );
	const JoinScreenLayout l = ComputeJoinScreenLayout( w, screen->GetHeight( ), SmallFont->GetHeight( ) * CleanYfac_1 );

	// [Doruk-Aksoy] GAMEINFO's STARTUPCOLORS paint the band and the bar, as they paint the Win32 startup window's title.
	const PalEntry fg( DoomStartupInfo.FgColor );
	const PalEntry bk( DoomStartupInfo.BkColor );

	DrawBackground( );
	screen->Dim( bk, 0.85f, 0, l.bandY, w, l.bandH );

	DrawCentered( DoomStartupInfo.Name.GetChars( ), l.titleY, CR_WHITE, MAKEARGB( 255, fg.r, fg.g, fg.b ));
	const bool bDarkText = ComputeJoinTextIsDark( bk.r, bk.g, bk.b );
	DrawCentered( ComputeJoinStatus( stage, ServerName( )).c_str( ), l.statusY, bDarkText ? CR_BLACK : CR_WHITE );

	// [Doruk-Aksoy] The trough is the fill's own colour faded, so the bar reads on any band.
	screen->Dim( fg, 0.3f, l.barX, l.barY, l.barW, l.barH );
	const int fill = static_cast<int>( l.barW * g_Shown );
	if ( fill > 0 )
		screen->Clear( l.barX, l.barY, l.barX + fill, l.barY + l.barH, -1, MAKEARGB( 255, fg.r, fg.g, fg.b ));

	DrawCentered( "Esc opens the menu, the console key shows the console", l.hintY, bDarkText ? CR_DARKGRAY : CR_GRAY );
	return true;
}

bool JoinScreen_TogglePeek( )
{
	if ( CurrentStage( ) == JoinStage::None )
		return false;
	g_Peek = !g_Peek;
	return true;
}

} // namespace zx
