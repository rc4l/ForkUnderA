// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: anim_compute.h
//
// Description: The client's animation decisions over states as plain keys:
// the ring of states a mirror entered, which tells a record's resync from a
// jump, and the loop census's verdict on a shadow against the wire's state.
// The ring and the verdict are templates on the state key, defined here; the
// engine's key is a state pointer, the tests' an int. ghostactor.cpp
// walks the real states and counts by what these return.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_ANIM_COMPUTE_H__
#define __SNAP_ANIM_COMPUTE_H__

//*****************************************************************************
//	MIRROR SPAWN RING

// The states a mirror entered, telling a resync from a jump, by any key
// compared by == (the engine's const FState*).
static const unsigned int SNAP_MS_RING = 16;
static const int SNAP_MS_SLACK = 4;			// tics a mirror may predict early

template <typename State> struct SnapMsEntry
{
	State			state;
	int				tic;
};
template <typename State> struct SnapMsRing
{
	typedef State		Key;
	SnapMsEntry<State>	e[SNAP_MS_RING];	// one per state change, newest at head
	unsigned int		head, count;
};

namespace zx { namespace netsnapshot {

// A state entered again keeps one entry, with the latest tic.
template <typename State>
inline void ComputeMsPush(SnapMsRing<State> & ring,
	typename SnapMsRing<State>::Key state, int tic)
{
	if ((ring.count > 0) && (ring.e[ring.head].state == state))
	{
		ring.e[ring.head].tic = tic;
		return;
	}
	ring.head = (ring.count == 0) ? 0 : (ring.head + 1) % SNAP_MS_RING;
	ring.e[ring.head].state = state;
	ring.e[ring.head].tic = tic;
	if (ring.count < SNAP_MS_RING)
		++ring.count;
}

// A resync: the ring entered wire no earlier than its longest duration and
// the slack before recTic, or at any tic where longest is -1 (it never ends).
template <typename State>
inline bool ComputeMsResync(const SnapMsRing<State> & ring,
	typename SnapMsRing<State>::Key wire, int longest, int recTic)
{
	for (unsigned int k = 0; k < ring.count; ++k)
	{
		const SnapMsEntry<State>& e = ring.e[(ring.head + SNAP_MS_RING - k) % SNAP_MS_RING];
		if ((e.state == wire) && ((longest < 0) || (e.tic >= recTic - longest - SNAP_MS_SLACK)))
			return true;
	}
	return false;
}

// The oldest entry, of a ring that must not be empty.
template <typename State>
inline const SnapMsEntry<State> & ComputeMsOldest(const SnapMsRing<State> & ring)
{
	return ring.e[(ring.head + SNAP_MS_RING - (ring.count - 1)) % SNAP_MS_RING];
}

}} // namespace zx::netsnapshot

//*****************************************************************************
//	LOOP CENSUS

// The census's gap buckets: <100 ms, <250, <500, <1 s, 1 s and over.
#define SNAP_SHADOW_BUCKETS		5

namespace zx { namespace netsnapshot {

// The bucket of an entity's own update gap, in server tics at 28.57 ms each.
unsigned int ComputeShadowBucket(int gapTics);

}} // namespace zx::netsnapshot

// How far each walk follows a chain: Doom chains loop, so an unbounded walk
// always finds the state and reports a fiction.
#define SNAP_SHADOW_WALK		8

// The shadow's verdict against the wire's state, in the census's column order.
enum
{
	SNAP_SHADOW_EXACT,		// on the wire's state
	SNAP_SHADOW_BEHIND,		// the wire's state is ahead on the shadow's chain
	SNAP_SHADOW_AHEAD,		// the shadow's state is ahead on the wire's chain
	SNAP_SHADOW_OFF,		// neither reaches the other in SNAP_SHADOW_WALK steps
	SNAP_SHADOW_OUTCOMES
};

namespace zx { namespace netsnapshot {

// The verdict on a shadow at `shadow` against the wire's state, walking
// each chain with `next` (State() at a chain's end).
template <typename State, typename Next>
inline int ComputeShadowVerdict(State shadow, State wire, Next next)
{
	if (shadow == wire)
		return SNAP_SHADOW_EXACT;
	State p = shadow;
	for (unsigned int k = 0; (k < SNAP_SHADOW_WALK) && (p != State()); ++k)
	{
		p = next(p);
		if (p == wire)
			return SNAP_SHADOW_BEHIND;
	}
	p = wire;
	for (unsigned int k = 0; (k < SNAP_SHADOW_WALK) && (p != State()); ++k)
	{
		p = next(p);
		if (p == shadow)
			return SNAP_SHADOW_AHEAD;
	}
	return SNAP_SHADOW_OFF;
}

}} // namespace zx::netsnapshot

#endif // __SNAP_ANIM_COMPUTE_H__
