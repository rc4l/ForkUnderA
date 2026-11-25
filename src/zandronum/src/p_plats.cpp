//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1994-1996 Raven Software
// Copyright 1998-1998 Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
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
//		Plats (i.e. elevator platforms) code, raising/lowering.
//
//-----------------------------------------------------------------------------

#include "i_system.h"
#include "m_random.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_lnspec.h"
#include "s_sndseq.h"
#include "doomstat.h"
#include "r_state.h"
#include "gi.h"
#include "farchive.h"
// [BB] New #includes.
#include "network.h"
#include "sv_commands.h"
#include "features/net-replication/mapstate.h"
#include "cl_demo.h"
#include "features/net-snapshot/movers.h"	// [Kaori-Chani] the capture and its holds
#include "features/net-snapshot/capture.h"	// [Kaori-Chani] MoverServerStops

static FRandom pr_doplat ("DoPlat");

IMPLEMENT_CLASS (DPlat)

inline FArchive &operator<< (FArchive &arc, DPlat::EPlatType &type)
{
	BYTE val = (BYTE)type;
	arc << val;
	type = (DPlat::EPlatType)val;
	return arc;
}
inline FArchive &operator<< (FArchive &arc, DPlat::EPlatState &state)
{
	BYTE val = (BYTE)state;
	arc << val;
	state = (DPlat::EPlatState)val;
	return arc;
}

DPlat::DPlat ()
{
}

void DPlat::Serialize (FArchive &arc)
{
	Super::Serialize (arc);
	arc << m_Speed
		<< m_Low
		<< m_High
		<< m_Wait
		<< m_Count
		<< m_Status
		<< m_OldStatus
		<< m_Crush
		<< m_Tag
		<< m_Type
		// [BC]
		<< m_PlatID;
}

// [Kaori-Chani] features/net-snapshot: PlayPlatSound's body from the sector, unchanged but for
// m_Sector becoming an argument. The sector's own sequence wins over the name, as it always did.
static void PlayPlatSound (sector_t *sec, const char *sound)
{
	if (sec->seqType >= 0)
	{
		SN_StartSequence (sec, CHAN_FLOOR, sec->seqType, SEQ_PLATFORM, 0);
	}
	else if (sec->SeqName != NAME_None)
	{
		SN_StartSequence (sec, CHAN_FLOOR, sec->SeqName, 0);
	}
	else
	{
		SN_StartSequence (sec, CHAN_FLOOR, sound, 0);
	}
}

void DPlat::PlayPlatSound (const char *sound)
{
	::PlayPlatSound (m_Sector, sound);
}

// [Kaori-Chani] features/net-snapshot: client_PlayPlatSound's switch, copied. Type 2 starts
// Silence itself, since the body above would let a sector's own sequence override it.
void P_PlatSoundSector (sector_t *sec, int type)
{
	switch (type)
	{
	case 0:

		SN_StopSequence (sec, CHAN_FLOOR);
		break;
	case 1:

		::PlayPlatSound (sec, "Platform");
		break;
	case 2:

		SN_StartSequence (sec, CHAN_FLOOR, "Silence", 0);
		break;
	case 3:

		::PlayPlatSound (sec, "Floor");
		break;
	}
}

