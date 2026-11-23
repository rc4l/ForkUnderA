// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: apply.cpp
//
// Description: The client's snapshot commands: the snapshot datagram
// and its ack, the door sound and the sound table. See apply.h.
//
//-----------------------------------------------------------------------------

#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "s_sound.h"
#include "s_sndseq.h"
#include "i_system.h"
#include "i_tictime.h"
#include "c_cvars.h"
#include "network.h"
#include "networkshared.h"
#include "cl_main.h"
#include "cl_demo.h"
#include "cl_commands.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/ghostactor.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/record.h"
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/ack_compute.h"
#include "features/net-snapshot/computation/sound_compute.h"
#include "network/servercommands.h"	// ServerCommands::SoundActor and SoundPoint, below
#include "features/net-snapshot/apply.h"

using namespace zx::netsnapshot;

//*****************************************************************************
//
// Applies a datagram too late to count, as before the drop, to compare the two
// (the checksum can then read DIVERGED).
CVAR( Bool, fua_csnap_debug_applylate, false, CVAR_NOSETBYACS )

// Seconds between this client's presence dumps to snappresence-cl<N>.txt, for
// tools/snapshot/presence_diff.py (0 off).
CVAR( Int, fua_csnap_debug_presencelog, 0, CVAR_NOSETBYACS )

static unsigned int g_tooLate = 0;
static unsigned int g_pastHistory = 0;
static unsigned int g_ackResets = 0;
static unsigned int g_ackRecoveries = 0;

unsigned int zx::netsnapshot::PastHistory( bool reset )
{
	const unsigned int n = g_pastHistory;
	if ( reset )
		g_pastHistory = 0;
	return n;
}

unsigned int zx::netsnapshot::AckRecoveries( bool reset )
{
	const unsigned int n = g_ackRecoveries;
	if ( reset )
		g_ackRecoveries = 0;
	return n;
}

unsigned int zx::netsnapshot::AckResets( bool reset )
{
	const unsigned int n = g_ackResets;
	if ( reset )
		g_ackResets = 0;
	return n;
}

// One presence dump when due: the newest frame applied, this player and the
// ids it holds.
static void client_PresenceLog( unsigned int nowMS )
{
	static unsigned int s_dueMS = 0;
	if (( fua_csnap_debug_presencelog <= 0 ) || ( static_cast<int>( nowMS - s_dueMS ) < 0 ))
		return;

	const unsigned int* ids = NULL;
	unsigned int count = 0;
	int frame = 0;
	if ( PresentIds( ids, count, frame ) == false )
		return;
	s_dueMS = nowMS + static_cast<unsigned int>( fua_csnap_debug_presencelog ) * 1000u;

	char name[40];
	mysnprintf( name, sizeof( name ), "snappresence-cl%d.txt", consoleplayer );
	FILE* f = fopen( SnapFilePath( name ).GetChars( ), "a" );
	if ( f == NULL )
		return;
	fprintf( f, "cl %d %d %u", frame, consoleplayer, count );
	for ( unsigned int k = 0; k < count; ++k )
		fprintf( f, " %u", ids[k] );
	fprintf( f, "\n" );
	fclose( f );
}

unsigned int zx::netsnapshot::TooLate( bool reset )
{
	const unsigned int n = g_tooLate;
	if ( reset )
		g_tooLate = 0;
	return n;
}

//*****************************************************************************
//
// How many datagrams before `seq` the newest missing one is, within the ack
// history's 32 and as the state stood before `seq`; 0 when none is missing.
static unsigned int client_SnapGapDistance( int seq, int lastSeq, unsigned int history )
{
	for ( int back = 1; back <= 32; back++ )
	{
		const int s = seq - back;
		// Datagram sequences start at 1.
		if ( s < 1 )
			break;
		if ( s == lastSeq )
			continue;

		const int b = lastSeq - s;
		if (( b <= 0 ) || ( b > 32 ) || (( history & ( 1u << ( b - 1 ))) == 0 ))
			return static_cast<unsigned int>( back );
	}
	return 0;
}

// The decode's share of the ghost report's timings, kept in ghostactor.cpp.
extern unsigned long long g_applyDeltaRaw;

