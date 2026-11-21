//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: sound_compute_test.cpp
//
// Description: Tests for sound_compute: the stamp, the flags, and the bytes
// an actor's sound and a point's sound are today, every field read back as it
// was written, a short body refused, and an axis width this build cannot read
// skipped without taking the rest of the body with it.
//
//-----------------------------------------------------------------------------

#include "sound_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] A frame with the origins and widths a case names.
static SnapFrame sndtest_Frame(int ox, int oy, int oz, BYTE wx, BYTE wy, BYTE wz)
{
	SnapFrame f = FrameNarrow();
	f.origin[0] = ox;
	f.origin[1] = oy;
	f.origin[2] = oz;
	f.width[0] = wx;
	f.width[1] = wy;
	f.width[2] = wz;
	return f;
}

// [Kaori-Chani] A body's own widths, which is what a client reads with while it understands
// the server's frame.
static void sndtest_Wire(const SnapFrame& f, BYTE wire[3])
{
	for (unsigned int k = 0; k < 3; ++k)
		wire[k] = f.width[k];
}

// [Kaori-Chani] An actor's sound at the defaults, as the fields arrive from a call site.
static SnapSoundWire sndtest_Actor(BYTE stamp, unsigned short netID, int channel, int soundId)
{
	SnapSoundWire s;
	s.stamp = stamp;
	s.bPoint = false;
	s.bLoop = false;
	s.x = (fixed_t)0;
	s.y = (fixed_t)0;
	s.z = (fixed_t)0;
	s.netID = netID;
	s.channel = channel;
	s.soundId = soundId;
	s.volume = SNAP_SOUND_VOLUME_NORM;
	s.atten = SNAP_SOUND_ATTEN_NORM;
	return s;
}

// [Kaori-Chani] The stamp is the first byte of every body, whatever else it carries, and it
// comes back as it went out at either end of its range.
TEST(SnapshotSound, TheStampLeadsEveryBody)
{
	const SnapFrame f = FrameNarrow();
	BYTE wire[3];
	sndtest_Wire(f, wire);
	static const BYTE stamps[] = { 0, 1, 42, 254, 255 };

	for (unsigned int i = 0; i < sizeof(stamps); ++i)
	{
		SnapSoundWire s = sndtest_Actor(stamps[i], 9, 1, 2);
		BYTE buf[SNAP_SOUND_MAX_BYTES];
		const unsigned int n = WriteSnapSound(buf, f, s);
		EXPECT_EQ(buf[0], stamps[i]) << "stamp " << (int)stamps[i];

		// And again with a point, whose positions sit behind the stamp, not ahead of it.
		s.bPoint = true;
		s.x = DequantPos(16);
		const unsigned int np = WriteSnapSound(buf, f, s);
		EXPECT_EQ(buf[0], stamps[i]) << "point stamp " << (int)stamps[i];
		EXPECT_EQ(np, n + 6u) << "point stamp " << (int)stamps[i];

		SnapSoundWire got;
		SnapCosCursor cur = { buf, buf + np };
		ReadSnapSound(cur, f, wire, got);
		EXPECT_EQ(got.stamp, stamps[i]);
		EXPECT_EQ(cur.p, buf + np);
	}
}

// [Kaori-Chani] The flags byte stands for what the body leaves out, and a point leaves out
// the loop whatever the call site asked for.
TEST(SnapshotSound, TheFlagsByteLeavesOutEveryDefault)
{
	SnapSoundWire s = sndtest_Actor(3, 7, 0, 9);
	EXPECT_EQ(SnapSoundFlags(s), 0);

	s.bLoop = true;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_LOOP);

	s.bPoint = true;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_POINT);

	s.bPoint = false;
	s.bLoop = false;
	s.volume = 60;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_VOLUME);

	s.volume = SNAP_SOUND_VOLUME_NORM;
	s.atten = 3;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_ATTEN);

	s.bPoint = true;
	s.bLoop = true;
	s.volume = 254;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_POINT | SNAP_SOUND_VOLUME | SNAP_SOUND_ATTEN);

	// The stamp is not a flag: it changes no bit of it.
	s.stamp = 255;
	EXPECT_EQ(SnapSoundFlags(s), SNAP_SOUND_POINT | SNAP_SOUND_VOLUME | SNAP_SOUND_ATTEN);
}

