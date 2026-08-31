// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/zx_continue.h"

#include "features/continue/computation/continuerecord_compute.h"
#include "features/continue/computation/continuebutton_compute.h"
#include "features/continue/computation/continuedepart_compute.h"
#include "features/continue/computation/continuerehost_compute.h"
#include "features/continue/computation/continuereturn_compute.h"
#include "features/continue/computation/continueshow_compute.h"
#include "features/continue/computation/continuewrite_compute.h"
#include "features/identity/zx_identity.h"
#include "features/server-hosting/zx_hosting.h"
#include "features/wadreload/zx_wadreload.h"
#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_waddownload.h"

#include "c_dispatch.h"
#include "cmdlib.h"
#include "cl_main.h"
#include "d_event.h"		// gameaction, ga_nothing
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
#include "features/server-browser/zx_joinserver.h"
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

ContinueRecord g_Offline;
bool g_bReconnecting = false;
bool g_bReturnPending = false;
// [rc4l] Decided while we are still IN the session, because the question changes the moment we are
// out of it: out of a session the pill offers the most recently left thing, which after a kick is
// the server we were just thrown out of. Asking then sends the player back where they came from.
ContinueTarget g_ReturnTarget = ContinueTarget::None;
// [rc4l] A rehost we started and have not yet joined. HostStart only SPAWNS the server; the menu
// path joins it separately once the child reports ready, so a rehost that stopped at HostStart left
// the player at a menu watching a server they were supposed to be inside.
bool g_bJoinRehostWhenReady = false;
// [rc4l] A return of ours between being performed and being proven. Only cleared by actually landing
// in the session -- a return that falls apart must not read as the player leaving and ask for
// another one, which is the infinite rehost.
bool g_bReturnInFlight = false;
// [rc4l] Set before a reload and read on the way back up, so a rehost that needed different files
// finishes on the other side of the restart. Survives because RequestReload unwinds in-process.
bool g_bHostAfterRestart = false;
// [rc4l] The player's own command is mid-flight and it has a destination of its own.
bool g_bGoingSomewhereChosen = false;
int g_DepartCalls = 0;
int g_DepartReturns = 0;
ContinueRecord g_Server;
bool g_bLoaded = false;
// [rc4l] Bumped every time the records are re-read, so anything derived from them can tell in one
// comparison whether what it worked out last frame still holds.
int g_LoadGeneration = 0;
// [rc4l] Answered by Chosen() when nothing is worth offering, so callers get a record rather than a
// null to check.
const ContinueRecord g_Nothing;
FString g_Label;
FString g_Tooltip;

// [rc4l] Which copy of the engine this is, so two of them do not share one record. Same numbering
// the account keys use, taken from the same claim, so the two cannot disagree about who is who.
int Instance()
{
	return Identity_Instance( );
}

std::string OfflineRecordPath()
{
	return ContinueOfflinePath( Identity_ConfigRoot( ), Instance( ));
}

std::string ServerRecordPath()
{
	return ContinueServerPath( Identity_ConfigRoot( ), Instance( ));
}

ContinueRecord ReadRecord( const std::string &path )
{
	ContinueRecord out;

	FILE *f = fopen( path.c_str( ), "rb" );
	if ( f == NULL )
		return out;

	std::string text;
	char buffer[1024];
	size_t got;
	while (( got = fread( buffer, 1, sizeof buffer, f )) > 0 )
		text.append( buffer, got );
	fclose( f );

	ContinueRecord parsed;
	return ParseContinue( text, parsed ) ? parsed : out;
}