//*****************************************************************************
//
// One snapshot datagram, decoded by the same code that reads recorded fixtures
// and acked (apply.md).
//
void zx::netsnapshot::OnSnapshot( BYTESTREAM_s * pByteStream )
{
	static TArray< BYTE > s_payload;

	const int frame = pByteStream->ReadLong( );
	const int gameTic = pByteStream->ReadLong( );
	const int seq = pByteStream->ReadLong( );
	const BYTE mapStamp = static_cast<BYTE>( pByteStream->ReadByte( ));
	const BYTE resetCount = static_cast<BYTE>( pByteStream->ReadByte( ));
	const BYTE ackLag = static_cast<BYTE>( pByteStream->ReadByte( ));
	const WORD reliableLow = static_cast<WORD>( pByteStream->ReadShort( ));
	const int size = pByteStream->ReadShort( );

	if (( size <= 0 ) || ( size > 8192 ))
	{
		// Nothing sane can follow; the stream position is already lost, so
		// just refuse the record rather than reading arbitrary length.
		CLIENT_PrintWarning( "OnSnapshot: implausible payload size %d\n", size );
		return;
	}

	if ( s_payload.Size() < static_cast<unsigned int>( size ))
		s_payload.Resize( static_cast<unsigned int>( size ));

	// Bounds checked here, as ReadBuffer's overflow path leaves the previous
	// packet's bytes in the buffer, which would decode cleanly.
	if ( pByteStream->pbStream + size > pByteStream->pbStreamEnd )
	{
		CLIENT_PrintWarning( "OnSnapshot: frame %d seq %d truncated (%d bytes claimed, %d available)\n",
		frame, seq, size, static_cast<int>( pByteStream->pbStreamEnd - pByteStream->pbStream ));
		pByteStream->pbStream = pByteStream->pbStreamEnd;
		return;
	}

	pByteStream->ReadBuffer( &s_payload[0], static_cast<size_t>( size ));

	// Ahead of the stamp drop, for a map change's reliable tail, but only once this connection
	// has joined, since a previous connection's datagram announces a sequence this one never sent.
	if (( CLIENTDEMO_IsPlaying( ) == false ) && ConnectionJoined( ))
		CLIENT_NoteReliableSequence( reliableLow );

	// Another map's datagram, or one before SnapJoin, is neither applied nor
	// acked.
	if ( AcceptMapStamp( mapStamp ) == false )
		return;

	// The ack state, read by the tests below before this datagram is folded in,
	// and begun again at each SnapJoin, the only place a new stream starts.
	static unsigned int s_lastAckSeq = 0;
	static unsigned int s_ackHistory = 0;
	static unsigned int s_joinSeen = 0;
	// Refusals past the history in a row, with nothing accepted between them.
	static unsigned int s_pastRun = 0;
	if ( JoinCount( ) != s_joinSeen )
	{
		s_joinSeen = JoinCount( );
		s_lastAckSeq = 0;
		s_ackHistory = 0;
		s_pastRun = 0;
		++g_ackResets;
	}

	// Too late to count, which the server repairs as never delivered, so
	// neither applied nor acked; past the history, apart.
	if (( seq > 0 ) && ComputeAckTooLate( static_cast<unsigned int>( seq ), s_lastAckSeq,
		s_ackHistory ))
	{
		const bool bPast = (( s_lastAckSeq - static_cast<unsigned int>( seq )) > SNAP_ACK_HISTORY );
		if ( bPast )
			++g_pastHistory;
		else
			++g_tooLate;
		// Nothing but refusals past the history means a newest sequence from
		// another stream, which only starting the ack state again can leave.
		if ( bPast && ( ++s_pastRun >= SNAP_ACK_HISTORY ))
		{
			CLIENT_PrintWarning( "OnSnapshot: %u datagrams in a row past the ack history;"
				" the ack state starts again at seq %d\n", s_pastRun, seq );
			s_pastRun = 0;
			s_lastAckSeq = 0;
			s_ackHistory = 0;
			++g_ackRecoveries;
		}
		else if ( fua_csnap_debug_applylate == false )
			return;
	}
	s_pastRun = 0;

	SetStreamVersion( ZSNAP_VERSION );

	char err[192];
	SnapApplyStats stats;
	const tictime_raw_t _ad0 = TIC_GetRawCounter( );
	const unsigned int arrivalMS = I_MSTime( );
	const unsigned int missingBack = client_SnapGapDistance( seq, static_cast<int>( s_lastAckSeq ),
	s_ackHistory );
	SetDatagramGapBehind( ComputeAckSettled( static_cast<unsigned int>( seq ), ackLag,
		missingBack ) == false );

	// The mover sink sorts this payload's records by it.
	MoverBeginDatagram( resetCount );
	const unsigned int errors = ApplyDelta(&s_payload[0], static_cast<unsigned int>(size),
	frame, gameTic, arrivalMS, err, sizeof(err), &stats);
	MoverEndDatagram( );

	// Recorded with the buffer and arrival time the decoder was given.
	if ( RecorderActive( ))
		RecorderDelta( &s_payload[0], static_cast< unsigned int>(size), frame, gameTic, arrivalMS );

	g_applyDeltaRaw += static_cast<unsigned long long>( TIC_GetRawCounter( ) - _ad0 );
	client_PresenceLog( arrivalMS );

	// The client's own copies of what just entered are hidden on the packet
	// clock, so a low frame rate cannot slow it.
	HideEnteredLegacy( );

	if ( errors != 0 )
		CLIENT_PrintWarning( "OnSnapshot: frame %d: %s\n", frame, err );

	// Live traffic is arriving: show the ghosts without requiring a cvar.
	GhostsFromNetwork( true );

	// [Kaori-Chani] And the fold, on this same clock, so the records are in each mirror's
	// smoothing before a frame draws them, whichever tic that frame falls in.
	GhostFoldApplied( arrivalMS );

	// The ack, by datagram sequence and never by frame, and sent even for a
	// rejected payload, which still arrived.
	ComputeAckFold( static_cast<unsigned int>( seq ), s_lastAckSeq, s_ackHistory );
	CLIENTCOMMANDS_SnapshotAck( static_cast<int>( s_lastAckSeq ), s_ackHistory,
		SoundIdsAnswer( ));
}