//
// Move a plat up and down
//
void DPlat::Tick ()
{
	EResult res;
	fixed_t snapWas;	// [Kaori-Chani] the plane before the move, for the stall test

	// [Kaori-Chani] features/net-snapshot: the state this tic starts in.
	SnapCapture( );

	switch (m_Status)
	{
	case up:
		// [Kaori-Chani] features/net-snapshot: a plane the server says stalled stays put.
		if ( zx::netsnapshot::MoverHoldsPlane( m_SnapBlocked ))
			break;
		snapWas = m_Sector->floorplane.d;
		res = MoveFloor (m_Speed, m_High, m_Crush, 1, false);
		if ( res == crushed )
			zx::netsnapshot::MoverCountCrushed( );
		// [Kaori-Chani] features/net-snapshot: stalled is crushed with the plane unmoved.
		m_SnapBlocked = zx::netsnapshot::MoverPlaneStalled( m_SnapBlocked,
			( res == crushed ) && ( m_Crush != -1 )
			&& ( m_Sector->floorplane.d == snapWas ));
		
		// [BC] That's all we need to do in client mode.
		if ( NETWORK_InClientMode() )
			break;

		if (res == crushed && (m_Crush == -1))
		{
			m_Count = m_Wait;
			m_Status = down;
			PlayPlatSound ("Platform");

			// [BC] Tell clients that this plat is changing directions.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_ChangePlatStatus( m_PlatID, m_Status );
				zx::netsnapshot::MoverSendPlatSound( m_Sector, m_PlatID, 1 );
			}
		}
		else if (res == pastdest)
		{
			// [BC] If the sector has reached its destination, this is probably a good time to verify all the clients
			// have the correct floor/ceiling height for this sector.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				// [Kaori-Chani] features/net-snapshot: a record writes this plane.
				if ( zx::netsnapshot::MoverServerStops( SNAPMV_PLAT, true ) == false )
				{
					if ( m_Sector->floorOrCeiling == 0 )
						SERVERCOMMANDS_SetSectorFloorPlane( ULONG( m_Sector - sectors ));
					else
						SERVERCOMMANDS_SetSectorCeilingPlane( ULONG( m_Sector - sectors ));
				}
			}

			SN_StopSequence (m_Sector, CHAN_FLOOR);

			// [BC] If we're the server, tell clients to play the plat sound.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				zx::netsnapshot::MoverSendPlatSound( m_Sector, m_PlatID, 0 );

			if (m_Type != platToggle)
			{
				m_Count = m_Wait;
				m_Status = waiting;

				switch (m_Type)
				{
					case platRaiseAndStayLockout:
						// Instead of keeping the dead thinker like Heretic did let's 
						// better use a flag to avoid problems elsewhere. For example,
						// keeping the thinker would make tagwait wait indefinitely.
						m_Sector->planes[sector_t::floor].Flags |= PLANEF_BLOCKED; 
					case platRaiseAndStay:
					case platDownByValue:
					case platDownWaitUpStay:
					case platDownWaitUpStayStone:
					case platUpByValueStay:
					case platDownToNearestFloor:
					case platDownToLowestCeiling:

						// [BC] If we're the server, tell clients to destroy the plat.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
							SERVERCOMMANDS_DestroyPlat( m_PlatID );

						Destroy ();
						break;
					default:
						break;
				}
			}
			else
			{
				m_OldStatus = m_Status;		//jff 3/14/98 after action wait  
				m_Status = in_stasis;		//for reactivation of toggle

				// [BC] If we're the server, tell clients that that status is changing.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_ChangePlatStatus( m_PlatID, m_Status );
			}
		}
		break;
	case down:
		// [Kaori-Chani] features/net-snapshot: as going up.
		if ( zx::netsnapshot::MoverHoldsPlane( m_SnapBlocked ))
			break;
		res = MoveFloor (m_Speed, m_Low, -1, -1, false);
		if ( res == crushed )
			zx::netsnapshot::MoverCountCrushed( );
		// [Kaori-Chani] features/net-snapshot: the bounce below is a turn, not a stall.
		m_SnapBlocked = zx::netsnapshot::MoverPlaneStalled( m_SnapBlocked,
			( res == crushed ) && (( m_Crush >= 0 ) || ( m_Type == platToggle )));

		// [BC] That's all we need to do in client mode.
		if ( NETWORK_InClientMode() )
			break;

		if (res == pastdest)
		{
			// [BC] If the sector has reached its destination, this is probably a good time to verify all the clients
			// have the correct floor/ceiling height for this sector.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				// [Kaori-Chani] features/net-snapshot: a record writes this plane.
				if ( zx::netsnapshot::MoverServerStops( SNAPMV_PLAT, true ) == false )
				{
					if ( m_Sector->floorOrCeiling == 0 )
						SERVERCOMMANDS_SetSectorFloorPlane( ULONG( m_Sector - sectors ));
					else
						SERVERCOMMANDS_SetSectorCeilingPlane( ULONG( m_Sector - sectors ));
				}
			}

			SN_StopSequence (m_Sector, CHAN_FLOOR);

			// [BC] If we're the server, tell clients to play the plat sound.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				zx::netsnapshot::MoverSendPlatSound( m_Sector, m_PlatID, 0 );

			// if not an instant toggle, start waiting
			if (m_Type != platToggle)		//jff 3/14/98 toggle up down
			{								// is silent, instant, no waiting
				m_Count = m_Wait;
				m_Status = waiting;

				switch (m_Type)
				{
					case platUpWaitDownStay:
					case platUpNearestWaitDownStay:
					case platUpByValue:

						// [BC] If we're the server, tell clients to destroy the plat.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
							SERVERCOMMANDS_DestroyPlat( m_PlatID );

						Destroy ();
						break;
					default:

						// [BC] In this case, the plat didn't get destroyed, so tell clients about
						// the state change.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
							SERVERCOMMANDS_ChangePlatStatus( m_PlatID, m_Status );

						break;
				}
			}
			else
			{	// instant toggles go into stasis awaiting next activation
				m_OldStatus = m_Status;		//jff 3/14/98 after action wait  
				m_Status = in_stasis;		//for reactivation of toggle

				// [BC] If we're the server, tell clients that that status is changing.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_ChangePlatStatus( m_PlatID, m_Status );
			}
		}
		else if (res == crushed && m_Crush < 0 && m_Type != platToggle)
		{
			m_Status = up;
			m_Count = m_Wait;
			PlayPlatSound ("Platform");
		}

		//jff 1/26/98 remove the plat if it bounced so it can be tried again
		//only affects plats that raise and bounce

		// remove the plat if it's a pure raise type
		switch (m_Type)
		{
			case platUpByValueStay:
			case platRaiseAndStay:
			case platRaiseAndStayLockout:

				// [BC] If we're the server, tell clients to destroy the plat.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_DestroyPlat( m_PlatID );

				Destroy ();
			default:
				break;
		}

		break;
		
	case waiting:

		// [Kaori-Chani] features/net-snapshot: a plat a record has reached counts its own
		// wait down; no record has reached one with the gate off, so a client breaks here.
		if ( zx::netsnapshot::MoverClientSkipsWait( m_SnapSeen ))
			break;

		// [Kaori-Chani] features/net-snapshot: the tic spent waiting, whoever ends it. Below
		// the break, as the door's counter is, so it counts only a wait this side runs.
		if ( m_Count > 0 )
			zx::netsnapshot::MoverCountPlatWaitTick( );

		if (m_Count > 0 && !--m_Count)
		{
			if (m_Sector->floorplane.d == m_Low)
				m_Status = up;
			else
				m_Status = down;

			// [Kaori-Chani] features/net-snapshot: a wait this side counted out itself.
			zx::netsnapshot::MoverCountPlatWaitExpired( );

			// [BC] If we're the server, tell clients that that status is changing.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_ChangePlatStatus( m_PlatID, m_Status );

			if (m_Type == platToggle)
			{
				// [Kaori-Chani] features/net-snapshot: the server sends this sound, so a
				// client that counted the wait out itself does not also play it.
				if ( zx::netsnapshot::MoverPlaysWaitSound( ))
					SN_StartSequence (m_Sector, CHAN_FLOOR, "Silence", 0);

				// [BC] If we're the server, tell clients to play the plat sound.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					zx::netsnapshot::MoverSendPlatSound( m_Sector, m_PlatID, 2 );
			}
			else
			{
				// [Kaori-Chani] features/net-snapshot: as above.
				if ( zx::netsnapshot::MoverPlaysWaitSound( ))
					PlayPlatSound ("Platform");

				// [BC] If we're the server, tell clients to play the plat sound.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					zx::netsnapshot::MoverSendPlatSound( m_Sector, m_PlatID, 1 );
			}
		}
		break;

	case in_stasis:
		break;
	}

	// [Kaori-Chani] features/net-snapshot: the state the tic ended in, unless a Destroy
	// above captured the GONE already.
	if (( ObjectFlags & OF_EuthanizeMe ) == 0 )
		SnapCapture( );
}

