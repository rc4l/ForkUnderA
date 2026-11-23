//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: join_compute.cpp
//
// Description: Which map a stream belongs to. See join_compute.h.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/join_compute.h"

using namespace zx::netsnapshot;

int zx::netsnapshot::ComputeMapStampAhead(BYTE stamp, BYTE joinedStamp)
{
	return (SBYTE)(BYTE)(stamp - joinedStamp);
}

void zx::netsnapshot::ComputeExtentTake(long long lo[3], long long hi[3], unsigned int k, fixed_t v)
{
	// [Kaori-Chani] The ceiling comes off the floor and the fraction, since adding FRACUNIT to a
	// 48.16 value near its limit overflows where a 32-bit one could not.
	const long long raw = (long long)v;
	const long long fl = raw >> FRACBITS;
	const long long ce = fl + (((raw & ((1LL << FRACBITS) - 1)) != 0) ? 1 : 0);
	lo[k] = (fl < lo[k]) ? fl : lo[k];
	hi[k] = (ce > hi[k]) ? ce : hi[k];
}
