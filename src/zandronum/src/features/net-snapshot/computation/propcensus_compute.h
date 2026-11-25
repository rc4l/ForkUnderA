// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: propcensus_compute.h
//
// Description: Which classes hold the entity property slots: one entry a live
// slot in, the classes most slots first out, each with the count of slots whose
// property bits are set. Pure: capture.cpp reads the table, names the classes
// and prints what this returns.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_PROPCENSUS_COMPUTE_H__
#define __SNAP_PROPCENSUS_COMPUTE_H__

// The property bits counted apart, which are the caller's own low four: it decides what each
// one means, and a bit above these is counted in the slot total and nowhere else.
#define SNAPPROPCENSUS_BITS		4

// One live slot, as the table holds it.
struct SnapPropSlotSeen
{
	unsigned short	classId;	// 0 for a slot whose entity could not be named
	unsigned char	flags;		// the property bits this slot carries
};

// One class and what its slots carry.
struct SnapPropClassCount
{
	unsigned short	classId;
	unsigned int	slots;
	unsigned int	byBit[SNAPPROPCENSUS_BITS];
};

namespace zx { namespace netsnapshot {

// The classes holding slots, most first with the lower class id breaking a tie: at most outMax of
// them, outCount what it wrote, so room for one class a slot keeps every class.
void ComputePropClassCensus( const SnapPropSlotSeen * seen, unsigned int count,
	SnapPropClassCount * out, unsigned int outMax, unsigned int & outCount );

}} // namespace zx::netsnapshot

#endif // __SNAP_PROPCENSUS_COMPUTE_H__