// [Kaori-Chani] features/net-snapshot: the GONE, and the only hook that catches a plat
// destroyed outside Tick, such as the map reset's loop.
void DPlat::Destroy()
{
	SnapCapture( true );
	Super::Destroy();
}

// [BC]
void DPlat::UpdateToClient( ULONG ulClient )
{
	SERVERCOMMANDS_DoPlat( m_Type, m_Sector, m_Status,(LONG)( m_High),(LONG)( m_Low),(LONG)( m_Speed), m_PlatID, ulClient, SVCF_ONLYTHISCLIENT );
}

DPlat::DPlat (sector_t *sector)
	: DMovingFloor (sector)
{
	// [Kaori-Chani] features/net-snapshot: neither is set by EV_DoPlat or client_DoPlat, and
	// both are read before stock code writes them (the wait, the census and the capture).
	m_Count = 0;
	m_OldStatus = up;

	// [EP]
	m_PlatID = -1;
}

// [BC]
LONG DPlat::GetID( void )
{
	return ( m_PlatID );
}

// [BC]
void DPlat::SetID( LONG lID )
{
	m_PlatID = lID;
}

// [BC]
fixed_t DPlat::GetLow( void )
{
	return ( m_Low );
}

// [BC]
void DPlat::SetLow( fixed_t Low )
{
	m_Low = Low;
}