// [Kaori-Chani] The bytes an actor's sound travels as today: stamp, flags, NetID, channel,
// id, little-endian as NetCommand writes them and BYTESTREAM_s reads them.
TEST(SnapshotSound, AnActorsSoundIsTheSevenBytesItIsToday)
{
	const SnapFrame f = FrameNarrow();
	BYTE buf[SNAP_SOUND_MAX_BYTES];
	SnapSoundWire s = sndtest_Actor(0x29, 0x1234, 0, 0x0056);

	ASSERT_EQ(WriteSnapSound(buf, f, s), 7u);
	const BYTE want[] = { 0x29, 0x00, 0x34, 0x12, 0x00, 0x56, 0x00 };
	for (unsigned int b = 0; b < sizeof(want); ++b)
		EXPECT_EQ(buf[b], want[b]) << "byte " << b;

	// A loop on channel 5, with the two bytes the defaults would have left out.
	s.bLoop = true;
	s.channel = 5;
	s.volume = 200;
	s.atten = 3;
	ASSERT_EQ(WriteSnapSound(buf, f, s), 9u);
	const BYTE wantLoop[] = { 0x29, 0x0E, 0x34, 0x12, 0x05, 0x56, 0x00, 0xC8, 0x03 };
	for (unsigned int b = 0; b < sizeof(wantLoop); ++b)
		EXPECT_EQ(buf[b], wantLoop[b]) << "byte " << b;
}

// [Kaori-Chani] A point's three axes, floored and relative to the frame's origin, each in
// the width its axis travels in.
TEST(SnapshotSound, APointCarriesThreeAxesInTheFramesWidth)
{
	BYTE buf[SNAP_SOUND_MAX_BYTES];
	SnapSoundWire s = sndtest_Actor(0x29, 0x0001, 2, 3);
	s.bPoint = true;
	s.bLoop = true;							// dropped: a point's loop could never be stopped
	s.x = (fixed_t)(100 * 65536 + 32768);	// 100.5 floors to 100
	s.y = (fixed_t)(-3 * 65536 - 16384);	// -3.25 floors to -4
	s.z = (fixed_t)0;

	ASSERT_EQ(WriteSnapSound(buf, FrameNarrow(), s), 13u);
	const BYTE want[] = { 0x29, 0x01, 0x64, 0x00, 0xFC, 0xFF, 0x00, 0x00,
		0x01, 0x00, 0x02, 0x03, 0x00 };
	for (unsigned int b = 0; b < sizeof(want); ++b)
		EXPECT_EQ(buf[b], want[b]) << "byte " << b;

	// A wide axis spends three bytes, and each axis is relative to its own origin.
	const SnapFrame wide = sndtest_Frame(5000, -2000, 0, SNAP_FRAME_WIDE, SNAP_FRAME_NARROW,
		SNAP_FRAME_WIDE);
	s.x = DequantPos(7000);
	s.y = DequantPos(-2000);
	s.z = DequantPos(-1);
	ASSERT_EQ(WriteSnapSound(buf, wide, s), 15u);
	const BYTE wantWide[] = { 0x29, 0x01, 0xD0, 0x07, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
		0x01, 0x00, 0x02, 0x03, 0x00 };
	for (unsigned int b = 0; b < sizeof(wantWide); ++b)
		EXPECT_EQ(buf[b], wantWide[b]) << "byte " << b;
}

// [Kaori-Chani] Every field read back as it was written, so a sender and a handler cannot
// drift: the loop dropped on a point, the defaults filled in, the axes nothing without one.
TEST(SnapshotSound, EveryBodyReadsBackAsItWasWritten)
{
	static const struct
	{
		const char*		pszWhat;
		bool			bWide;
		BYTE			stamp;
		bool			bPoint, bLoop;
		int				qx, qy, qz;			// whole units, so the floor is exact
		unsigned short	netID;
		int				channel, soundId, volume, atten;
	} cases[] =
	{
		{ "an actor at the defaults", false, 0, false, false, 0, 0, 0, 55, 0, 12,
			SNAP_SOUND_VOLUME_NORM, SNAP_SOUND_ATTEN_NORM },
		{ "a looping actor", false, 1, false, true, 0, 0, 0, 1, 7, 1,
			SNAP_SOUND_VOLUME_NORM, SNAP_SOUND_ATTEN_NORM },
		{ "the loudest and the farthest", false, 255, false, false, 0, 0, 0, 65535, 255,
			65535, 254, 3 },
		{ "a silent-volume actor", false, 7, false, false, 0, 0, 0, 9, 1, 2, 0,
			SNAP_SOUND_ATTEN_NORM },
		{ "a point with no NetID", false, 64, true, false, 1000, -1000, 64, 0, 3, 40,
			SNAP_SOUND_VOLUME_NORM, SNAP_SOUND_ATTEN_NORM },
		{ "a point whose loop is dropped", false, 128, true, true, -32768, 32767, 0, 12, 0, 5,
			100, 2 },
		{ "a point in a wide frame", true, 200, true, false, 100000, -100000, 40000, 300, 4, 9,
			SNAP_SOUND_VOLUME_NORM, SNAP_SOUND_ATTEN_NORM },
	};

	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const SnapFrame f = cases[i].bWide ? FrameWidest() : FrameNarrow();
		BYTE wire[3];
		sndtest_Wire(f, wire);

		SnapSoundWire s = sndtest_Actor(cases[i].stamp, cases[i].netID, cases[i].channel,
			cases[i].soundId);
		s.bPoint = cases[i].bPoint;
		s.bLoop = cases[i].bLoop;
		s.x = DequantPos(cases[i].qx);
		s.y = DequantPos(cases[i].qy);
		s.z = DequantPos(cases[i].qz);
		s.volume = cases[i].volume;
		s.atten = cases[i].atten;

		BYTE buf[SNAP_SOUND_MAX_BYTES];
		const unsigned int n = WriteSnapSound(buf, f, s);
		ASSERT_LE(n, (unsigned int)SNAP_SOUND_MAX_BYTES) << cases[i].pszWhat;

		SnapSoundWire got;
		SnapCosCursor cur = { buf, buf + n };
		ReadSnapSound(cur, f, wire, got);
		EXPECT_EQ(cur.p, buf + n) << cases[i].pszWhat;
		EXPECT_EQ(got.stamp, s.stamp) << cases[i].pszWhat;
		EXPECT_EQ(got.bPoint, s.bPoint) << cases[i].pszWhat;
		EXPECT_EQ(got.bLoop, s.bPoint ? false : s.bLoop) << cases[i].pszWhat;
		EXPECT_EQ(got.netID, s.netID) << cases[i].pszWhat;
		EXPECT_EQ(got.channel, s.channel) << cases[i].pszWhat;
		EXPECT_EQ(got.soundId, s.soundId) << cases[i].pszWhat;
		EXPECT_EQ(got.volume, s.volume) << cases[i].pszWhat;
		EXPECT_EQ(got.atten, s.atten) << cases[i].pszWhat;
		EXPECT_EQ((long long)got.x, s.bPoint ? (long long)s.x : 0LL) << cases[i].pszWhat;
		EXPECT_EQ((long long)got.y, s.bPoint ? (long long)s.y : 0LL) << cases[i].pszWhat;
		EXPECT_EQ((long long)got.z, s.bPoint ? (long long)s.z : 0LL) << cases[i].pszWhat;
	}
}

