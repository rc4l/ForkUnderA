// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/zx_continue.h"

#include "features/continue/computation/continuerecord_compute.h"
#include "features/continue/computation/continueshow_compute.h"
#include "features/continue/computation/continuewrite_compute.h"
#include "features/identity/zx_identity.h"
#include "features/wadreload/zx_wadreload.h"
#include "features/wad-download/zx_waddownload.h"

#include "c_dispatch.h"
#include "cmdlib.h"
#include "cl_main.h"
#include "d_main.h"		// D_AddFile
#include "d_netinf.h"
#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h"
#include "g_level.h"
#include "m_misc.h"
#include "menu/menu.h"	// M_ClearMenus
#include "m_png.h"
#include "network.h"
#include "features/server-browser/browser.h"
#include "i_system.h"
#include "version.h"
#include "w_wad.h"
#include "zstring.h"

#include <stdio.h>
#include <string.h>
#include <string>

namespace zx
{

namespace
{

ContinueRecord g_Record;
bool g_bLoaded = false;
FString g_Label;
FString g_Tooltip;

// [rc4l] Which copy of the engine this is, so two of them do not share one record. Same numbering
// the account keys use, taken from the same claim, so the two cannot disagree about who is who.
int Instance()
{
	return Identity_Instance( );
}

std::string RecordPath()
{
	return ContinueOfflinePath( Identity_ConfigRoot( ), Instance( ));
}

std::string SavePath()
{
	return ContinueSavePath( Identity_ConfigRoot( ), Instance( ));
}

// The folder has to exist before either file can be written, and it is ours to make.
void EnsureDir()
{
	CreatePath( ContinueDir( Identity_ConfigRoot( ), Instance( )).c_str( ));
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

	EnsureDir( );

	FILE *f = fopen( RecordPath( ).c_str( ), "wb" );
	if ( f == NULL )
		return;

	fwrite( text.data( ), 1, text.size( ), f );
	fclose( f );

	g_Record = record;
	g_bLoaded = true;
}

// [rc4l] What is loaded right now, by NAME and CHECKSUM, so a Continue can find the same game again
// wherever the player keeps it.
//
// The checksums are not computed here. NETWORK_GetPWADList already holds one per loaded file: it is
// built once per WAD set at startup, by a loop that reads every file end to end because servers have
// to tell clients what they are running. Hashing again at quit would re-read every byte -- a large
// pack is a visible pause on exit -- to arrive at an answer the engine worked out at launch.
void CollectLoadedWads( ContinueRecord &record )
{
	// The PWAD list is already exactly what we want here: it excludes the IWAD, our own pk3 and
	// anything the engine auto-loaded, which are the three things a Continue must not try to re-add.
	const TArray<NetworkPWAD> &loaded = NETWORK_GetPWADList( );

	for ( unsigned int i = 0; i < loaded.Size( ); ++i )
	{
		if ( loaded[i].name.IsEmpty( ))
			continue;

		record.wads.push_back( std::make_pair(
			std::string( loaded[i].name.GetChars( )), std::string( loaded[i].checksum.GetChars( ))));
	}

	// The IWAD is deliberately absent from that list, so its digest is fetched the way the server
	// fetches it for SQF2_FUA_IWAD_HASH: by name out of the authenticated list.
	const char *iwadName = NETWORK_GetIWAD( );
	if (( iwadName == NULL ) || ( *iwadName == 0 ))
		return;

	record.iwad = iwadName;

	const TArray<NetworkPWAD> &authenticated = NETWORK_GetAuthenticatedWADsList( );
	for ( unsigned int i = 0; i < authenticated.Size( ); ++i )
	{
		if ( authenticated[i].name.CompareNoCase( iwadName ) == 0 )
		{
			record.iwadHash = authenticated[i].checksum.GetChars( );
			break;
		}
	}
}

// [rc4l] The file on this disk that a recorded name and checksum mean, as a path something can load.
//
// The join path's own order, and for its own reason: ask for the CONTENT first, because a copy we
// already hold with that digest is the right answer by definition, and going by name instead lets
// another file of the same name earlier in the search path answer for it. Falling back to D_AddFile
// is what turns a bare name into a real path at all -- WadLoadable only stats what it is handed, so
// a name that was never resolved is simply "file not found", whatever the search directories say.
//
// Empty when nothing on this machine matches, which the caller must treat as "cannot continue"
// rather than guessing.
FString ResolveWad( const std::string &name, const std::string &md5 )
{
	if ( name.empty( ))
		return FString( );

	if ( md5.empty( ) == false )
	{
		const FString exact = waddownload::FindLocalCopy( name.c_str( ), md5.c_str( ));
		if ( exact.IsNotEmpty( ))
			return exact;
	}

	TArray<FString> resolved;
	if ( D_AddFile( resolved, name.c_str( )) && ( resolved.Size( ) > 0 ))
		return resolved[resolved.Size( ) - 1];

	return FString( );
}

// [rc4l] Asking the remembered server whether it is still there, and still the same game.
//
// Through the browser's own machinery rather than a second copy of the query: AddServerToList makes
// the slot (deduping if the browser already knows it) and RecheckServer sends the query the browser
// would have sent. Anything else would be a private reimplementation of a protocol that already has
// one owner.
ServerProbe g_Probe = ServerProbe::Unknown;
bool g_bProbeSent = false;
unsigned int g_ProbeStartedMS = 0;
LONG g_ProbeSlot = -1;

// How long to let it answer before calling it gone. A query and its reply on a working link is
// milliseconds; this is loose enough for a slow one and short enough that the button does not sit
// there offering a dead server for the whole time somebody reads the menu.
const unsigned int kProbeTimeoutMS = 4000;

// Same game, judged the way the record was written: bare PWAD names, in order.
bool ProbeWadsMatch( ULONG slot )
{
	const LONG count = BROWSER_GetNumPWADs( slot );
	if ( count < 0 )
		return true;					// it told us nothing, so it has not contradicted us

	if ( static_cast<size_t>( count ) != g_Record.wads.size( ) )
		return false;

	for ( LONG i = 0; i < count; ++i )
	{
		const char *name = BROWSER_GetPWADName( slot, i );
		if (( name == NULL ) || ( g_Record.wads[i].first != name ))
			return false;
	}

	return true;
}

// [rc4l] Which file the MAP itself came from, which is not the same question as which files are
// loaded. Several may define MAP11; only one of them is the one that opens.
//
// The rule is P_OpenMapData's, mirrored rather than guessed at: a map can be a plain lump, or
// maps/<name>.wad, or maps/<name>.map, and THE HIGHEST LUMP NUMBER WINS because that is the copy
// loaded last. Checking only the plain lump -- the obvious version -- finds nothing at all for a
// map inside a pk3, and checking them in a fixed order names the IWAD's MAP11 while the player is
// standing in a megawad's.
std::string MapWadName( const char *mapName )
{
	if (( mapName == NULL ) || ( *mapName == 0 ))
		return std::string( );

	int best = -1;

	// Names longer than eight characters cannot be a plain lump at all.
	if ( strlen( mapName ) <= 8 )
		best = Wads.CheckNumForName( mapName );

	FString path;
	path.Format( "maps/%s.wad", mapName );
	const int asWad = Wads.CheckNumForFullName( path );
	if ( asWad > best )
		best = asWad;

	path.Format( "maps/%s.map", mapName );
	const int asMap = Wads.CheckNumForFullName( path );
	if ( asMap > best )
		best = asMap;

	if ( best < 0 )
		return std::string( );

	const char *wad = Wads.GetWadName( Wads.GetLumpFile( best ));
	return (( wad != NULL ) && ( *wad != 0 )) ? std::string( wad ) : std::string( );
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

	in.probe = g_Probe;
	return ContinueIsShown( in );
}

const char *Continue_Tooltip( void )
{
	if ( Continue_IsShown( ) == false )
	{
		g_Tooltip = "";
		return g_Tooltip.GetChars( );
	}

	if ( g_Record.kind == ContinueKind::Server )
	{
		// The name if we have one, the address if we never learned it.
		const char *where = g_Record.serverName.empty( )
			? g_Record.address.c_str( ) : g_Record.serverName.c_str( );
		g_Tooltip.Format( "Continue playing online in %s", where );
	}
	else if ( g_Record.mapWad.empty( ) == false )
		g_Tooltip.Format( "Continue singleplayer in %s on %s", g_Record.mapName.c_str( ), g_Record.mapWad.c_str( ) );
	else
		g_Tooltip.Format( "Continue singleplayer in %s", g_Record.mapName.c_str( ) );

	return g_Tooltip.GetChars( );
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

	record.mapWad = MapWadName( level.MapName.GetChars( ));

	CollectLoadedWads( record );

	// [rc4l] G_DoSaveGame and not G_SaveGame, which only sets gameaction = ga_savegame and leaves the
	// work to the next tic. There is no next tic: the caller is the quit, and exit() follows
	// immediately. The queued form wrote the record and never the snapshot, so Continue pointed at a
	// file that would never exist and hid itself forever -- safe, and useless.
	EnsureDir( );
	G_DoSaveGame( false, record.savePath.c_str( ), "Continue" );
	WriteRecord( record );
}

void Continue_NoteJoined( void )
{
	if ( DecideContinueWriteOnJoin( false ) != ContinueWriteVerdict::Write )
		return;

	ContinueRecord record;
	record.kind = ContinueKind::Server;

	const NETADDRESS_s address = CLIENT_GetServerAddress( );
	record.address = address.ToString( );
	record.password = cl_password;

	// What the server calls itself, if the browser happens to know: an address in a tooltip is
	// honest but unreadable, and a name is what the player recognises.
	const LONG slot = BROWSER_GetListIDByAddress( address );
	if ( slot >= 0 )
	{
		const char *name = BROWSER_GetHostName( slot );
		if (( name != NULL ) && ( *name != 0 ))
			record.serverName = name;
	}
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
	// [rc4l] The record has to be in hand before any of this means anything. Without it every tick
	// saw kind None, the probe never started, and the button sat there offering a server nobody had
	// asked about -- which looked exactly like a probe that had run and found it alive.
	if ( g_bLoaded == false )
		Continue_Load( );

	// The probe runs while the player is at a menu deciding, so the answer is usually in before the
	// button is pressed. It settles once and stays settled: re-asking every frame would be a query
	// storm aimed at somebody else's server.
	if (( g_Record.kind == ContinueKind::Server ) && ( g_Probe == ServerProbe::Unknown ))
	{
		if ( g_bProbeSent == false )
		{
			NETADDRESS_s address;
			if ( address.LoadFromString( g_Record.address.c_str( )))
			{
				BROWSER_AddServerToList( address );
				g_ProbeSlot = BROWSER_GetListIDByAddress( address );
				if ( g_ProbeSlot >= 0 )
					BROWSER_RecheckServer( g_ProbeSlot );

				g_ProbeStartedMS = I_MSTime( );
				g_bProbeSent = true;
			}
		}
		else if ( g_ProbeSlot >= 0 )
		{
			if ( BROWSER_IsActive( g_ProbeSlot ))
				g_Probe = ProbeWadsMatch( g_ProbeSlot ) ? ServerProbe::Alive : ServerProbe::WadsDiffer;
			else if ( I_MSTime( ) - g_ProbeStartedMS > kProbeTimeoutMS )
				g_Probe = ServerProbe::Gone;
		}
	}

	if ( g_bLoadAfterRestart == false )
		return;

	// Once only, and only once the engine is actually up enough to load into.
	if ( gamestate == GS_STARTUP )
		return;

	g_bLoadAfterRestart = false;
	G_LoadGame( g_SaveAfterRestart.GetChars( ));
}

int Continue_DebugProbe( void )
{
	switch ( g_Probe )
	{
	case ServerProbe::Alive:      return 1;
	case ServerProbe::Gone:       return 2;
	case ServerProbe::WadsDiffer: return 3;
	default:                      return 0;
	}
}

int Continue_DebugProbeSlot( void )
{
	return static_cast<int>( g_ProbeSlot );
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

	// Names into paths before anything is asked to load them; see ResolveWad.
	TArray<FString> pwads;
	bool bAllFound = true;

	for ( size_t i = 0; i < g_Record.wads.size( ); ++i )
	{
		const FString path = ResolveWad( g_Record.wads[i].first, g_Record.wads[i].second );
		if ( path.IsEmpty( ))
		{
			bAllFound = false;
			break;
		}

		pwads.Push( path );
	}

	const FString iwad = ResolveWad( g_Record.iwad, g_Record.iwadHash );
	if ( g_Record.iwad.empty( ) == false && iwad.IsEmpty( ))
		bAllFound = false;

	if ( bAllFound == false )
	{
		Printf( "Continue: the files that session used are no longer on this machine.\n" );
		return;
	}

	g_SaveAfterRestart = g_Record.savePath.c_str( );

	const wadreload::ReloadResult r = wadreload::RequestReload(
		iwad.IsEmpty( ) ? NULL : iwad.GetChars( ), pwads );

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
