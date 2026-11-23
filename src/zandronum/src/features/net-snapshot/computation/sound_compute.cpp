// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: sound_compute.cpp
//
// Description: SVC2_SNAPSOUND's body. See sound_compute.h.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/sound_compute.h"

using namespace zx::netsnapshot;

// One axis of a point, read inside the cursor's own bounds. A width this build
// cannot read is skipped, leaving the axis 0 and the rest of the body in step.
static fixed_t snd_ReadAxis(SnapCosCursor& c, const SnapFrame& f, unsigned int k, unsigned int wire)
{
	const bool bRead = (wire == PosBytes(f, k)) && ((c.p + wire) <= c.end);
	const fixed_t v = bRead ? DequantPos(ReadPosIn(c.p, f, k)) : (fixed_t)0;
	c.p += wire;
	return v;
}

int zx::netsnapshot::SnapSoundFlags(const SnapSoundWire& s)
{
	int flags = s.bPoint ? SNAP_SOUND_POINT : 0;
	if ((s.bPoint == false) && s.bLoop)
		flags |= SNAP_SOUND_LOOP;
	if (s.volume != SNAP_SOUND_VOLUME_NORM)
		flags |= SNAP_SOUND_VOLUME;
	if (s.atten != SNAP_SOUND_ATTEN_NORM)
		flags |= SNAP_SOUND_ATTEN;
	return flags;
}

unsigned int zx::netsnapshot::WriteSnapSound(BYTE* p, const SnapFrame& f, const SnapSoundWire& s)
{
	const int flags = SnapSoundFlags(s);
	unsigned int n = 0;
	// The stamp leads, as the sound table's and the cosmetic events' do, so a
	// client reads it before anything it would have to act on.
	p[n++] = s.stamp;
	p[n++] = (BYTE)flags;
	if (s.bPoint)
	{
		// Floored, as the cosmetic events are, and relative to the frame in its
		// width, so a narrow axis is the SHORT it always was.
		const fixed_t pos[3] = { s.x, s.y, s.z };
		for (unsigned int k = 0; k < 3; ++k)
			n += PutPosIn(p + n, f, k, FloorPosIn(f, k, pos[k]));
	}
	Put16(p + n, (WORD)s.netID);
	n += 2;
	p[n++] = (BYTE)(s.channel & 0xFF);
	Put16(p + n, (WORD)s.soundId);
	n += 2;
	if ((flags & SNAP_SOUND_VOLUME) != 0)
		p[n++] = (BYTE)s.volume;
	if ((flags & SNAP_SOUND_ATTEN) != 0)
		p[n++] = (BYTE)s.atten;
	return n;
}

void zx::netsnapshot::ReadSnapSound(SnapCosCursor& c, const SnapFrame& f, const BYTE wire[3],
	SnapSoundWire& s)
{
	// Past the end every read gives -1, which leaves the fields meaningless and
	// the cursor past `end`, where the caller refuses the body.
	s.stamp = (BYTE)CosRead8(c);
	const int flags = CosRead8(c);
	s.bPoint = ((flags & SNAP_SOUND_POINT) != 0);
	s.bLoop = ((flags & SNAP_SOUND_LOOP) != 0);
	s.x = (fixed_t)0;
	s.y = (fixed_t)0;
	s.z = (fixed_t)0;
	if (s.bPoint)
	{
		s.x = snd_ReadAxis(c, f, 0, wire[0]);
		s.y = snd_ReadAxis(c, f, 1, wire[1]);
		s.z = snd_ReadAxis(c, f, 2, wire[2]);
	}
	s.netID = (unsigned short)CosRead16(c);
	s.channel = CosRead8(c);
	s.soundId = (WORD)CosRead16(c);
	s.volume = ((flags & SNAP_SOUND_VOLUME) != 0) ? CosRead8(c) : SNAP_SOUND_VOLUME_NORM;
	s.atten = ((flags & SNAP_SOUND_ATTEN) != 0) ? CosRead8(c) : SNAP_SOUND_ATTEN_NORM;
}