// [Kaori-Chani] A body cut anywhere leaves the cursor past the end, which is what the
// handler refuses on before it reads the stamp, and reads nothing beyond it.
TEST(SnapshotSound, AShortBodyLeavesTheCursorPastTheEnd)
{
	const SnapFrame f = FrameWidest();
	BYTE wire[3];
	sndtest_Wire(f, wire);

	SnapSoundWire s = sndtest_Actor(0x11, 70, 1, 8);
	s.bPoint = true;
	s.x = DequantPos(12);
	s.y = DequantPos(-12);
	s.z = DequantPos(1);
	s.volume = 20;
	s.atten = 2;

	BYTE buf[SNAP_SOUND_MAX_BYTES];
	const unsigned int n = WriteSnapSound(buf, f, s);
	ASSERT_EQ(n, (unsigned int)SNAP_SOUND_MAX_BYTES);	// the widest body there is

	for (unsigned int cut = 0; cut < n; ++cut)
	{
		SnapSoundWire got;
		SnapCosCursor cur = { buf, buf + cut };
		ReadSnapSound(cur, f, wire, got);
		EXPECT_GT(cur.p, buf + cut) << "cut at " << cut;
	}

	// The whole body, by contrast, ends exactly on the end.
	SnapSoundWire got;
	SnapCosCursor cur = { buf, buf + n };
	ReadSnapSound(cur, f, wire, got);
	EXPECT_EQ(cur.p, buf + n);
	EXPECT_EQ(got.stamp, s.stamp);
}

// [Kaori-Chani] A width this build cannot read is skipped: the axes are nothing, and the
// NetID, channel and id behind them still read in step.
TEST(SnapshotSound, AnUnknownWidthSkipsItsAxisAndKeepsTheRestInStep)
{
	BYTE buf[SNAP_SOUND_MAX_BYTES];
	SnapSoundWire s = sndtest_Actor(0x5A, 4242, 6, 77);
	s.bPoint = true;
	s.x = DequantPos(70000);
	s.y = DequantPos(-70000);
	s.z = DequantPos(8);
	const unsigned int n = WriteSnapSound(buf, FrameWidest(), s);

	// This client understands a narrow frame only, so each wide axis is skipped
	// by the width the server announced for it.
	BYTE wire[3] = { SNAP_FRAME_WIDE, SNAP_FRAME_WIDE, SNAP_FRAME_WIDE };
	SnapSoundWire got;
	SnapCosCursor cur = { buf, buf + n };
	ReadSnapSound(cur, FrameNarrow(), wire, got);

	EXPECT_EQ(cur.p, buf + n);
	EXPECT_EQ(got.stamp, 0x5A);
	EXPECT_EQ((long long)got.x, 0LL);
	EXPECT_EQ((long long)got.y, 0LL);
	EXPECT_EQ((long long)got.z, 0LL);
	EXPECT_TRUE(got.bPoint);
	EXPECT_EQ(got.netID, 4242);
	EXPECT_EQ(got.channel, 6);
	EXPECT_EQ(got.soundId, 77);
}
