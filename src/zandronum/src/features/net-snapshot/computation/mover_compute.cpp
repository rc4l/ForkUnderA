//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: mover_compute.cpp
//
// Description: The movers' decisions. See mover_compute.h.
//
//-----------------------------------------------------------------------------

#include <vector>

#include "features/net-snapshot/computation/mover_compute.h"

using namespace zx::netsnapshot;

int zx::netsnapshot::ComputeLinkWriteOrder(int numSectors, const SnapLinkEdge* edges, unsigned int nEdges,
	int floorBits, int ceilingBits, int* order, BYTE* drivenPlanes)
{
	std::vector<int> controls(numSectors), round(numSectors);
	for (int i = 0; i < numSectors; i++)
	{
		controls[i] = 0;
		round[i] = -1;
		drivenPlanes[i] = 0;
	}
	for (unsigned k = 0; k < nEdges; k++)
	{
		++controls[edges[k].to];
		if (edges[k].type & floorBits)
			drivenPlanes[edges[k].to] |= 1;
		if (edges[k].type & ceilingBits)
			drivenPlanes[edges[k].to] |= 2;
	}

	int n = 0;
	for (int r = 0; ; r++)
	{
		const int start = n;
		for (int i = 0; i < numSectors; i++)
		{
			if ((round[i] < 0) && (controls[i] == 0))
			{
				round[i] = r;
				order[n++] = i;
			}
		}
		if (n == start)
			break;
		for (unsigned k = 0; k < nEdges; k++)
		{
			if (round[edges[k].from] == r)
				--controls[edges[k].to];
		}
	}

	const int cyclic = numSectors - n;
	for (int i = 0; i < numSectors; i++)
	{
		if (round[i] < 0)
			order[n++] = i;
	}
	return cyclic;
}

const char* zx::netsnapshot::ComputeMoverClassName(BYTE cls)
{
	switch ( cls )
	{
	case SNAPMV_PLAT:		return "plat";
	case SNAPMV_FLOOR:		return "floor";
	case SNAPMV_ELEVATOR:	return "elevator";
	case SNAPMV_PILLAR:		return "pillar";
	case SNAPMV_CEILING:	return "ceiling";
	case SNAPMV_DOOR:		return "door";
	case SNAPMV_ANIMDOOR:	return "animdoor";
	case SNAPMV_POLY:		return "poly";
	default:				return "?";
	}
}

BYTE zx::netsnapshot::ComputeMoverNoFlags(BYTE planes)
{
	return (BYTE)((( planes & SNAPMV_PLANE_FLOOR ) ? SNAPMVF_NOFLOOR : 0 )
		| (( planes & SNAPMV_PLANE_CEILING ) ? SNAPMVF_NOCEILING : 0 ));
}

bool zx::netsnapshot::ComputePolyTurns(BYTE type)
{
	return ( type == SNAPPO_ROTATE ) || ( type == SNAPPO_SWING );
}

void zx::netsnapshot::ComputePolyCompareKey(SnapMoverPoly& s)
{
	s.x = s.y = 0;
	s.angle = 0;
	for (unsigned int i = 0; i < SNAPPO_MAXENTRIES; ++i)
	{
		SnapPolyEntry & e = s.e[i];
		e.speed = e.dist = e.tics = 0;
		e.xSpeed = e.ySpeed = 0;
	}
}
