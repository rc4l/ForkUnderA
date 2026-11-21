//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: snap_sound_compute.h
//
// Description: SVC2_SNAPSOUND's body, written and read in one place: the
// flags its fields imply, a point's three positions in the map's frame, and
// the defaults a body leaves out. Pure, so its test pins the bytes both ends
// use and the two cannot drift apart.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_SOUND_COMPUTE_H__
#define __SNAP_SOUND_COMPUTE_H__

#include "features/net-snapshot/format.h"	// SNAP_SOUND_*, SnapFrame, SnapCosCursor

// The volume and attenuation a body leaves out. The byte is the sound-amplify
// unit's encoding, and 1 is network.h's ATTN_INT_NORM, which a sender asserts.
#define SNAP_SOUND_VOLUME_NORM	127
#define SNAP_SOUND_ATTEN_NORM	1

// The map stamp, the flags, three positions at their widest, the NetID, the
// channel, the sound id, and the two bytes a body may add.
#define SNAP_SOUND_MAX_BYTES	( 1 + 1 + 3 * SNAP_FRAME_WIDEST + 2 + 1 + 2 + 1 + 1 )

// One SVC2_SNAPSOUND body. A point carries the NetID too: the client moves the
// sound onto the mirror once one exists, as stock's travels with the entity.
struct SnapSoundWire
{
	BYTE			stamp;		// the map it was sent for, ahead of everything else
	bool			bPoint;		// x, y and z travel, ahead of the NetID
	bool			bLoop;		// CHAN_LOOP at the call site, never sent on a point
	fixed_t			x, y, z;	// floored into the frame; 0 unless bPoint
	unsigned short	netID;		// 0 where the entity has none
	int				channel;	// the low byte alone, CHAN_LOOP riding bLoop
	int				soundId;	// an index into the table the client matched
	int				volume;		// SNAP_SOUND_VOLUME_NORM is left out
	int				atten;		// SNAP_SOUND_ATTEN_NORM is left out
};

namespace zx { namespace netsnapshot {

// The flags byte these fields imply. A point drops the loop, as SoundPoint's
// byte channel does: a loop with no actor could never be stopped.
int SnapSoundFlags(const SnapSoundWire& s);

// Writes the body at p, which holds SNAP_SOUND_MAX_BYTES, and gives its length.
unsigned int WriteSnapSound(BYTE* p, const SnapFrame& f, const SnapSoundWire& s);

// Reads one back. `wire` is the bytes each axis travels in, the frame's own
// widths unless the server announced ones this build cannot read.
void ReadSnapSound(SnapCosCursor& c, const SnapFrame& f, const BYTE wire[3], SnapSoundWire& s);

}} // namespace zx::netsnapshot

#endif	// __SNAP_SOUND_COMPUTE_H__