// [BC]
fixed_t DPlat::GetHigh( void )
{
	return ( m_High );
}

// [BC]
void DPlat::SetHigh( fixed_t High )
{
	m_High = High;
}

// [BC]
DPlat::EPlatState DPlat::GetStatus( void )
{
	return ( m_Status );
}

// [BC]
void DPlat::SetStatus( LONG lStatus )
{
	m_Status = (DPlat::EPlatState)lStatus;
}

// [BC]
void DPlat::SetType( EPlatType Type )
{
	m_Type = Type;
}

// [BC]
void DPlat::SetCrush( LONG lCrush )
{
	m_Crush = lCrush;
}

// [BC]
void DPlat::SetTag( LONG lTag )
{
	m_Tag = lTag;
}

// [BC]
void DPlat::SetSpeed( LONG lSpeed )
{
	m_Speed = lSpeed;
}

// [BC]
void DPlat::SetDelay( LONG lDelay )
{
	m_Wait = lDelay;
}


//
// Do Platforms
//	[RH] Changed amount to height and added delay,
//		 lip, change, tag, and speed parameters.
//
bool EV_DoPlat (int tag, line_t *line, DPlat::EPlatType type, int height,
				int speed, int delay, int lip, int change)
{
	DPlat *plat;
	int secnum;
	sector_t *sec;
	bool rtn = false;
	bool manual = false;
	fixed_t newheight = 0;
	vertex_t *spot;

	if (tag != 0)
	{
		//	Activate all <type> plats that are in_stasis
		switch (type)
		{
		case DPlat::platToggle:
			rtn = true;
		case DPlat::platPerpetualRaise:
			P_ActivateInStasis (tag);
			break;

		default:
			break;
		}
	}


	// [RH] If tag is zero, use the sector on the back side
	//		of the activating line (if any).
	FSectorTagIterator itr(tag, line);
	while ((secnum = itr.Next()) >= 0)
	{
		sec = &sectors[secnum];

		if (sec->PlaneMoving(sector_t::floor))
		{
			continue;
		}

		// Find lowest & highest floors around sector
		rtn = true;
		plat = new DPlat (sec);

		plat->m_Type = type;
		plat->m_Crush = -1;
		plat->m_Tag = tag;
		plat->m_Speed = speed;
		plat->m_Wait = delay;

		// [BC] Potentially create the platform's network ID.
		if ( NETWORK_GetState() == NETSTATE_SERVER )
			plat->m_PlatID = P_GetFirstFreePlatID( );

		//jff 1/26/98 Avoid raise plat bouncing a head off a ceiling and then
		//going down forever -- default lower to plat height when triggered
		plat->m_Low = sec->floorplane.d;

		if (change)
		{
			if (line)
			{
				sec->SetTexture(sector_t::floor, line->sidedef[0]->sector->GetTexture(sector_t::floor));

				// [BC] Update clients about this flat change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					zx::netreplication::TouchSector( ULONG( sec - sectors ));

				// [BC] Also, mark this sector as having its flat changed.
				sec->bFlatChange = true;
			}
			if (change == 1)
				sec->special &= SECRET_MASK;	// Stop damage and other stuff, if any
		}

		switch (type)
		{
		case DPlat::platRaiseAndStay:
		case DPlat::platRaiseAndStayLockout:
			newheight = sec->FindNextHighestFloor (&spot);
			plat->m_High = sec->floorplane.PointToDist (spot, newheight);
			plat->m_Low = sec->floorplane.d;
			plat->m_Status = DPlat::up;
			plat->PlayPlatSound ("Floor");
			sec->special &= SECRET_MASK;		// NO MORE DAMAGE, IF APPLICABLE
			break;

		case DPlat::platUpByValue:
		case DPlat::platUpByValueStay:
			newheight = sec->floorplane.ZatPoint(fixed_t(0), fixed_t(0)) + height;
			plat->m_High = sec->floorplane.PointToDist (0, 0, newheight);
			plat->m_Low = sec->floorplane.d;
			plat->m_Status = DPlat::up;
			plat->PlayPlatSound ("Floor");
			break;
		
		case DPlat::platDownByValue:
			newheight = sec->floorplane.ZatPoint(fixed_t(0), fixed_t(0)) - height;
			plat->m_Low = sec->floorplane.PointToDist (0, 0, newheight);
			plat->m_High = sec->floorplane.d;
			plat->m_Status = DPlat::down;

			// [BC] I think this should be the platform sound, but I could be wrong.
			plat->PlayPlatSound ("Platform");
			break;

		case DPlat::platDownWaitUpStay:
		case DPlat::platDownWaitUpStayStone:
			newheight = sec->FindLowestFloorSurrounding (&spot) + lip*FRACUNIT;
			plat->m_Low = sec->floorplane.PointToDist (spot, newheight);

			if (plat->m_Low < sec->floorplane.d)
				plat->m_Low = sec->floorplane.d;

			plat->m_High = sec->floorplane.d;
			plat->m_Status = DPlat::down;
			plat->PlayPlatSound (type == DPlat::platDownWaitUpStay ? "Platform" : "Floor");
			break;
		
		case DPlat::platUpNearestWaitDownStay:
			newheight = sec->FindNextHighestFloor (&spot);
			// Intentional fall-through

		case DPlat::platUpWaitDownStay:
			if (type == DPlat::platUpWaitDownStay)
			{
				newheight = sec->FindHighestFloorSurrounding (&spot);
			}
			plat->m_High = sec->floorplane.PointToDist (spot, newheight);
			plat->m_Low = sec->floorplane.d;

			if (plat->m_High > sec->floorplane.d)
				plat->m_High = sec->floorplane.d;

			plat->m_Status = DPlat::up;
			plat->PlayPlatSound ("Platform");
			break;

		case DPlat::platPerpetualRaise:
			newheight = sec->FindLowestFloorSurrounding (&spot) + lip*FRACUNIT;
			plat->m_Low =  sec->floorplane.PointToDist (spot, newheight);

			if (plat->m_Low < sec->floorplane.d)
				plat->m_Low = sec->floorplane.d;

			newheight = sec->FindHighestFloorSurrounding (&spot);
			plat->m_High =  sec->floorplane.PointToDist (spot, newheight);

			if (plat->m_High > sec->floorplane.d)
				plat->m_High = sec->floorplane.d;

			plat->m_Status = pr_doplat() & 1 ? DPlat::up : DPlat::down;
			plat->PlayPlatSound ("Platform");
			break;

		case DPlat::platToggle:	//jff 3/14/98 add new type to support instant toggle
			plat->m_Crush = 10;	//jff 3/14/98 crush anything in the way

			// set up toggling between ceiling, floor inclusive
			newheight = sec->FindLowestCeilingPoint (&spot);
			plat->m_Low = sec->floorplane.PointToDist (spot, newheight);
			plat->m_High = sec->floorplane.d;
			plat->m_Status = DPlat::down;
			SN_StartSequence (sec, CHAN_FLOOR, "Silence", 0);
			break;

		case DPlat::platDownToNearestFloor:
			newheight = sec->FindNextLowestFloor (&spot) + lip*FRACUNIT;
			plat->m_Low = sec->floorplane.PointToDist (spot, newheight);
			plat->m_Status = DPlat::down;
			plat->m_High = sec->floorplane.d;
			plat->PlayPlatSound ("Platform");
			break;

		case DPlat::platDownToLowestCeiling:
			newheight = sec->FindLowestCeilingSurrounding (&spot);
		    plat->m_Low = sec->floorplane.PointToDist (spot, newheight);
			plat->m_High = sec->floorplane.d;

			if (plat->m_Low < sec->floorplane.d)
				plat->m_Low = sec->floorplane.d;

			plat->m_Status = DPlat::down;
			plat->PlayPlatSound ("Platform");
			break;

		default:
			break;
		}

		// [BC] If we're the server, tell clients to create the plat.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			SERVERCOMMANDS_DoPlat( type, &sectors[secnum], plat->m_Status,(LONG)( plat->m_High),(LONG)( plat->m_Low),(LONG)( plat->m_Speed), plat->m_PlatID );

			// [BC] Also, if we're the server, tell clients to play the appropriate plat sound.
			switch ( type )
			{
			case DPlat::platRaiseAndStay:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 3 );
				break;
			case DPlat::platUpByValue:
			case DPlat::platUpByValueStay:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 3 );
				break;
			case DPlat::platDownByValue:

				// [BC] I think this should be the platform sound, but I could be wrong.
				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			case DPlat::platDownWaitUpStay:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			case DPlat::platDownWaitUpStayStone:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 3 );
				break;
			case DPlat::platUpNearestWaitDownStay:
			case DPlat::platUpWaitDownStay:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			case DPlat::platPerpetualRaise:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			case DPlat::platToggle:	//jff 3/14/98 add new type to support instant toggle

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 2 );
				break;
			case DPlat::platDownToNearestFloor:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			case DPlat::platDownToLowestCeiling:

				zx::netsnapshot::MoverSendPlatSound( plat->m_Sector, plat->m_PlatID, 1 );
				break;
			default:

				break;
			}
		}
	}
	return rtn;
}

