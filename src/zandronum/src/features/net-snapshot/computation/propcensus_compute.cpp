// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: propcensus_compute.cpp
//
// Description: propcensus_compute.h's tally of the property slots by class.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/propcensus_compute.h"

void zx::netsnapshot::ComputePropClassCensus( const SnapPropSlotSeen * seen, unsigned int count,
	SnapPropClassCount * out, unsigned int outMax, unsigned int & outCount )
{
	outCount = 0;
	if ( ( seen == nullptr ) || ( out == nullptr ) || ( outMax == 0 ) )
		return;

	// A linear find per slot: the table is a few hundred slots, which costs less than anything
	// needing an allocation, and this runs once on a command.
	for ( unsigned int i = 0; i < count; ++i )
	{
		unsigned int at = 0;
		while ( ( at < outCount ) && ( out[at].classId != seen[i].classId ) )
			++at;
		if ( at == outCount )
		{
			// A class that does not fit is dropped, which the caller avoids by passing room
			// for one class a slot.
			if ( outCount == outMax )
				continue;
			out[at].classId = seen[i].classId;
			out[at].slots = 0;
			for ( unsigned int b = 0; b < SNAPPROPCENSUS_BITS; ++b )
				out[at].byBit[b] = 0;
			++outCount;
		}
		++out[at].slots;
		for ( unsigned int b = 0; b < SNAPPROPCENSUS_BITS; ++b )
		{
			if ( ( seen[i].flags & ( 1u << b ) ) != 0 )
				++out[at].byBit[b];
		}
	}

	// Most slots first, the lower class id breaking a tie, by insertion: the order is what the
	// reader needs and the list is at most one entry a slot.
	for ( unsigned int i = 1; i < outCount; ++i )
	{
		const SnapPropClassCount hold = out[i];
		unsigned int j = i;
		while ( ( j > 0 )
			&& ( ( out[j - 1].slots < hold.slots )
				|| ( ( out[j - 1].slots == hold.slots )
					&& ( out[j - 1].classId > hold.classId ) ) ) )
		{
			out[j] = out[j - 1];
			--j;
		}
		out[j] = hold;
	}
}