// [rc4l] One past whichever record is currently the newer, so "most recently left" is a comparison
// rather than a clock. Read from disk rather than remembered: the other record may have been written
// by a different copy of the engine, or by this one before a restart.
int NextStamp()
{
	const int offline = ReadRecord( ContinueOfflinePath( Identity_ConfigRoot( ), Instance( ))).stamp;
	const int server = ReadRecord( ContinueServerPath( Identity_ConfigRoot( ), Instance( ))).stamp;
	const int highest = ( offline > server ) ? offline : server;

	return highest + 1;
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

void WriteRecord( const ContinueRecord &record, const std::string &path )
{
	const std::string text = SerialiseContinue( record );
	if ( text.empty( ))
		return;

	EnsureDir( );

	FILE *f = fopen( path.c_str( ), "wb" );
	if ( f == NULL )
		return;

	fwrite( text.data( ), 1, text.size( ), f );
	fclose( f );

	// [rc4l] What is held in memory is now behind what is on disk, so make the next question re-read
	// it. Without this the pill answers from whatever was loaded when the menus first opened, which
	// is exactly the moment before anything interesting has been recorded.
	g_bLoaded = false;
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

		ContinueRecord::Wad wad;
		wad.name = loaded[i].name.GetChars( );
		wad.hash = loaded[i].checksum.GetChars( );

		// Where we opened it from, so a Continue can find it again even somewhere the engine would
		// not think to look. Checked against the digest before it is believed; see the header.
		const char *path = W_GetLoadedWadPath( wad.name.c_str( ));
		if (( path != NULL ) && ( *path != 0 ))
			wad.path = path;

		record.wads.push_back( wad );
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

// Every file a record names, as paths, or false the moment one cannot be found. Shared because
// going back to an offline game and starting a server again need exactly the same answer, and when
// they each worked it out the rehost path got the weaker version.
static bool ResolveRecordFiles( const ContinueRecord &rec, TArray<FString> &pwads, FString &iwad );

// The IWAD as a Wad, so one resolver answers for every file rather than two that can drift apart.
static ContinueRecord::Wad IwadOf( const ContinueRecord &rec )
{
	ContinueRecord::Wad out;
	out.name = rec.iwad;
	out.hash = rec.iwadHash;
	return out;
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
FString ResolveWad( const ContinueRecord::Wad &wad )
{
	if ( wad.name.empty( ))
		return FString( );

	// [rc4l] A file we are holding open answers for itself. Searching first meant a mod loaded from
	// anywhere but a wad directory -- a downloads folder, which is where one arrives -- was reported
	// as no longer on this machine while it was open in this very process.
	const char *loaded = W_GetLoadedWadPath( wad.name.c_str( ));
	if (( loaded != NULL ) && ( *loaded != 0 ))
		return FString( loaded );

	if ( wad.hash.empty( ) == false )
	{
		const FString exact = waddownload::FindLocalCopy( wad.name.c_str( ), wad.hash.c_str( ));
		if ( exact.IsNotEmpty( ))
			return exact;
	}

	TArray<FString> resolved;
	if ( D_AddFile( resolved, wad.name.c_str( )) && ( resolved.Size( ) > 0 ))
		return resolved[resolved.Size( ) - 1];

	// [rc4l] Last: where it was when we recorded it. Only ever reached once every portable way of
	// finding it has failed, and only believed when the file there still has the digest we recorded
	// -- a path is a guess about another machine's disk, and the digest is what makes it safe to
	// act on. Without this a mod kept outside the search directories, which is where a downloaded
	// one lands, was declared missing while sitting exactly where the player left it.
	if (( wad.path.empty( ) == false ) && ( wad.hash.empty( ) == false ))
	{
		char actual[33] = { 0 };
		if ( Md5OfFile( wad.path.c_str( ), actual, sizeof actual ) && ( stricmp( actual, wad.hash.c_str( )) == 0 ))
			return FString( wad.path.c_str( ));
	}

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

	if ( static_cast<size_t>( count ) != g_Server.wads.size( ) )
		return false;

	for ( LONG i = 0; i < count; ++i )
	{
		const char *name = BROWSER_GetPWADName( slot, i );
		if (( name == NULL ) || ( g_Server.wads[i].name != name ))
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

static bool ResolveRecordFiles( const ContinueRecord &rec, TArray<FString> &pwads, FString &iwad )
{
	pwads.Clear( );

	for ( size_t i = 0; i < rec.wads.size( ); ++i )
	{
		const FString path = ResolveWad( rec.wads[i] );
		if ( path.IsEmpty( ))
			return false;

		pwads.Push( path );
	}

	iwad = ResolveWad( IwadOf( rec ));
	return ( rec.iwad.empty( ) || iwad.IsNotEmpty( ));
}

// Whether what is loaded right now is what the record describes, by bare name and in order. Names
// alone, because that is all a record can hold about files this machine may keep anywhere.
//
// [rc4l] Takes the record to compare against rather than reading a stored winner. It always read the
// derived one, including from the path activating the offline record, so an offline return could be
// measured against the file set of an unrelated server and skip the reload it needed.
bool SameWadSetAsRecord( const ContinueRecord &against )
{
	ContinueRecord loaded;
	CollectLoadedWads( loaded );

	if ( loaded.iwad != against.iwad )
		return false;
	if ( loaded.wads.size( ) != against.wads.size( ) )
		return false;

	for ( size_t i = 0; i < loaded.wads.size( ); ++i )
	{
		if ( loaded.wads[i].name != against.wads[i].name )
			return false;
	}

	return true;
}

} // namespace

void Continue_Load( void )
{
	g_bLoaded = true;
	++g_LoadGeneration;

	// Both, kept apart, so joining a server does not forget the game that was already going.
	g_Offline = ReadRecord( OfflineRecordPath( ));
	g_Server = ReadRecord( ServerRecordPath( ));
}

// [rc4l] Connected to a server. NOT merely "a level is running": the title screen plays a demo, so
// a level runs while the player sits at the main menu.
static bool InSession( void )
{
	return ( NETWORK_GetState( ) == NETSTATE_CLIENT );
}

// [rc4l] Whether this particular record is worth offering: a snapshot too old to load, one whose
// file has gone, and a server that no longer answers are all records that parse perfectly and lead
// nowhere.
//
// Asked per record rather than once for "the" record. Deriving a single winner up front and then
// testing THAT was the old shape, and it hid the pill whenever the more recent of the two happened
// to be a dead server -- the other record was still perfectly good and never got asked.
static bool RecordUsable( const ContinueRecord &rec )
{
	ContinueShowInputs in;
	in.recordParsed = ( rec.kind != ContinueKind::None );
	in.kind = rec.kind;
	in.minSaveVersion = MINSAVEVER;

	if ( rec.kind == ContinueKind::Single )
	{
		in.saveFileExists = FileExists( rec.savePath );
		in.saveVersion = in.saveFileExists ? SaveVersionOf( rec.savePath ) : 0;
	}

	// The probe is a fact about a remote address, so it only bears on the server record.
	in.probe = ( rec.kind == ContinueKind::Server ) ? g_Probe : ServerProbe::Alive;

	return ContinueIsShown( in );
}

// Everything the pill's decision rests on, gathered in one place so the label, the action and
// whether it is drawn at all cannot disagree about the answer.
static ContinueButtonVerdict Verdict( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	// [rc4l] Worked out once per change rather than once per frame. The header asks the pill for its
	// label and whether to draw it every frame, and answering honestly means a stat and, for a
	// snapshot, opening the savegame and parsing its PNG header -- twice over, sixty times a second,
	// for an answer that only moves when one of these four inputs does.
	static ContinueButtonVerdict cached;
	static int cachedGeneration = -1;
	static ServerProbe cachedProbe = ServerProbe::Unknown;
	static bool cachedInSession = false;

	const bool inSession = InSession( );

	if (( cachedGeneration == g_LoadGeneration ) && ( cachedProbe == g_Probe )
		&& ( cachedInSession == inSession ))
	{
		return cached;
	}

	ContinueButtonInputs in;
	in.inSession = inSession;
	in.offlineUsable = RecordUsable( g_Offline );
	in.offlineIsHosted = ( g_Offline.kind == ContinueKind::Hosted );
	in.offlineStamp = g_Offline.stamp;
	in.serverUsable = RecordUsable( g_Server );
	in.serverStamp = g_Server.stamp;

	cached = DecideContinueButton( in );
	cachedGeneration = g_LoadGeneration;
	cachedProbe = g_Probe;
	cachedInSession = inSession;

	return cached;
}

// The record the pill would act on, which is the only sense in which one of the two is "the"
// record. Everything that used to read a stored winner reads this.
static const ContinueRecord &Chosen( void )
{
	switch ( Verdict( ).target )
	{
	case ContinueTarget::Server:	return g_Server;
	case ContinueTarget::Offline:
	case ContinueTarget::Hosted:	return g_Offline;
	default:						return g_Nothing;
	}
}

bool Continue_IsDisconnect( void )
{
	return ( Verdict( ).mode == ContinueMode::Disconnect );
}

bool Continue_IsShown( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	// [rc4l] In a server it is the way OUT, and leaving is always possible, so it is always there.
	if ( InSession( ))
		return true;

	// Not while a LOCAL game is running: Continue is a way back into something, and offering it to
	// somebody already playing is offering to throw away what they are doing.
	//
	// AND usergame, because the title screen runs a demo: gamestate is GS_LEVEL while the player is
	// sitting at the main menu, so the level check alone hid the button in exactly the place it is
	// meant to appear.
	if (( gamestate == GS_LEVEL ) && usergame )
		return false;

	// Whether there is anywhere to go is the same question as where, asked of the same verdict.
	return ( Verdict( ).target != ContinueTarget::None );
}

const char *Continue_Tooltip( void )
{
	if ( Continue_IsShown( ) == false )
	{
		g_Tooltip = "";
		return g_Tooltip.GetChars( );
	}

	// [rc4l] Leaving says where it will put you, because that is the part nobody can guess.
	if ( Continue_IsDisconnect( ))
	{
		const ContinueButtonVerdict v = Verdict( );

		if ( v.target == ContinueTarget::Hosted )
			g_Tooltip.Format( "Leave and go back to hosting %s", g_Offline.host.map.c_str( ));
		else if ( v.target == ContinueTarget::Offline )
			g_Tooltip.Format( "Leave and go back to %s", g_Offline.mapName.c_str( ));
		else
			g_Tooltip = "Leave and go back to the main menu";

		return g_Tooltip.GetChars( );
	}

	const ContinueRecord &rec = Chosen( );

	if ( rec.kind == ContinueKind::Server )
	{
		// The name if we have one, the address if we never learned it.
		const char *where = rec.serverName.empty( )
			? rec.address.c_str( ) : rec.serverName.c_str( );
		g_Tooltip.Format( "Continue playing online in %s", where );
	}
	else if ( rec.kind == ContinueKind::Hosted )
		g_Tooltip.Format( "Continue hosting %s", rec.host.map.c_str( ) );
	else if ( rec.mapWad.empty( ) == false )
		g_Tooltip.Format( "Continue singleplayer in %s on %s", rec.mapName.c_str( ), rec.mapWad.c_str( ) );
	else
		g_Tooltip.Format( "Continue singleplayer in %s", rec.mapName.c_str( ) );

	return g_Tooltip.GetChars( );
}

const char *Continue_Label( void )
{
	if ( Continue_IsShown( ) == false )
	{
		g_Label = "";
		return g_Label.GetChars( );
	}

	// [rc4l] Just the word, either way. The pill is sized to its label and sits beside two fixed
	// ones, so a label that grew with the map name would move the bar's left edge every time the
	// player changed level.
	g_Label = Continue_IsDisconnect( ) ? "Disconnect" : "Continue";
	return g_Label.GetChars( );
}

// Defined below, next to the record it writes.
void WriteLocalSnapshot( void );

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

	WriteLocalSnapshot( );
}

// [rc4l] Snapshot whatever is running locally into the offline record. One implementation, because
// quitting and leaving for a server are the same act as far as the record is concerned -- the only
// difference is what happens next.
void WriteLocalSnapshot( void )
{
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
	record.stamp = NextStamp( );

	G_DoSaveGame( false, record.savePath.c_str( ), "Continue" );
	WriteRecord( record, OfflineRecordPath( ));
}

void Continue_NoteLeavingLocalGame( void )
{
	// Same rules as quitting: a crash is not a session, mid-connect there is nothing to go back to,
	// and a menu is not a place to be returned to.
	ContinueWriteInputs in;
	in.inMap = ( gamestate == GS_LEVEL ) && usergame;
	in.connecting = ( CLIENT_GetConnectionState( ) == CTS_ATTEMPTINGCONNECTION );
	in.crashing = false;
	in.hosting = HostIsActive( );

	if ( DecideContinueWrite( in ) != ContinueWriteVerdict::Write )
		return;

	// Only what is OURS to snapshot. Already being a client means the local game went long ago, and
	// that session belongs to whoever recorded it at the time.
	if ( NETWORK_GetState( ) == NETSTATE_CLIENT )
		return;

	WriteLocalSnapshot( );
}

void Continue_NoteHosting( const HostConfig &config )
{
	// What we are LEAVING first, so the stamps land in the order the player lived them: the game
	// they were in is older than the server they are starting.
	Continue_NoteLeavingLocalGame( );

	ContinueRecord record;
	record.kind = ContinueKind::Hosted;
	record.host = config;
	record.host.rconSecret.clear( );	// worth nothing after its process; a rehost mints a new one
	record.stamp = NextStamp( );

	// [rc4l] The files WE were holding, not just the ones the server was told to load. A rehost has
	// to put this process back on them before it can join anything, and by name alone it cannot: the
	// hashes are what turn a remembered set into files on this machine. Free here -- the same list
	// the offline path records, already digested at startup.
	CollectLoadedWads( record );

	WriteRecord( record, OfflineRecordPath( ));
}

void Continue_JoinHostWhenReady( void )
{
	g_bJoinRehostWhenReady = true;
}

void Continue_NoteReconnecting( bool bReconnecting )
{
	g_bReconnecting = bReconnecting;
}

void Continue_NoteLeftServer( void )
{
	++g_DepartCalls;

	if ( g_bLoaded == false )
		Continue_Load( );

	ContinueDepartInputs in;
	in.wasInSession = InSession( );
	in.joinInFlight = IsJoinInFlight( );
	in.reconnecting = g_bReconnecting;
	in.crashing = false;
	in.goingSomewhereChosen = g_bGoingSomewhereChosen;
	in.returnInFlight = g_bReturnInFlight;

	if ( DecideContinueDepart( in ) != ContinueDepartVerdict::Return )
		return;

	// Where to, decided now while the answer is still the leaving one.
	ContinueButtonInputs where;
	where.inSession = true;
	where.offlineUsable = ( g_Offline.kind != ContinueKind::None );
	where.offlineIsHosted = ( g_Offline.kind == ContinueKind::Hosted );
	where.offlineStamp = g_Offline.stamp;

	g_ReturnTarget = DecideContinueButton( where ).target;

	// Acted on by the tick, once the teardown has finished. See the header.
	++g_DepartReturns;
	g_bReturnPending = true;
}

// [rc4l] Bracket a command that is deliberately leaving a server in order to go somewhere it has
// already chosen -- `map` from a client does exactly that, disconnecting first and starting the map
// after. Without this the disconnect reads as an ordinary departure and we take the player back to
// the session they were leaving, instead of the map they typed.
void Continue_NoteChoosingDestination( bool bChoosing )
{
	g_bGoingSomewhereChosen = bChoosing;
}

void Continue_NoteJoined( void )
{
	// [rc4l] We are in. Whatever return brought us here is finished, so a later disconnect is the
	// player leaving rather than our own attempt coming apart.
	g_bReturnInFlight = false;

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

	record.stamp = NextStamp( );
	WriteRecord( record, ServerRecordPath( ));
}

void Continue_Forget( void )
{
	remove( OfflineRecordPath( ).c_str( ));
	remove( ServerRecordPath( ).c_str( ));
	g_Offline = ContinueRecord( );
	g_Server = ContinueRecord( );
	g_bLoaded = true;

	// The records changed without being re-read, so say so: anything derived from them is now about
	// two records that no longer exist.
	++g_LoadGeneration;
}

int Continue_RecordKind( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	switch ( Chosen( ).kind )
	{
	case ContinueKind::Single: return 1;
	case ContinueKind::Server: return 2;
	case ContinueKind::Hosted: return 3;
	default:                   return 0;
	}
}

const char *Continue_RecordTarget( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	const ContinueRecord &rec = Chosen( );
	return ( rec.kind == ContinueKind::Server ) ? rec.address.c_str( ) : rec.mapName.c_str( );
}

bool Continue_DebugSaveExists( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	return FileExists( Chosen( ).savePath );
}

int Continue_DebugSaveVersion( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	return SaveVersionOf( Chosen( ).savePath );
}

bool Continue_DebugBusy( void )
{
	return ( gamestate == GS_LEVEL ) && usergame;
}

// [rc4l] Survives the reload, because RequestReload throws CRestartException and that unwinds inside
// the SAME process -- the trick zx_joinserver already relies on to know a connect was one it started.
bool g_bLoadAfterRestart = false;
FString g_SaveAfterRestart;

// Both defined below, beside the records they act on.
static void RehostRecorded( void );
static void StartAndJoinRecordedHost( void );
static void ActivateOfflineRecord( const ContinueRecord &rec );

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
	if (( g_Server.kind == ContinueKind::Server ) && ( g_Probe == ServerProbe::Unknown ))
	{
		if ( g_bProbeSent == false )
		{
			NETADDRESS_s address;
			if ( address.LoadFromString( g_Server.address.c_str( )))
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

	// [rc4l] Join a server we restarted, the moment it is actually listening. HostTakeReadyEdge is a
	// one-shot on purpose: a level would have this trying to join again on every frame afterwards,
	// on a connection it already has.
	if ( g_bJoinRehostWhenReady && HostTakeReadyEdge( ))
	{
		g_bJoinRehostWhenReady = false;

		const FString address = HostConnectAddress( );
		if ( address.IsNotEmpty( ))
		{
			FString command;
			command.Format( "connect %s", address.GetChars( ));
			AddCommandString( command.LockBuffer( ));
			command.UnlockBuffer( );
		}
	}

	// [rc4l] The return the departure gate asked for, now that the teardown is over. Same
	// destination as pressing Disconnect, because leaving is leaving however it happened.
	// [rc4l] And not until the engine has settled. A load is queued as a gameaction, so performing
	// it while the teardown still has one of its own pending means ours is quietly overwritten --
	// which is exactly what a kick does, with ga_fullconsole. Waiting for ga_nothing costs a frame
	// or two and is the difference between returning and appearing to do nothing at all.
	ContinueReturnInputs ret;
	ret.pending = g_bReturnPending;
	ret.inSession = InSession( );
	ret.engineIdle = ( gameaction == ga_nothing );

	if ( DecideContinueReturn( ret ) == ContinueReturnStep::Perform )
	{
		g_bReturnPending = false;

		if ( g_ReturnTarget == ContinueTarget::Hosted )
			RehostRecorded( );
		else if ( g_ReturnTarget == ContinueTarget::Offline )
			ActivateOfflineRecord( g_Offline );
		// MainMenu: the disconnect has already put us there.

		g_ReturnTarget = ContinueTarget::None;
	}

	// [rc4l] The other side of a reload the rehost asked for. Same shape as the load below, and the
	// same reason for waiting: there is no point starting a server before the engine can join one.
	if ( g_bHostAfterRestart )
	{
		if ( gamestate == GS_STARTUP )
			return;

		g_bHostAfterRestart = false;
		StartAndJoinRecordedHost( );
		return;
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

int Continue_DebugDepartCalls( void ) { return g_DepartCalls; }
int Continue_DebugDepartReturns( void ) { return g_DepartReturns; }
bool Continue_DebugReturnPending( void ) { return g_bReturnPending; }

int Continue_DebugProbeSlot( void )
{
	return static_cast<int>( g_ProbeSlot );
}

// [rc4l] Start the game we recorded hosting, silently. A fresh match on the same terms: the world
// lived in the child process and went with it, so there is nothing to restore but the settings.
static void StartAndJoinRecordedHost( void )
{
	HostConfig config = g_Offline.host;
	config.rconSecret.clear( );		// minted fresh by HostStart; the stored one died with its process

	if ( HostStart( config ) == false )
	{
		Printf( "Continue: could not start that server again.\n" );
		return;
	}

	// Spawning it is only half. The child is not listening yet, so the join waits for the ready edge.
	g_bJoinRehostWhenReady = true;

	// [rc4l] From here until we are actually in, this is a return of ours in flight. If the join is
	// refused, the disconnect that follows must not read as the player leaving a server and ask for
	// another rehost -- see continuedepart_compute.
	g_bReturnInFlight = true;
}

static void RehostRecorded( void )
{
	const ContinueRecord &rec = g_Offline;

	// Names into paths, so "found" means found rather than named. Same resolution the offline path
	// uses, for the same reason: a bare name is not a file.
	TArray<FString> pwads;
	FString iwad;
	const bool bAllFound = ResolveRecordFiles( rec, pwads, iwad );

	ContinueRehostInputs in;
	in.filesFound = bAllFound;
	in.filesMatchOurs = SameWadSetAsRecord( rec );

	switch ( DecideContinueRehost( in ))
	{
	case ContinueRehostStep::Host:
		StartAndJoinRecordedHost( );
		return;

	case ContinueRehostStep::RefuseMissing:
		Printf( "Continue: the files that server used are no longer on this machine.\n" );
		return;

	case ContinueRehostStep::ReloadThenHost:
		break;
	}

	// [rc4l] Our files are not that server's files, so joining it as we are is refused before it
	// starts. Restart onto them first and host on the way back up: the reload throws and unwinds
	// inside this same process, so the flag below survives it.
	g_bHostAfterRestart = true;

	const wadreload::ReloadResult r = wadreload::RequestReload(
		iwad.IsEmpty( ) ? NULL : iwad.GetChars( ), pwads );

	// Only reached when it did NOT restart -- the restarting case throws past here and the flag is
	// read on the way back up.
	g_bHostAfterRestart = false;

	if ( r == wadreload::ReloadResult::AlreadyLoaded )
		StartAndJoinRecordedHost( );
	else if ( r == wadreload::ReloadResult::InvalidWads )
		Printf( "Continue: the files that server used are no longer loadable.\n" );
}

// [rc4l] Put a recorded local session back: the right WAD set, then the snapshot.
//
// Shared by both directions on purpose. Pressing Continue at the menu and disconnecting from a
// server both mean "put me back where I was", and two implementations of that would be two things
// to keep in step.
static void ActivateOfflineRecord( const ContinueRecord &rec )
{
	// [rc4l] Only reload when the set is actually wrong. The ordinary case by far is relaunching the
	// same way and pressing Continue, and asking RequestReload to prove that costs a full validation
	// against search paths the record cannot know -- our IWAD is remembered by bare name, so a copy
	// living outside the search path fails the check and refuses a reload nothing needed.
	if ( SameWadSetAsRecord( rec ) )
	{
		G_LoadGame( rec.savePath.c_str( ));
		return;
	}

	// Names into paths before anything is asked to load them; see ResolveWad.
	TArray<FString> pwads;
	FString iwad;
	const bool bAllFound = ResolveRecordFiles( rec, pwads, iwad );

	if ( bAllFound == false )
	{
		Printf( "Continue: the files that session used are no longer on this machine.\n" );
		return;
	}

	g_SaveAfterRestart = rec.savePath.c_str( );

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

// [rc4l] Go where the verdict points. One switch rather than one per entry point: leaving a server
// and pressing the pill at a menu differ only in whether a connection has to be dropped first, and
// when they were written separately the menu path had no Hosted case at all -- pressing Continue on
// a game you had hosted quietly tried to load a savegame that a hosted record never has.
static void GoTo( ContinueTarget target )
{
	switch ( target )
	{
	case ContinueTarget::Hosted:
		RehostRecorded( );
		return;

	case ContinueTarget::Offline:
		ActivateOfflineRecord( g_Offline );
		return;

	case ContinueTarget::Server:
	{
		// Down the join path the browser already uses, so a failure lands where every other failed
		// join lands rather than inventing a second way to go wrong.
		FString command;
		command.Format( "connect %s", g_Server.address.c_str( ));
		AddCommandString( command.LockBuffer( ));
		command.UnlockBuffer( );
		return;
	}

	case ContinueTarget::MainMenu:
	case ContinueTarget::None:
		// Nothing to do: a disconnect has already put us at the menu, and None is not offered.
		return;
	}
}

void Continue_Activate( void )
{
	if ( Continue_IsShown( ) == false )
		return;

	// [rc4l] Straight in, no confirmation: the decision was made when the button chose to exist.
	M_ClearMenus( );

	const ContinueTarget target = Verdict( ).target;

	// Leaving a server: drop the connection first. Where we go afterwards is the same question it
	// always is, so it is the same answer.
	if ( Continue_IsDisconnect( ))
		CLIENT_QuitNetworkGame( NULL );

	GoTo( target );
}


} // namespace zx

//*****************************************************************************
//
// [rc4l] Press the pill from the console.
//
// The button was reachable only by clicking it, which meant the one thing the whole feature does
// could not be asserted without driving a menu -- and every bug found in it so far has been in what
// happens AFTER the press. Same reasoning as fua_hostmap.
CCMD( fua_continue )
{
	if ( zx::Continue_IsShown( ) == false )
	{
		Printf( "fua_continue: nothing to continue.\n" );
		return;
	}

	Printf( "fua_continue: %s\n", zx::Continue_Tooltip( ));
	zx::Continue_Activate( );
}