void DPlat::Reactivate ()
{
	if (m_Type == platToggle)	//jff 3/14/98 reactivate toggle type
		m_Status = m_OldStatus == up ? down : up;
	else
		m_Status = m_OldStatus;
}

void P_ActivateInStasis (int tag)
{
	DPlat *scan;
	TThinkerIterator<DPlat> iterator;

	while ( (scan = iterator.Next ()) )
	{
		if (scan->m_Tag == tag && scan->m_Status == DPlat::in_stasis)
		{
			scan->Reactivate ();

			// [BC] If we're the server, tell clients that that status is changing.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_ChangePlatStatus( scan->m_PlatID, scan->m_Status );

				// [BC] If the sector is starting to moved, then this is probably a good time
				// to verify all the clients have the correct floor/ceiling height for
				// this sector.
				// [Kaori-Chani] features/net-snapshot: a record writes this plane.
				if ( zx::netsnapshot::MoverServerStops( SNAPMV_PLAT, true ) == false )
				{
					if ( scan->m_Sector->floorOrCeiling == 0 )
						SERVERCOMMANDS_SetSectorFloorPlane( ULONG( scan->m_Sector - sectors ));
					else
						SERVERCOMMANDS_SetSectorCeilingPlane( ULONG( scan->m_Sector - sectors ));
				}
			}
		}
	}
}