//*****************************************************************************
//
// Played only while the legacy door handlers are gated; otherwise they play
// their own from the DoDoor and ChangeDoorDirection they handle.
void zx::netsnapshot::OnDoorSound( BYTESTREAM_s *pByteStream )
{
	const int sectorID = pByteStream->ReadShort();
	const int choice = pByteStream->ReadByte();

	if (( sectorID < 0 ) || ( sectorID >= numsectors )
		|| ( MoverSuppressesLegacy( SNAPMV_DOOR ) == false ))
		return;

	sector_t *pSector = &sectors[sectorID];
	P_DoorSoundSector( pSector, choice & 3,
		( choice & 4 ) ? SN_CheckSequence( pSector, CHAN_CEILING ) : NULL );
}

//*****************************************************************************
// A ceiling's or a plat's sound, played only while that class's legacy handlers
// are gated. One kind per class, the plat's arriving with that class.
void zx::netsnapshot::OnMoverSound( BYTESTREAM_s *pByteStream )
{
	const int sectorID = pByteStream->ReadShort();
	const int packed = pByteStream->ReadByte();

	if (( sectorID < 0 ) || ( sectorID >= numsectors ))
		return;

	if (( SNAPSND_KIND( packed ) == SNAPSND_CEILING )
		&& MoverSuppressesLegacy( SNAPMV_CEILING ))
		P_CeilingSoundSector( &sectors[sectorID], SNAPSND_CHOICE( packed ));
	else if (( SNAPSND_KIND( packed ) == SNAPSND_PLAT )
		&& MoverSuppressesLegacy( SNAPMV_PLAT ))
		P_PlatSoundSector( &sectors[sectorID], SNAPSND_CHOICE( packed ));
	else if (( SNAPSND_KIND( packed ) == SNAPSND_ELEVATOR )
		&& MoverSuppressesLegacy( SNAPMV_ELEVATOR ))
		P_FloorSoundSector( &sectors[sectorID] );
	else if (( SNAPSND_KIND( packed ) == SNAPSND_PILLAR )
		&& MoverSuppressesLegacy( SNAPMV_PILLAR ))
		P_PillarSoundSector( &sectors[sectorID] );
}

//*****************************************************************************
// Played only while the legacy floor handlers are gated; otherwise the floor's
// own SVC_STARTFLOORSOUND carries it, which this client can key on its mover.
void zx::netsnapshot::OnFloorSound( BYTESTREAM_s *pByteStream )
{
	const int sectorID = pByteStream->ReadShort();

	if (( sectorID < 0 ) || ( sectorID >= numsectors )
		|| ( MoverSuppressesLegacy( SNAPMV_FLOOR ) == false ))
		return;

	P_FloorSoundSector( &sectors[sectorID] );
}

