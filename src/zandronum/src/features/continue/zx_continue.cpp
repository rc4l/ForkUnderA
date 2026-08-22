// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/zx_continue.h"

#include "features/continue/computation/continuerecord_compute.h"
#include "features/continue/computation/continueshow_compute.h"
#include "features/continue/computation/continuewrite_compute.h"
#include "features/identity/zx_identity.h"
#include "features/wadreload/zx_wadreload.h"

#include "c_dispatch.h"
#include "cl_main.h"
#include "d_netinf.h"
#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h"
#include "g_level.h"
#include "m_misc.h"
#include "menu/menu.h"	// M_ClearMenus
#include "m_png.h"
#include "network.h"
#include "version.h"
#include "w_wad.h"
#include "zstring.h"

#include <stdio.h>
#include <string>

namespace zx
{

namespace
{

ContinueRecord g_Record;
bool g_bLoaded = false;
FString g_Label;

// [rc4l] The snapshot a Single record points at. One slot, overwritten every time: this is "where
// you left off", not a save history, and a growing pile of them is a thing nobody asked for and
// nobody prunes.
const char *const kContinueSaveName = "continue.zds";

std::string RecordPath()
{
	return ContinueRecordPath( Identity_ConfigRoot( ));
}

std::string SavePath()
{
	const std::string root = Identity_ConfigRoot( );
	if ( root.empty( ))
		return std::string( kContinueSaveName );

	const char last = root[root.size( ) - 1];
	const bool bHasSeparator = ( last == '/' ) || ( last == '\\' );
	return root + ( bHasSeparator ? "" : "/" ) + kContinueSaveName;
}

bool FileExists( const std::string &path )
{
	if ( path.empty( ))
		return false;

	FILE *f = fopen( path.c_str( ), "rb" );
	if ( f == NULL )
		return false;

	fclose( f );
	return true;
}

// The version stamped inside a snapshot, or 0 when it cannot be read. Asked before the button is
// drawn rather than at load time, because by load time the WAD set has already been swapped.
int SaveVersionOf( const std::string &path )
{
	FILE *f = fopen( path.c_str( ), "rb" );
	if ( f == NULL )
		return 0;

	PNGHandle *png = M_VerifyPNG( f );
	if ( png == NULL )
	{
		fclose( f );
		return 0;
	}

	char sigcheck[20] = { 0 };
	int version = 0;
	if ( M_GetPNGText( png, "ZDoom Save Version", sigcheck, 20 ))
		version = atoi( sigcheck + 9 );

	delete png;
	fclose( f );
	return version;
}

void WriteRecord( const ContinueRecord &record )
{
	const std::string text = SerialiseContinue( record );
	if ( text.empty( ))
		return;

	FILE *f = fopen( RecordPath( ).c_str( ), "wb" );
	if ( f == NULL )
		return;

	fwrite( text.data( ), 1, text.size( ), f );
	fclose( f );

	g_Record = record;
	g_bLoaded = true;
}

// What is loaded right now, so a Continue can put it back. Bare names only: the record is about
// which game, not about where this machine happened to keep it.
void CollectLoadedWads( ContinueRecord &record )
{
	for ( int i = 0; i < Wads.GetNumWads( ); ++i )
	{
		const char *name = Wads.GetWadName( i );
		if (( name == NULL ) || ( *name == 0 ))
			continue;

		if ( i == 0 )
			continue;						// our own pk3, which every launch already has

		if ( i == FWadCollection::IWAD_FILENUM )
		{
			record.iwad = name;
			continue;
		}

		record.wads.push_back( std::make_pair( std::string( name ), std::string( )));
	}
}

// Whether what is loaded right now is what the record describes, by bare name and in order. Names
// alone, because that is all a record can hold about files this machine may keep anywhere.
bool SameWadSetAsRecord( )
{
	ContinueRecord loaded;
	CollectLoadedWads( loaded );

	if ( loaded.iwad != g_Record.iwad )
		return false;
	if ( loaded.wads.size( ) != g_Record.wads.size( ) )
		return false;

	for ( size_t i = 0; i < loaded.wads.size( ); ++i )
	{
		if ( loaded.wads[i].first != g_Record.wads[i].first )
			return false;
	}

	return true;
}

} // namespace

void Continue_Load( void )
{
	g_bLoaded = true;
	g_Record = ContinueRecord( );

	FILE *f = fopen( RecordPath( ).c_str( ), "rb" );
	if ( f == NULL )
		return;

	std::string text;
	char buffer[1024];
	size_t got;
	while (( got = fread( buffer, 1, sizeof buffer, f )) > 0 )
		text.append( buffer, got );
	fclose( f );

	ContinueRecord parsed;
	if ( ParseContinue( text, parsed ))
		g_Record = parsed;
}

bool Continue_IsShown( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	// [rc4l] Not while a game is running. Continue is a way back INTO something, and offering it to
	// somebody already playing is offering to throw away what they are doing.
	//
	// AND usergame, because the title screen runs a demo: gamestate is GS_LEVEL while the player is
	// sitting at the main menu, so the level check alone hid the button in exactly the place it is
	// meant to appear.
	if (( gamestate == GS_LEVEL ) && usergame )
		return false;

	ContinueShowInputs in;
	in.recordParsed = ( g_Record.kind != ContinueKind::None );
	in.kind = g_Record.kind;
	in.minSaveVersion = MINSAVEVER;

	if ( g_Record.kind == ContinueKind::Single )
	{
		in.saveFileExists = FileExists( g_Record.savePath );
		in.saveVersion = in.saveFileExists ? SaveVersionOf( g_Record.savePath ) : 0;
	}

	// The probe stays Unknown until something has actually asked the address; see
	// continueshow_compute.h on why that shows rather than hides.
	return ContinueIsShown( in );
}

const char *Continue_Label( void )
{
	if ( Continue_IsShown( ) == false )
	{
		g_Label = "";
		return g_Label.GetChars( );
	}

	// [rc4l] Just the word. The pill is sized to its label and sits beside two fixed ones, so a
	// label that grew with the map name would move the bar's whole left edge every time the player
	// changed level.
	g_Label = "Continue";
	return g_Label.GetChars( );
}

void Continue_NoteQuit( void )
{
	ContinueWriteInputs in;
	// Same trap in the other direction: quitting at the main menu is quitting during the title
	// demo, which is GS_LEVEL and is emphatically not a session worth returning to.
	in.inMap = ( gamestate == GS_LEVEL ) && usergame;
	in.connecting = ( CLIENT_GetConnectionState( ) == CTS_ATTEMPTINGCONNECTION );
	in.crashing = false;			// this path is only ever reached by a deliberate quit

	if ( DecideContinueWrite( in ) != ContinueWriteVerdict::Write )
		return;

	// Online, the address is the session; there is nothing local worth snapshotting.
	if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
	{
		Continue_NoteJoined( );
		return;
	}

	ContinueRecord record;
	record.kind = ContinueKind::Single;
	record.savePath = SavePath( );
	record.saveVersion = SAVEVER;
	record.mapName = level.MapName.GetChars( );
	CollectLoadedWads( record );

	// [rc4l] G_DoSaveGame and not G_SaveGame, which only sets gameaction = ga_savegame and leaves the
	// work to the next tic. There is no next tic: the caller is the quit, and exit() follows
	// immediately. The queued form wrote the record and never the snapshot, so Continue pointed at a
	// file that would never exist and hid itself forever -- safe, and useless.
	G_DoSaveGame( false, record.savePath.c_str( ), "Continue" );
	WriteRecord( record );
}

void Continue_NoteJoined( void )
{
	if ( DecideContinueWriteOnJoin( false ) != ContinueWriteVerdict::Write )
		return;

	ContinueRecord record;
	record.kind = ContinueKind::Server;
	record.address = CLIENT_GetServerAddress( ).ToString( );
	record.password = cl_password;
	CollectLoadedWads( record );

	WriteRecord( record );
}

void Continue_Forget( void )
{
	remove( RecordPath( ).c_str( ));
	g_Record = ContinueRecord( );
	g_bLoaded = true;
}

int Continue_RecordKind( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	switch ( g_Record.kind )
	{
	case ContinueKind::Single: return 1;
	case ContinueKind::Server: return 2;
	default:                   return 0;
	}
}

const char *Continue_RecordTarget( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	if ( g_Record.kind == ContinueKind::Server )
		return g_Record.address.c_str( );

	return g_Record.mapName.c_str( );
}

bool Continue_DebugSaveExists( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	return FileExists( g_Record.savePath );
}

int Continue_DebugSaveVersion( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	return SaveVersionOf( g_Record.savePath );
}

bool Continue_DebugBusy( void )
{
	return ( gamestate == GS_LEVEL ) && usergame;
}

// [rc4l] Survives the reload, because RequestReload throws CRestartException and that unwinds inside
// the SAME process -- the trick zx_joinserver already relies on to know a connect was one it started.
bool g_bLoadAfterRestart = false;
FString g_SaveAfterRestart;

void Continue_Tick( void )
{
	if ( g_bLoadAfterRestart == false )
		return;

	// Once only, and only once the engine is actually up enough to load into.
	if ( gamestate == GS_STARTUP )
		return;

	g_bLoadAfterRestart = false;
	G_LoadGame( g_SaveAfterRestart.GetChars( ));
}

void Continue_Activate( void )
{
	if ( Continue_IsShown( ) == false )
		return;

	// [rc4l] Straight in, no confirmation: the decision was made when the button chose to exist.
	M_ClearMenus( );

	if ( g_Record.kind == ContinueKind::Server )
	{
		// Down the join path the browser already uses, so a failure lands where every other failed
		// join lands rather than inventing a second way to go wrong.
		FString command;
		command.Format( "connect %s", g_Record.address.c_str( ));
		AddCommandString( command.LockBuffer( ));
		command.UnlockBuffer( );
		return;
	}

	// [rc4l] Only reload when the set is actually wrong. The ordinary case by far is relaunching the
	// same way and pressing Continue, and asking RequestReload to prove that costs a full validation
	// against search paths the record cannot know -- our IWAD is remembered by bare name, so a copy
	// living outside the search path fails the check and refuses a reload nothing needed.
	if ( SameWadSetAsRecord( ) )
	{
		G_LoadGame( g_Record.savePath.c_str( ));
		return;
	}

	TArray<FString> pwads;
	for ( size_t i = 0; i < g_Record.wads.size( ); ++i )
		pwads.Push( g_Record.wads[i].first.c_str( ));

	g_SaveAfterRestart = g_Record.savePath.c_str( );

	const wadreload::ReloadResult r = wadreload::RequestReload(
		g_Record.iwad.empty( ) ? NULL : g_Record.iwad.c_str( ), pwads );

	if ( r == wadreload::ReloadResult::AlreadyLoaded )
	{
		G_LoadGame( g_SaveAfterRestart.GetChars( ));
		return;
	}

	// Restarting: the reload throws, so this is only reached when it refused. Say so rather than
	// leaving a button that looks like it did nothing.
	if ( r == wadreload::ReloadResult::InvalidWads )
		Printf( "Continue: the files that session used are no longer loadable.\n" );

	g_bLoadAfterRestart = ( r == wadreload::ReloadResult::Restarting );
}

} // namespace zx
