// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Ported from qzandronum@397272811e4f71b168f1949d21369d3e91a7146c. See the header.

#include "features/quake-movement/elevatorjump.h"
#include "features/quake-movement/quakemove.h"

#include "actor.h"
#include "d_player.h"
#include "p_local.h"
#include "p_spec.h"
#include "r_defs.h"

namespace zx {
namespace quakemove {

fixed_t FloorMovingSpeed( sector_t *sector )
{
	if (( sector == NULL ) || ( sector->floordata == NULL ))
		return 0;

	DSectorEffect *const data = sector->floordata;

	// Each mover reports its rate differently; only the ones that can carry a standing player
	// upward are consulted. A mover at rest (waiting/in_stasis, direction 0) contributes nothing.
	if ( data->IsKindOf( RUNTIME_CLASS( DFloor )))
	{
		DFloor *const floor = barrier_cast<DFloor *>( sector->floordata );
		switch ( floor->GetDirection() )
		{
		case 1:		return floor->GetSpeed();
		case -1:	return -floor->GetSpeed();
		}
	}
	else if ( data->IsKindOf( RUNTIME_CLASS( DPlat )))
	{
		DPlat *const plat = barrier_cast<DPlat *>( sector->floordata );
		switch ( plat->GetStatus() )
		{
		case DPlat::up:		return plat->GetSpeed();
		case DPlat::down:	return -plat->GetSpeed();
		default:			break;
		}
	}
	else if ( data->IsKindOf( RUNTIME_CLASS( DElevator )))
	{
		DElevator *const elevator = barrier_cast<DElevator *>( sector->floordata );
		switch ( elevator->GetDirection() )
		{
		case 1:		return elevator->GetSpeed();
		case -1:	return -elevator->GetSpeed();
		}
	}
	else if ( data->IsKindOf( RUNTIME_CLASS( DPillar )))
	{
		// Only a building pillar drives its floor upward; an opening one moves it away.
		DPillar *const pillar = barrier_cast<DPillar *>( sector->floordata );
		if ( pillar->GetType() == DPillar::pillarBuild )
			return pillar->GetFloorSpeed();
	}

	return 0;
}

fixed_t CeilingMovingSpeed( sector_t *sector )
{
	if (( sector == NULL ) || ( sector->ceilingdata == NULL ))
		return 0;

	DSectorEffect *const data = sector->ceilingdata;

	// A 3D floor's "floor" is the underside of a ceiling mover, which is why this exists at all.
	if ( data->IsKindOf( RUNTIME_CLASS( DCeiling )))
	{
		DCeiling *const ceiling = barrier_cast<DCeiling *>( sector->ceilingdata );
		switch ( ceiling->GetDirection() )
		{
		case 1:		return ceiling->GetSpeed();
		case -1:	return -ceiling->GetSpeed();
		}
	}
	else if ( data->IsKindOf( RUNTIME_CLASS( DElevator )))
	{
		DElevator *const elevator = barrier_cast<DElevator *>( sector->ceilingdata );
		switch ( elevator->GetDirection() )
		{
		case 1:		return elevator->GetSpeed();
		case -1:	return -elevator->GetSpeed();
		}
	}

	return 0;
}

void ApplyElevatorJump( AActor *mo )
{
	if (( mo->mvFlags & MV_ELEVATORJUMP ) == 0 )
		return;

	sector_t *sector = mo->Sector;
	bool standingOn3DFloor = false;

#ifdef _3DFLOORS
	// A player standing on a 3D floor is carried by that floor's model sector, not by the sector
	// they are geometrically inside -- so resolve which surface is actually underfoot first.
	if ( mo->Sector->e->XFloor.ffloors.Size() )
	{
		for ( unsigned i = 0; i < mo->Sector->e->XFloor.ffloors.Size(); ++i )
		{
			F3DFloor *const rover = mo->Sector->e->XFloor.ffloors[i];
			if ((( rover->flags & FF_EXISTS ) == 0 ) || (( rover->flags & FF_SOLID ) == 0 ))
				continue;

			if ( mo->z >= rover->top.plane->ZatPoint( mo->x, mo->y ))
			{
				sector = rover->top.model;
				standingOn3DFloor = true;
			}
		}
	}
#endif

	const fixed_t elevatorSpeed = standingOn3DFloor
		? CeilingMovingSpeed( sector )
		: FloorMovingSpeed( sector );

	// Only a RISING surface contributes. A descending lift must not subtract from the jump, or
	// jumping off a downward lift would be worse than jumping off solid ground.
	if ( elevatorSpeed > 0 )
		mo->velz += elevatorSpeed;
}

} // namespace quakemove
} // namespace zx
