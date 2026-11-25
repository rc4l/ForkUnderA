// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: proptell_compute.h
//
// Description: Whether an entity's properties need a slot at all: a set is
// told once to each client that holds the entity, and a slot is taken only
// once the value stops matching what was told. Pure: capture.cpp reads the
// actor, keeps the mark per id and owns the table.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_PROPTELL_COMPUTE_H__
#define __SNAP_PROPTELL_COMPUTE_H__

// No set has been told for this id. ComputePropMark never returns it, so a real set can never
// read as one and an id's mark can be cleared by writing it.
#define SNAPPROP_MARK_NONE	0u

// What a capture does with an entity that holds no slot.
enum SnapPropAction
{
	SNAPPROPACT_NOTHING = 0,	// its class is all a mirror needs, or it already matches its mark
	SNAPPROPACT_TELL,			// off its defaults with nothing told yet: tell it once a client
	SNAPPROPACT_SLOT			// changed since it was told, including back to the defaults
};

namespace zx { namespace netsnapshot {

// The mark for a body of property bytes as they travel: FNV-1a over them, never SNAPPROP_MARK_NONE.
unsigned int ComputePropMark( const unsigned char * body, unsigned int len );

// `mark` is what was told for this id and `bodyMark` the mark of what the actor would send now;
// a body naming nothing has a mark too, so a return to the class defaults reads as a change.
SnapPropAction ComputePropTellOrSlot( unsigned int mark, bool offDefaults, bool bodyCarries,
	unsigned int bodyMark );

}} // namespace zx::netsnapshot

#endif // __SNAP_PROPTELL_COMPUTE_H__