void DPlat::Stop ()
{
	m_OldStatus = m_Status;
	m_Status = in_stasis;
}

void EV_StopPlat (int tag)
{
	DPlat *scan;
	TThinkerIterator<DPlat> iterator;

	while ( (scan = iterator.Next ()) )
	{
		if (scan->m_Status != DPlat::in_stasis && scan->m_Tag == tag)
		{
			scan->Stop ();

			// [BC] If we're the server, tell clients that that status is changing.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_ChangePlatStatus( scan->m_PlatID, scan->m_Status );

				// [BC] If the sector has stopped, then this is probably a good time
				// to verify all the clients have the correct floor/ceiling height for
				// this sector.
				// [Kaori-Chani] features/net-snapshot: a record writes this plane.
				if ( zx::netsnapshot::MoverServerStops( SNAPMV_PLAT, true ) == false )
				{
					if ( scan->m_Sector->floorOrCeiling == 0 )
						SERVERCOMMANDS_SetSectorFloorPlane( ULONG( scan->m_Sector - sectors ));
					else
						SERVERCOMMANDS_SetSectorCeilingPlane( ULONG( scan->m_Sector - sectors ));
				}
			}
		}
	}
}

//*****************************************************************************
//
DPlat *P_GetPlatByID( LONG lID )
{
	DPlat	*pPlat;

	TThinkerIterator<DPlat>		Iterator;

	while (( pPlat = Iterator.Next( )))
	{
		if ( pPlat->GetID( ) == lID )
			return ( pPlat );
	}

	return ( NULL );
}

//*****************************************************************************
//
LONG P_GetFirstFreePlatID( void )
{
	return NETWORK_GetFirstFreeID<DPlat>();
}