//*****************************************************************************
// The server's sound table for this map, whose ids are taken only if this
// client's first `count` names hash the same.
void zx::netsnapshot::OnSoundTable( BYTESTREAM_s *pByteStream )
{
	const BYTE stamp = static_cast<BYTE>( pByteStream->ReadByte( ));
	const unsigned int count = static_cast<WORD>( pByteStream->ReadShort( ));
	const unsigned int lo = static_cast<unsigned int>( pByteStream->ReadLong( ));
	const unsigned int hi = static_cast<unsigned int>( pByteStream->ReadLong( ));

	bool bMatch = ( count > 0 ) && ( count <= S_sfx.Size( ));
	if ( bMatch )
	{
		unsigned int myLo = 0, myHi = 0;
		SoundTableHash( count, myLo, myHi );
		bMatch = ( myLo == lo ) && ( myHi == hi );
	}
	SetSoundTable( stamp, bMatch, count, S_sfx.Size( ));
}

//*****************************************************************************
// A SoundActor or a SoundPoint by table index, played through those commands.
// The index means nothing unless this client matched the map's sound table.
void zx::netsnapshot::OnSnapSound( BYTESTREAM_s *pByteStream )
{
	BYTE wire[3];
	ClientFrameWire( wire );
	SnapSoundWire snd;
	SnapCosCursor cur = { pByteStream->pbStream, pByteStream->pbStreamEnd };
	ReadSnapSound( cur, ClientMapFrame( ), wire, snd );
	// The cursor read the body; the stream follows it, const dropped because it is
	// the stream's own buffer.
	pByteStream->pbStream = const_cast<BYTE *>( cur.p );
	if ( pByteStream->pbStream > pByteStream->pbStreamEnd )
	{
		CLIENT_PrintWarning( "OnSnapSound: Packet contained %td too few bytes\n",
			pByteStream->pbStream - pByteStream->pbStreamEnd );
		return;
	}

	// Stamped for another map: that map's table named the id and its mirrors are
	// gone, so there is nothing here this sound could mean.
	if ( MapStampCurrent( snd.stamp ) == false )
	{
		CountSoundOtherMap( );
		return;
	}

	const int channel = snd.bLoop ? ( snd.channel | CHAN_LOOP ) : snd.channel;
	const unsigned int id = static_cast<unsigned int>( snd.soundId );

	// An index means something only below the count this client matched.
	if (( SoundIdUsable( id ) == false ) || ( id >= S_sfx.Size( )))
	{
		CountSoundId( false );
		return;
	}
	CountSoundId( true );

	if ( snd.bPoint )
	{
		// The server sends a point while its belief is young; a mirror already
		// here takes the sound attached, as stock plays a launch sound.
		AActor *pMirror = ( snd.netID != 0 ) ? MirrorForNetID( snd.netID, false ) : NULL;
		if ( pMirror != NULL )
		{
			CountPointSoundOnMirror( );
			ServerCommands::SoundActor command;
			command.SetActor( pMirror );
			command.SetChannel( channel );
			command.SetSound( S_sfx[id].name );
			command.SetVolume( snd.volume );
			command.SetAttenuation( snd.atten );
			// The body carries no pitch, so the client computes its own, as it does
			// for every stock sound a server leaves at the default.
			command.SetPitch( -1 );
			command.Execute( );
			return;
		}

		ServerCommands::SoundPoint command;
		command.SetX( snd.x );
		command.SetY( snd.y );
		command.SetZ( snd.z );
		command.SetChannel( channel );
		command.SetSound( S_sfx[id].name );
		command.SetVolume( snd.volume );
		command.SetAttenuation( snd.atten );
		command.SetPitch( -1 );
		const unsigned int starts = S_SoundStarts( );
		command.Execute( );
		// Moved onto the mirror when it appears, if this one started.
		PointSoundPending( snd.netID, snd.x, snd.y, snd.z, channel,
			static_cast<int>( id ), S_SoundStarts( ) != starts );
		return;
	}

	// Resolved under SoundActor's name, which is what lets it reach a mirror.
	AActor *pActor = NULL;
	if ( CLIENT_ReadActorFromNetID( snd.netID, RUNTIME_CLASS( AActor ), true, pActor,
		"SoundActor", "actor" ) == false )
		return;

	ServerCommands::SoundActor command;
	command.SetActor( pActor );
	command.SetChannel( channel );
	command.SetSound( S_sfx[id].name );
	command.SetVolume( snd.volume );
	command.SetAttenuation( snd.atten );
	command.SetPitch( -1 );
	command.Execute( );
}
