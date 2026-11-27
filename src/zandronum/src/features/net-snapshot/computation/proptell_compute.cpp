// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: proptell_compute.cpp
//
// Description: proptell_compute.h's mark and its tell-or-slot decision.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/proptell_compute.h"

unsigned int zx::netsnapshot::ComputePropMark( const unsigned char * body, unsigned int len )
{
	// FNV-1a, 32-bit: the bytes that travel are the input, so no field the wire carries is left
	// out of the comparison and none of the struct's padding is in it.
	unsigned int h = 2166136261u;
	for ( unsigned int i = 0; i < len; ++i )
	{
		h ^= (unsigned int)( ( body != nullptr ) ? body[i] : 0u );
		h *= 16777619u;
	}
	// 0 is the "nothing told" reading, so a body that hashes there takes 1.
	return ( h != SNAPPROP_MARK_NONE ) ? h : 1u;
}

SnapPropAction zx::netsnapshot::ComputePropTellOrSlot( unsigned int mark, bool offDefaults,
	bool bodyCarries, unsigned int bodyMark )
{
	if ( mark == SNAPPROP_MARK_NONE )
	{
		// Nothing has been told for this id, so one telling a client is all it needs; and only a
		// set worth sending is marked at all.
		return ( offDefaults && bodyCarries ) ? SNAPPROPACT_TELL : SNAPPROPACT_NOTHING;
	}
	// A set was told: a slot once what it would send stops matching that, the clients already
	// told having no second telling coming.
	return ( bodyMark == mark ) ? SNAPPROPACT_NOTHING : SNAPPROPACT_SLOT;
}
