// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/continue/zx_continue.h"

#include "features/continue/computation/continuerecord_compute.h"
#include "features/continue/computation/continuebutton_compute.h"
#include "features/continue/computation/continuedepart_compute.h"
#include "features/continue/computation/continuehistory_compute.h"
#include "features/continue/computation/continuerehost_compute.h"
#include "features/continue/computation/continuereturn_compute.h"
#include "features/continue/computation/continueshow_compute.h"
#include "features/continue/computation/continuewrite_compute.h"
#include "features/identity/zx_identity.h"
#include "features/server-hosting/zx_hosting.h"
#include "features/wadreload/zx_wadreload.h"
#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_waddownload.h"

#include "c_cvars.h"
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

#include <ctime>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// [rc4l] How many things back the history goes. Archived and global, because it is a preference
// about the player rather than about the game they happen to have loaded.
CUSTOM_CVAR( Int, cl_fua_continue_history, 10, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )
{
	// Pulled into range in the cvar itself, so a value typed at the console is corrected where the
	// player can see it rather than silently meaning something else everywhere it is read.
	const int clamped = zx::ClampContinueHistoryLimit( self );
	if ( self != clamped )
		self = clamped;
}

namespace zx
{

namespace
{

// [rc4l] Everything worth going back to, newest first. See continuehistory_compute for why it is a
// list rather than the two records it grew out of.
std::vector<ContinueRecord> g_History;
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
// [rc4l] The entry a departure decided to return to, COPIED rather than pointed at. The history is
// rewritten while the teardown runs -- leaving a server records that we were in it -- so an index or
// a pointer into it names something different by the time the return is performed.
ContinueRecord g_ReturnRecord;
// The settings a rehost is on its way to, held across the WAD reload that gets us there.
ContinueRecord g_PendingHost;
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

std::string HistoryPath()
{
	return ContinueHistoryPath( Identity_ConfigRoot( ), Instance( ));
}

// Whole file into a string, or empty when there is not one.
std::string ReadFile( const std::string &path )
{
	std::string text;

	FILE *f = fopen( path.c_str( ), "rb" );
	if ( f == NULL )
		return text;

	char buffer[1024];
	size_t got;
	while (( got = fread( buffer, 1, sizeof buffer, f )) > 0 )
		text.append( buffer, got );
	fclose( f );

	return text;
}

ContinueRecord ReadRecord( const std::string &path )
{
	ContinueRecord parsed;
	ContinueRecord out;
	return ParseContinue( ReadFile( path ), parsed ) ? parsed : out;
}

// How many entries the player asked to keep, as a number this code may act on.
int HistoryLimit()
{
	return ClampContinueHistoryLimit( *cl_fua_continue_history );
}

// [rc4l] Now, for the "last played" column. The only clock in the feature: the ORDER still comes
// from the stamp, so a machine whose time is wrong cannot reshuffle the list.
long long Now()
{
	return static_cast<long long>( time( NULL ));
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

void WriteFile( const std::string &path, const std::string &text )
{
	EnsureDir( );

	FILE *f = fopen( path.c_str( ), "wb" );
	if ( f == NULL )
		return;

	fwrite( text.data( ), 1, text.size( ), f );
	fclose( f );
}

// [rc4l] The snapshots of entries that have just fallen off the end of the list.
//
// Deleted from the DIFFERENCE between the two lists rather than by scanning the folder for files
// nothing points at. A scan would also find the snapshot another copy of the engine is holding, or
// one being written right now, and deciding those are orphans deletes somebody's game. The pair of
// lists says exactly which rows this write dropped, and nothing else is anybody's business.
void RemoveDroppedSnapshots( const std::vector<ContinueRecord> &before,
	const std::vector<ContinueRecord> &after )
{
	for ( size_t i = 0; i < before.size( ); ++i )
	{
		if (( before[i].kind != ContinueKind::Single ) || before[i].savePath.empty( ))
			continue;

		bool bStillReferenced = false;
		for ( size_t j = 0; j < after.size( ); ++j )
		{
			if ( after[j].savePath == before[i].savePath )
			{
				bStillReferenced = true;
				break;
			}
		}

		if ( bStillReferenced == false )
			remove( before[i].savePath.c_str( ));
	}
}

void WriteHistory( const std::vector<ContinueRecord> &history )
{
	WriteFile( HistoryPath( ), SerialiseContinueHistory( history ));

	// Anything derived from the list is now about a list that no longer exists.
	++g_LoadGeneration;
}

// [rc4l] Remember this session, at the top of the list.
//
// One door in, so the stamp, the clock, the dedupe, the cap and the snapshot tidy-up cannot be got
// right in one caller and wrong in the next.
void NoteRecord( ContinueRecord record )
{
	if ( record.kind == ContinueKind::None )
		return;

	if ( g_bLoaded == false )
		Continue_Load( );

	record.stamp = NextContinueStamp( g_History );
	record.playedAt = Now( );

	const std::vector<ContinueRecord> before = g_History;
	g_History = InsertContinueEntry( before, record, HistoryLimit( ));

	WriteHistory( g_History );
	RemoveDroppedSnapshots( before, g_History );
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
// [rc4l] What we have learned about an address, remembered per address rather than one answer for
// the feature.
//
// The list can hold several servers now, and asking all of them the moment the menu opens is a query
// storm aimed at other people's machines for rows nobody may click. So: ONE query in flight at a
// time, started for the entry the pill would act on and for whichever row the player selects, and
// every answer kept so a row is only ever asked once.
struct ProbeResult
{
	std::string address;
	ServerProbe state;
};

std::vector<ProbeResult> g_Probes;
std::string g_ProbeAddress;			// the one being asked right now; empty when nothing is
bool g_bProbeSent = false;
unsigned int g_ProbeStartedMS = 0;
LONG g_ProbeSlot = -1;

// How long to let it answer before calling it gone. A query and its reply on a working link is
// milliseconds; this is loose enough for a slow one and short enough that the button does not sit
// there offering a dead server for the whole time somebody reads the menu.
const unsigned int kProbeTimeoutMS = 4000;

ServerProbe ProbeStateFor( const std::string &address )
{
	for ( size_t i = 0; i < g_Probes.size( ); ++i )
	{
		if ( g_Probes[i].address == address )
			return g_Probes[i].state;
	}

	// Never asked, which is not the same as no answer: an unasked server is offered, and only a
	// definite refusal takes the row away. See continueshow_compute.
	return ServerProbe::Unknown;
}

void SetProbeState( const std::string &address, ServerProbe state )
{
	for ( size_t i = 0; i < g_Probes.size( ); ++i )
	{
		if ( g_Probes[i].address == address )
		{
			g_Probes[i].state = state;
			++g_LoadGeneration;			// the pill's answer may have just changed
			return;
		}
	}

	ProbeResult fresh;
	fresh.address = address;
	fresh.state = state;
	g_Probes.push_back( fresh );
	++g_LoadGeneration;
}

// Same game, judged the way the record was written: bare PWAD names, in order.
bool ProbeWadsMatch( ULONG slot, const std::vector<ContinueRecord::Wad> &wads )
{
	const LONG count = BROWSER_GetNumPWADs( slot );
	if ( count < 0 )
		return true;					// it told us nothing, so it has not contradicted us

	if ( static_cast<size_t>( count ) != wads.size( ) )
		return false;

	for ( LONG i = 0; i < count; ++i )
	{
		const char *name = BROWSER_GetPWADName( slot, i );
		if (( name == NULL ) || ( wads[i].name != name ))
			return false;
	}

	return true;
}

// Addresses waiting their turn. One question in flight at a time; see the ProbeResult comment.
std::vector<std::string> g_ProbeQueue;

void RequestProbe( const std::string &address )
{
	if ( address.empty( ))
		return;

	// Asked once, ever. A settled answer is not re-checked and an unsettled one is not asked twice.
	if ( ProbeStateFor( address ) != ServerProbe::Unknown )
		return;
	if ( g_ProbeAddress == address )
		return;

	for ( size_t i = 0; i < g_ProbeQueue.size( ); ++i )
	{
		if ( g_ProbeQueue[i] == address )
			return;
	}

	g_ProbeQueue.push_back( address );
}

// The entry an answer belongs to, so its reply can be judged against the files that entry recorded.
const ContinueRecord *EntryForAddress( const std::string &address )
{
	for ( size_t i = 0; i < g_History.size( ); ++i )
	{
		if (( g_History[i].kind == ContinueKind::Server ) && ( g_History[i].address == address ))
			return &g_History[i];
	}
	return NULL;
}

void TickProbe( void )
{
	if ( g_ProbeAddress.empty( ))
	{
		if ( g_ProbeQueue.empty( ))
			return;

		g_ProbeAddress = g_ProbeQueue[0];
		g_ProbeQueue.erase( g_ProbeQueue.begin( ));
		g_bProbeSent = false;
		g_ProbeSlot = -1;
	}

	if ( g_bProbeSent == false )
	{
		NETADDRESS_s address;
		if ( address.LoadFromString( g_ProbeAddress.c_str( )) == false )
		{
			// An address we cannot even read is not one anything could have reached.
			SetProbeState( g_ProbeAddress, ServerProbe::Gone );
			g_ProbeAddress.clear( );
			return;
		}

		BROWSER_AddServerToList( address );
		g_ProbeSlot = BROWSER_GetListIDByAddress( address );
		if ( g_ProbeSlot >= 0 )
			BROWSER_RecheckServer( g_ProbeSlot );

		g_ProbeStartedMS = I_MSTime( );
		g_bProbeSent = true;
		return;
	}

	if (( g_ProbeSlot >= 0 ) && BROWSER_IsActive( g_ProbeSlot ))
	{
		// The entry may have fallen off the list while we waited, in which case there is nothing to
		// contradict and the answer is simply that it is up.
		const ContinueRecord *rec = EntryForAddress( g_ProbeAddress );
		const bool bSameGame = ( rec == NULL ) || ProbeWadsMatch( g_ProbeSlot, rec->wads );

		SetProbeState( g_ProbeAddress, bSameGame ? ServerProbe::Alive : ServerProbe::WadsDiffer );
		g_ProbeAddress.clear( );
	}
	else if ( I_MSTime( ) - g_ProbeStartedMS > kProbeTimeoutMS )
	{
		SetProbeState( g_ProbeAddress, ServerProbe::Gone );
		g_ProbeAddress.clear( );
	}
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

// [rc4l] Everything the player had before this build existed, moved into the list.
//
// Only ever reached when there is no history file at all. Once one has been written -- even an empty
// one -- it is the answer, so somebody who clears their history does not find it back on the next
// launch. The two old files are deleted afterwards for the same reason.
static void MigrateLegacyRecords( void )
{
	const ContinueRecord offline = ReadRecord( OfflineRecordPath( ));
	const ContinueRecord server = ReadRecord( ServerRecordPath( ));

	// Oldest first, so the stamps they already carry put them in the order the player lived them.
	const bool bServerFirst = ( server.stamp <= offline.stamp );

	const ContinueRecord *order[2];
	order[0] = bServerFirst ? &server : &offline;
	order[1] = bServerFirst ? &offline : &server;

	for ( int i = 0; i < 2; ++i )
	{
		if ( order[i]->kind == ContinueKind::None )
			continue;

		ContinueRecord record = *order[i];

		// [rc4l] The clock is left at zero rather than set to now. These sessions happened at some
		// point in the past that nothing wrote down, and stamping them with the moment of the upgrade
		// would have the list claim the player was in all of them a second ago.
		record.playedAt = 0;
		g_History = InsertContinueEntry( g_History, record, HistoryLimit( ));
	}

	WriteHistory( g_History );

	// [rc4l] Gone, so a later launch cannot migrate them a second time on top of whatever the player
	// has done since. The snapshot they point at is NOT deleted: the offline record still names it
	// and the entry that came from it still leads there.
	remove( OfflineRecordPath( ).c_str( ));
	remove( ServerRecordPath( ).c_str( ));
}

void Continue_Load( void )
{
	g_bLoaded = true;
	++g_LoadGeneration;

	g_History.clear( );

	std::vector<ContinueRecord> parsed;
	if ( ParseContinueHistory( ReadFile( HistoryPath( )), parsed ))
	{
		// Trimmed on the way OUT as well as the way in, so lowering the setting takes effect at the
		// next launch rather than waiting for the next thing the player happens to play.
		g_History = TrimContinueHistory( parsed, HistoryLimit( ));
		return;
	}

	MigrateLegacyRecords( );
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

	// [rc4l] Deliberately NOT the probe. This asks whether the entry is structurally sound -- a
	// snapshot that is gone, or too old for this build -- which is a fact about our own disk and
	// cannot change while the player looks at it.
	in.probe = ServerProbe::Alive;

	return ContinueIsShown( in );
}

// [rc4l] Whether it is worth pressing ONE BUTTON for, which is a stricter question.
//
// A server that has been asked and did not answer stays in the LIST, dimmed and labelled, because
// rows that vanish from under a pointer are how a click lands on something the player did not read.
// But the pill acts without asking, and a button that is offered and then fails is worse than no
// button: the player has already decided by the time it fails.
static bool RecordOfferable( const ContinueRecord &rec )
{
	if ( RecordUsable( rec ) == false )
		return false;

	if ( rec.kind != ContinueKind::Server )
		return true;

	const ServerProbe probe = ProbeStateFor( rec.address );
	return ( probe != ServerProbe::Gone ) && ( probe != ServerProbe::WadsDiffer );
}

// [rc4l] The rows worth offering, as indices into the history.
//
// Indices rather than copies so a caller can act on one, and asked fresh rather than cached: a
// snapshot that has been deleted or a probe that has come back changes this answer without anything
// having been written.
static std::vector<int> UsableEntries( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	std::vector<int> out;
	for ( size_t i = 0; i < g_History.size( ); ++i )
	{
		if ( RecordUsable( g_History[i] ))
			out.push_back( static_cast<int>( i ));
	}
	return out;
}

// The rows one press may act on. A subset of the above; see RecordOfferable.
static std::vector<int> OfferableEntries( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	std::vector<int> out;
	for ( size_t i = 0; i < g_History.size( ); ++i )
	{
		if ( RecordOfferable( g_History[i] ))
			out.push_back( static_cast<int>( i ));
	}
	return out;
}

// The kind of thing an entry is, as the target that would act on it.
static ContinueTarget TargetOf( const ContinueRecord &rec )
{
	switch ( rec.kind )
	{
	case ContinueKind::Server: return ContinueTarget::Server;
	case ContinueKind::Hosted: return ContinueTarget::Hosted;
	case ContinueKind::Single: return ContinueTarget::Offline;
	default:                   return ContinueTarget::None;
	}
}

// [rc4l] The newest usable entry that is NOT a server, which is where LEAVING lands.
//
// A different question from "the newest entry", and it has to be: joining a server records that we
// are in it, so the newest entry during a session is the very server the player is asking to leave.
static int NewestLocalEntry( void )
{
	const std::vector<int> usable = OfferableEntries( );

	for ( size_t i = 0; i < usable.size( ); ++i )
	{
		if ( g_History[usable[i]].kind != ContinueKind::Server )
			return usable[i];
	}
	return -1;
}

// Everything the pill's decision rests on, gathered in one place so the label, the action and
// whether it is drawn at all cannot disagree about the answer.
static ContinueButtonVerdict Verdict( void )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	// [rc4l] Worked out once per change rather than once per frame. The header asks the pill for its
	// label and whether to draw it every frame, and answering honestly means a stat and, for every
	// snapshot in the list, opening the savegame and parsing its PNG header -- sixty times a second,
	// for an answer that only moves when one of these inputs does. With a history rather than two
	// records this matters more, not less: the work is now proportional to how much the player has
	// been playing.
	static ContinueButtonVerdict cached;
	static int cachedGeneration = -1;
	static bool cachedInSession = false;

	const bool inSession = InSession( );

	// The generation covers the probes too: every answer that lands bumps it.
	if (( cachedGeneration == g_LoadGeneration ) && ( cachedInSession == inSession ))
		return cached;

	const std::vector<int> offerable = OfferableEntries( );
	const int local = NewestLocalEntry( );

	ContinueButtonInputs in;
	in.inSession = inSession;
	in.usableCount = static_cast<int>( offerable.size( ));
	in.newestTarget = offerable.empty( ) ? ContinueTarget::None
		: TargetOf( g_History[offerable[0]] );
	in.localUsable = ( local >= 0 );
	in.localIsHosted = ( local >= 0 ) && ( g_History[local].kind == ContinueKind::Hosted );

	cached = DecideContinueButton( in );
	cachedGeneration = g_LoadGeneration;
	cachedInSession = inSession;

	return cached;
}

// [rc4l] The entry ONE press would act on: the newest usable row. Everything that used to read a
// stored winner reads this.
static const ContinueRecord &Chosen( void )
{
	const std::vector<int> offerable = OfferableEntries( );
	return offerable.empty( ) ? g_Nothing : g_History[offerable[0]];
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
		const int local = NewestLocalEntry( );

		if (( v.target == ContinueTarget::Hosted ) && ( local >= 0 ))
			g_Tooltip.Format( "Leave and go back to hosting %s", g_History[local].host.map.c_str( ));
		else if (( v.target == ContinueTarget::Offline ) && ( local >= 0 ))
			g_Tooltip.Format( "Leave and go back to %s", g_History[local].mapName.c_str( ));
		else
			g_Tooltip = "Leave and go back to the main menu";

		return g_Tooltip.GetChars( );
	}

	// [rc4l] With more than one thing to go back to the press opens a list, so the tooltip has to
	// say that rather than name a single destination -- a pill that promised one server and then
	// showed a menu would be describing the row instead of the button.
	if ( Verdict( ).opensList )
	{
		g_Tooltip.Format( "Pick up where you left off (%d to choose from)",
			static_cast<int>( OfferableEntries( ).size( )));
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
	if ( g_bLoaded == false )
		Continue_Load( );

	ContinueRecord record;
	record.kind = ContinueKind::Single;
	record.saveVersion = SAVEVER;
	record.mapName = level.MapName.GetChars( );

	record.mapWad = MapWadName( level.MapName.GetChars( ));

	CollectLoadedWads( record );

	// [rc4l] Where the snapshot goes, which depends on whether this session is a row the list
	// already has.
	//
	// Coming back to a map already in the history OVERWRITES that row's snapshot, because the row is
	// about to be replaced and its old save would otherwise be left behind with nothing pointing at
	// it. Anything else gets a slot of its own, named after the stamp it is about to be written with
	// -- unique and increasing already, so no second counter has to be kept in step with the first.
	const ContinueRecord *existing = FindContinueEntry( g_History, ContinueIdentity( record ));
	if (( existing != NULL ) && ( existing->savePath.empty( ) == false ))
		record.savePath = existing->savePath;
	else
		record.savePath = ContinueSaveSlotPath( Identity_ConfigRoot( ), Instance( ),
			NextContinueStamp( g_History ));

	// [rc4l] G_DoSaveGame and not G_SaveGame, which only sets gameaction = ga_savegame and leaves the
	// work to the next tic. There is no next tic: the caller is the quit, and exit() follows
	// immediately. The queued form wrote the record and never the snapshot, so Continue pointed at a
	// file that would never exist and hid itself forever -- safe, and useless.
	EnsureDir( );

	G_DoSaveGame( false, record.savePath.c_str( ), "Continue" );
	NoteRecord( record );
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

	// [rc4l] The files WE were holding, not just the ones the server was told to load. A rehost has
	// to put this process back on them before it can join anything, and by name alone it cannot: the
	// hashes are what turn a remembered set into files on this machine. Free here -- the same list
	// the offline path records, already digested at startup.
	CollectLoadedWads( record );

	NoteRecord( record );
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

	// Where to, decided now while the answer is still the leaving one -- and the record COPIED, not
	// indexed: this very teardown is about to record the server we are leaving, which rewrites the
	// list the index would have pointed into.
	const int local = NewestLocalEntry( );

	ContinueButtonInputs where;
	where.inSession = true;
	where.localUsable = ( local >= 0 );
	where.localIsHosted = ( local >= 0 ) && ( g_History[local].kind == ContinueKind::Hosted );

	g_ReturnTarget = DecideContinueButton( where ).target;
	g_ReturnRecord = ( local >= 0 ) ? g_History[local] : ContinueRecord( );

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

	NoteRecord( record );
}

void Continue_Forget( void )
{
	const std::vector<ContinueRecord> before = g_History;

	g_History.clear( );
	g_bLoaded = true;

	// [rc4l] An empty history is WRITTEN rather than the file deleted. A missing file means "never
	// had one" and sends the next launch off to migrate the old records, so deleting it would hand
	// the player back the very sessions they just cleared.
	WriteHistory( g_History );
	RemoveDroppedSnapshots( before, g_History );

	// And the records this feature grew out of, in case one is still sitting there unmigrated.
	remove( OfflineRecordPath( ).c_str( ));
	remove( ServerRecordPath( ).c_str( ));
}

void Continue_ForgetEntry( int index )
{
	if ( g_bLoaded == false )
		Continue_Load( );

	if (( index < 0 ) || ( index >= static_cast<int>( g_History.size( ))))
		return;

	const std::vector<ContinueRecord> before = g_History;
	g_History = RemoveContinueEntry( before, index );

	WriteHistory( g_History );
	RemoveDroppedSnapshots( before, g_History );
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
static void RehostRecorded( const ContinueRecord &rec );
static void StartAndJoinRecordedHost( const ContinueRecord &rec );
static void ActivateOfflineRecord( const ContinueRecord &rec );

void Continue_Tick( void )
{
	// [rc4l] The record has to be in hand before any of this means anything. Without it every tick
	// saw kind None, the probe never started, and the button sat there offering a server nobody had
	// asked about -- which looked exactly like a probe that had run and found it alive.
	if ( g_bLoaded == false )
		Continue_Load( );

	// [rc4l] The row ONE press would act on is asked without being clicked, exactly as the single
	// record was: the answer is then usually in before the player decides. The rest of the list is
	// asked only when it is selected -- querying fifty of other people's servers the moment a menu
	// opens is a storm sent on behalf of rows nobody may ever look at.
	for ( size_t i = 0; i < g_History.size( ); ++i )
	{
		if ( g_History[i].kind == ContinueKind::Server )
		{
			RequestProbe( g_History[i].address );
			break;
		}
	}

	TickProbe( );

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
			RehostRecorded( g_ReturnRecord );
		else if ( g_ReturnTarget == ContinueTarget::Offline )
			ActivateOfflineRecord( g_ReturnRecord );
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
		StartAndJoinRecordedHost( g_PendingHost );
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
	if ( g_bLoaded == false )
		Continue_Load( );

	// What we know about the server the pill would act on, which is the one asked without being
	// clicked. A row further down the list has its own answer; see Continue_EntryProbe.
	for ( size_t i = 0; i < g_History.size( ); ++i )
	{
		if ( g_History[i].kind != ContinueKind::Server )
			continue;

		switch ( ProbeStateFor( g_History[i].address ))
		{
		case ServerProbe::Alive:      return 1;
		case ServerProbe::Gone:       return 2;
		case ServerProbe::WadsDiffer: return 3;
		default:                      return 0;
		}
	}

	return 0;
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
static void StartAndJoinRecordedHost( const ContinueRecord &rec )
{
	HostConfig config = rec.host;
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

static void RehostRecorded( const ContinueRecord &rec )
{
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
		StartAndJoinRecordedHost( rec );
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
	// Held across the restart, because the list on the other side of it is re-read from disk and the
	// record we were handed is a local that will not survive the unwind.
	g_PendingHost = rec;
	g_bHostAfterRestart = true;

	const wadreload::ReloadResult r = wadreload::RequestReload(
		iwad.IsEmpty( ) ? NULL : iwad.GetChars( ), pwads );

	// Only reached when it did NOT restart -- the restarting case throws past here and the flag is
	// read on the way back up.
	g_bHostAfterRestart = false;

	if ( r == wadreload::ReloadResult::AlreadyLoaded )
		StartAndJoinRecordedHost( rec );
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
// [rc4l] Go to one particular entry. One switch rather than one per entry point: leaving a server
// and picking a row from the list differ only in whether a connection has to be dropped first, and
// when they were written separately the menu path had no Hosted case at all -- pressing Continue on
// a game you had hosted quietly tried to load a savegame that a hosted record never has.
static void GoToRecord( const ContinueRecord &rec )
{
	switch ( rec.kind )
	{
	case ContinueKind::Hosted:
		RehostRecorded( rec );
		return;

	case ContinueKind::Single:
		ActivateOfflineRecord( rec );
		return;

	case ContinueKind::Server:
	{
		// Down the join path the browser already uses, so a failure lands where every other failed
		// join lands rather than inventing a second way to go wrong.
		FString command;
		command.Format( "connect %s", rec.address.c_str( ));
		AddCommandString( command.LockBuffer( ));
		command.UnlockBuffer( );
		return;
	}

	default:
		// Nothing to continue is nothing to do; a disconnect has already put us at the menu.
		return;
	}
}

int Continue_HistoryCount( void )
{
	return static_cast<int>( UsableEntries( ).size( ));
}

// [rc4l] The history as the LIST SEES IT: only rows worth offering, in the order they are shown.
//
// The menu indexes this, not the raw history, so a row that has stopped being usable cannot be
// pressed by an index that used to mean something else.
static const ContinueRecord *EntryAt( int index )
{
	const std::vector<int> usable = UsableEntries( );
	if (( index < 0 ) || ( index >= static_cast<int>( usable.size( ))))
		return NULL;

	return &g_History[usable[index]];
}

const char *Continue_EntryLabel( int index )
{
	static FString label;

	const ContinueRecord *rec = EntryAt( index );
	label = ( rec != NULL ) ? ContinueEntryLabel( *rec ).c_str( ) : "";
	return label.GetChars( );
}

const char *Continue_EntryWhen( int index )
{
	static FString when;

	const ContinueRecord *rec = EntryAt( index );
	when = ( rec != NULL ) ? FormatLastPlayed( Now( ), rec->playedAt ).c_str( ) : "";
	return when.GetChars( );
}

int Continue_EntryKind( int index )
{
	const ContinueRecord *rec = EntryAt( index );
	if ( rec == NULL )
		return 0;

	switch ( rec->kind )
	{
	case ContinueKind::Single: return 1;
	case ContinueKind::Server: return 2;
	case ContinueKind::Hosted: return 3;
	default:                   return 0;
	}
}

int Continue_EntryProbe( int index )
{
	const ContinueRecord *rec = EntryAt( index );
	if (( rec == NULL ) || ( rec->kind != ContinueKind::Server ))
		return 0;

	switch ( ProbeStateFor( rec->address ))
	{
	case ServerProbe::Alive:      return 1;
	case ServerProbe::Gone:       return 2;
	case ServerProbe::WadsDiffer: return 3;
	default:                      return 0;
	}
}

void Continue_ProbeEntry( int index )
{
	const ContinueRecord *rec = EntryAt( index );
	if (( rec != NULL ) && ( rec->kind == ContinueKind::Server ))
		RequestProbe( rec->address );
}

bool Continue_ActivateEntry( int index )
{
	const ContinueRecord *rec = EntryAt( index );
	if ( rec == NULL )
		return false;

	// [rc4l] COPIED before anything else happens. Everything below can rewrite the history --
	// leaving a server records it, a reload re-reads the file -- and the pointer would then name a
	// different row, or none.
	const ContinueRecord chosen = *rec;

	M_ClearMenus( );

	if ( InSession( ))
		CLIENT_QuitNetworkGame( NULL );

	GoToRecord( chosen );
	return true;
}

void Continue_Activate( void )
{
	if ( Continue_IsShown( ) == false )
		return;

	const ContinueButtonVerdict v = Verdict( );

	// [rc4l] More than one thing to go back to is a question, and the list is where it gets asked.
	// One thing is not: the press has already chosen, and a menu of a single row would turn the
	// one-press feature this started as into two presses for no decision.
	if ( v.opensList )
	{
		Continue_OpenList( );
		return;
	}

	// [rc4l] Straight in, no confirmation: the decision was made when the button chose to exist.
	M_ClearMenus( );

	// Leaving a server: drop the connection first. Where we go afterwards is the same question it
	// always is, so it is the same answer.
	if ( Continue_IsDisconnect( ))
	{
		CLIENT_QuitNetworkGame( NULL );

		const int local = NewestLocalEntry( );
		if (( v.target != ContinueTarget::MainMenu ) && ( local >= 0 ))
			GoToRecord( g_History[local] );
		return;
	}

	GoToRecord( Chosen( ));
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

	// [rc4l] With a row number it acts on that row, which is the only way to drive the list without
	// a menu -- and the list is where every bug in this feature has been found.
	if ( argv.argc( ) > 1 )
	{
		const int index = atoi( argv[1] );
		if ( zx::Continue_ActivateEntry( index ) == false )
			Printf( "fua_continue: there is no entry %d.\n", index );

		return;
	}

	Printf( "fua_continue: %s\n", zx::Continue_Tooltip( ));
	zx::Continue_Activate( );
}
