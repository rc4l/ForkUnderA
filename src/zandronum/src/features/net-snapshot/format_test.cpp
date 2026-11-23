// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: format_test.cpp
//
// Description: Tests for format.h's position frame: the extent rule, every
// position field on both sides of a nonzero origin at the narrow limits and
// one past them, the checksum's narrow pin, and the same again on a frame
// wide in x and y which no 32-bit build ever produced.
//
//-----------------------------------------------------------------------------

#include "format.h"
#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>		// abs

#include "doomdef.h"		// TICRATE

using namespace zx::netsnapshot;

// [Kaori-Chani] The checksum block's inputs, the same on every compiler, unlike rand(), whose 15
// bits on MSVC never reached the sign bit or a rounding half.
#define FORMATTEST_SEED		0x9E3779B9u

static DWORD formattest_Next(DWORD& s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

// [Kaori-Chani] A frame with a nonzero origin, which a 32-bit fixed_t never produced, so only
// this runs the offset paths. x spans [-32668, 32867] and y [-32868, 32667], so each axis has one
// limit a 16.16 fixed_t could reach; a 48.16 one reaches both, and the cases below use the
// reachable ones either way.
static SnapFrame formattest_OffsetFrame(unsigned int* pClamped)
{
	SnapFrame f = FrameNarrow();
	f.origin[0] = 100;
	f.origin[1] = -100;
	f.origin[2] = 7;
	f.pClamped = pClamped;
	return f;
}

// [Kaori-Chani] A frame wide in x and y, narrow in z, placed so each wide axis has one limit a
// 16.16 fixed_t reaches: x spans [-1000, 16776215] and y [-16776216, 999].
static SnapFrame formattest_WideFrame(unsigned int* pClamped)
{
	SnapFrame f = FrameNarrow();
	f.origin[0] = 8387608;
	f.origin[1] = -8387608;
	f.origin[2] = 7;
	f.width[0] = SNAP_FRAME_WIDE;
	f.width[1] = SNAP_FRAME_WIDE;
	f.pClamped = pClamped;
	return f;
}

static const fixed_t FORMATTEST_XLO = -32668 * FRACUNIT;
// [Kaori-Chani] The top of y's 16.16 range from the origin, which a 48.16 fixed_t still holds.
static const fixed_t FORMATTEST_YHI = -100 * FRACUNIT + (fixed_t)0x7FFFFFFF;

// [Kaori-Chani] Origin 0 while the extent and its clamped margin fit plus or minus 32k, else
// centred, wide past 65,535 units and overflowing past 16,777,215.
TEST(SnapshotFormat, TheExtentDecidesTheFramesOriginAndWidth)
{
	const long long lo[3] = { -1000, -30000, 0 }, hi[3] = { 1000, 30000, 100 };
	bool bOver = true;
	const SnapFrame a = FrameFromExtent(lo, hi, -32768, 32767, &bOver);
	EXPECT_EQ(0, a.origin[0]);
	EXPECT_EQ(0, a.origin[1]);
	EXPECT_EQ(0, a.origin[2]);
	EXPECT_FALSE(bOver);
	EXPECT_EQ(SNAP_FRAME_NARROW, a.width[0]);
	EXPECT_EQ(SNAP_FRAME_NARROW, a.width[1]);

	const long long lo2[3] = { 10000, -50000, -5000 }, hi2[3] = { 60000, 100000, 0 };
	bool bOver2 = true;
	const SnapFrame b = FrameFromExtent(lo2, hi2, -0x80000000LL, 0x7FFFFFFFLL, &bOver2);
	EXPECT_EQ(35000, b.origin[0]);
	EXPECT_EQ(25000, b.origin[1]);
	EXPECT_EQ(0, b.origin[2]);
	EXPECT_FALSE(bOver2);
	EXPECT_EQ(SNAP_FRAME_NARROW, b.width[0]);
	EXPECT_EQ(SNAP_FRAME_WIDE, b.width[1]);
	EXPECT_EQ(SNAP_FRAME_NARROW, b.width[2]);
	EXPECT_EQ(NULL, b.pClamped);

	const long long lo3[3] = { -10000000, 0, 0 }, hi3[3] = { 10000000, 0, 0 };
	bool bOver3 = false;
	const SnapFrame c = FrameFromExtent(lo3, hi3, -0x80000000LL, 0x7FFFFFFFLL, &bOver3);
	EXPECT_EQ(0, c.origin[0]);
	EXPECT_EQ(SNAP_FRAME_WIDE, c.width[0]);
	EXPECT_TRUE(bOver3) << "an axis past 16,777,215 units overflows a wide frame";
}

// [Kaori-Chani] Rounded and floored, each clamped at both ends and counted; the floored values
// relative to the origin as the point sound sends them.
TEST(SnapshotFormat, TheFramesQuantizersRoundFloorAndClamp)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_OffsetFrame(&clamps);

	EXPECT_EQ(-32668, QuantPosIn(f, 0, FORMATTEST_XLO));
	EXPECT_EQ(-32668, QuantPosIn(f, 0, FORMATTEST_XLO - FRACUNIT / 2 - 1));
	EXPECT_EQ(32667, QuantPosIn(f, 1, 32667 * FRACUNIT + FRACUNIT / 2 - 1));
	EXPECT_EQ(32667, QuantPosIn(f, 1, 32667 * FRACUNIT + FRACUNIT / 2));

	EXPECT_EQ(-32668, FloorPosIn(f, 0, FORMATTEST_XLO));
	EXPECT_EQ(-32668, FloorPosIn(f, 0, FORMATTEST_XLO - 1));
	EXPECT_EQ(32667, FloorPosIn(f, 1, 32668 * FRACUNIT - 1));
	EXPECT_EQ(32667, FloorPosIn(f, 1, 32668 * FRACUNIT));

	// [Kaori-Chani] 99.5 floors to 99, one below the origin; -0.5 to -1; 100.5 to the origin.
	const fixed_t v[3] =
		{ 99 * FRACUNIT + FRACUNIT / 2, -FRACUNIT / 2, 100 * FRACUNIT + FRACUNIT / 2 };
	const int rel[3] = { -1, -101, 0 };
	for (unsigned int i = 0; i < 3; ++i)
	{
		const SDWORD r = PosRel(f, 0, FloorPosIn(f, 0, v[i]));
		EXPECT_EQ(rel[i], r) << "case " << i;
		EXPECT_EQ(v[i] & ~(FRACUNIT - 1), DequantPos(PosAbs(f, 0, r))) << "case " << i;
	}

	EXPECT_EQ(4u, clamps) << "two rounded and two floored values sit past a limit";
}

// [Kaori-Chani] The values are already in the frame, so nothing clamps: the limits, and either
// side of the origin.
TEST(SnapshotFormat, TheFramesGroupsAndBranchEventRoundTrip)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_OffsetFrame(&clamps);
	const SDWORD xs[] = { -32668, 99, 100, 101, 32867 };
	const SDWORD ys[] = { -32868, -101, -100, -99, 32667 };
	const SDWORD zs[] = { -32761, 6, 7, 8, 32774 };

	for (unsigned int i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
	{
		SnapEntityState e, r;
		memset(&e, 0, sizeof(e));
		memset(&r, 0, sizeof(r));
		e.qx = xs[i];
		e.qy = ys[i];
		e.qz = zs[i];
		BYTE buf[SNAP_MAX_EVENT_BYTES];
		WriteGroups(buf, f, e, (BYTE)(SNAPGRP_POS | SNAPGRP_POSZ));
		ReadGroups(buf, f, r, (BYTE)(SNAPGRP_POS | SNAPGRP_POSZ));
		EXPECT_EQ((WORD)(SWORD)(xs[i] - 100), Read16(buf)) << "case " << i;
		EXPECT_EQ((WORD)(SWORD)(ys[i] + 100), Read16(buf + 2)) << "case " << i;
		EXPECT_EQ(e.qx, r.qx) << "case " << i;
		EXPECT_EQ(e.qy, r.qy) << "case " << i;
		EXPECT_EQ(e.qz, r.qz) << "case " << i;

		memset(&r, 0, sizeof(r));
		WriteEventBody(buf, f, SNAPEV_BRANCH, e, 0);
		ReadEvent(buf + SNAP_EVENT_MASK_BYTES, f, SNAPEV_BRANCH, r);
		EXPECT_EQ(e.qx, r.qx) << "branch, case " << i;
		EXPECT_EQ(e.qy, r.qy) << "branch, case " << i;
		EXPECT_EQ(e.qz, r.qz) << "branch, case " << i;
	}
	EXPECT_EQ(0u, clamps);
}

// [Kaori-Chani] 16.16 relative to the origin: the limits exactly, a fraction either side of the
// origin, and one step past each limit, which clamps to it.
TEST(SnapshotFormat, TheFramesLocalBlockAndPolyobjectClampAtTheirLimits)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_OffsetFrame(&clamps);
	BYTE buf[SNAP_MAX_EVENT_BYTES];
	SnapLocalState s, b;
	memset(&s, 0, sizeof(s));
	s.x = FORMATTEST_XLO;
	s.y = FORMATTEST_YHI;
	s.z = 7 * FRACUNIT - 1;
	memset(&b, 0, sizeof(b));

	WriteLocalBody(buf, f, s);
	ReadLocal(buf + SNAP_EVENT_MASK_BYTES, f, b);
	EXPECT_EQ(s.x, b.x);
	EXPECT_EQ(s.y, b.y);
	EXPECT_EQ(s.z, b.z);
	EXPECT_EQ(0u, clamps);

	s.x = FORMATTEST_XLO - 1;
	s.y = FORMATTEST_YHI + 1;
	WriteLocalBody(buf, f, s);
	ReadLocal(buf + SNAP_EVENT_MASK_BYTES, f, b);
	EXPECT_EQ(FORMATTEST_XLO, b.x);
	EXPECT_EQ(FORMATTEST_YHI, b.y);
	EXPECT_EQ(2u, clamps);

	SnapMoverPoly a, p;
	memset(&a, 0, sizeof(a));
	a.x = 99 * FRACUNIT + FRACUNIT / 2;
	a.y = -101 * FRACUNIT;
	a.count = 1;
	a.e[0].kind = SNAPPO_MOVETO;
	a.e[0].xTarget = FORMATTEST_XLO - 1;
	a.e[0].yTarget = 12345;
	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int n = WriteMoverPoly(body, f, a);
	memset(&p, 0, sizeof(p));
	EXPECT_EQ(0xFFFF8000u, Read32(body));
	ASSERT_TRUE(ReadMoverPoly(body, f, n, p));
	EXPECT_EQ(a.x, p.x);
	EXPECT_EQ(a.y, p.y);
	EXPECT_EQ(FORMATTEST_XLO, p.e[0].xTarget);
	EXPECT_EQ(12345, p.e[0].yTarget);
	EXPECT_EQ(3u, clamps);
}

// [Kaori-Chani] Floored before the origin is taken, so a fraction below the origin is -1, and a
// fraction below 0 is the unit under it.
TEST(SnapshotFormat, TheFramesCosmeticEventsFloorBeforeTheOrigin)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_OffsetFrame(&clamps);
	BYTE buf[40];
	bool bBig, bSmall;
	SnapCosGeneric g, gr;
	memset(&g, 0, sizeof(g));
	g.x = 99 * FRACUNIT + FRACUNIT / 2;
	g.y = -100 * FRACUNIT - FRACUNIT / 4;
	g.z = 7 * FRACUNIT;
	g.stateCode = -1;
	const unsigned int gn = CosWriteGeneric(buf, f, SNAP_COSE_PLAIN, g, bBig, bSmall);
	SnapCosCursor gc = { buf + 1, buf + gn };
	CosReadGeneric(gc, f, buf[0], gr);
	EXPECT_EQ(0xFFFF, Read16(buf + 3));
	EXPECT_EQ(0xFFFF, Read16(buf + 5));
	EXPECT_EQ(0, Read16(buf + 7));
	EXPECT_EQ(99 * FRACUNIT, gr.x);
	EXPECT_EQ(-101 * FRACUNIT, gr.y);
	EXPECT_EQ(7 * FRACUNIT, gr.z);
	EXPECT_EQ(0u, clamps);

	SnapCosBlood c, cr;
	memset(&c, 0, sizeof(c));
	c.x = FORMATTEST_XLO - 1;
	c.y = 32667 * FRACUNIT + FRACUNIT - 1;
	c.z = -1;
	const unsigned int cn = CosWriteBlood(buf, f, SNAP_COSE_SPLATTER, c);
	SnapCosCursor cc = { buf + 1, buf + cn };
	CosReadBlood(cc, f, buf[0], cr);
	EXPECT_EQ(FORMATTEST_XLO, cr.x);
	EXPECT_EQ(32667 * FRACUNIT, cr.y);
	EXPECT_EQ(-FRACUNIT, cr.z);
	EXPECT_EQ(1u, clamps);
}

// [Kaori-Chani] A narrow position hashes as it did before the frame existed, pinned over 4,096
// drawn states, and a wide one differs from the narrow value sharing its low 16 bits.
TEST(SnapshotFormat, TheStateChecksumKeepsItsNarrowPin)
{
	DWORD seed = FORMATTEST_SEED, acc = 0;
	for (int i = 0; i < 4096; ++i)
	{
		SnapEntityState e;
		memset(&e, 0, sizeof(e));
		e.qx = (SWORD)formattest_Next(seed);
		e.qy = (SWORD)formattest_Next(seed);
		e.qz = (SWORD)formattest_Next(seed);
		e.qangle = (WORD)formattest_Next(seed);
		e.qpitch = (WORD)formattest_Next(seed);
		e.qvx = (SWORD)formattest_Next(seed);
		e.qvy = (SWORD)formattest_Next(seed);
		e.qvz = (SWORD)formattest_Next(seed);
		e.qstate = (WORD)formattest_Next(seed);
		e.renderstate = formattest_Next(seed);
		e.effects = (BYTE)formattest_Next(seed);
		e.misc = (WORD)formattest_Next(seed);
		acc = (acc * 31u) ^ StateChecksum(e);
	}
	EXPECT_EQ(0xEBA815A2u, acc);

	SnapEntityState w, n;
	memset(&w, 0, sizeof(w));
	memset(&n, 0, sizeof(n));
	w.qx = 40000;
	n.qx = 40000 - 65536;
	EXPECT_NE(StateChecksum(w), StateChecksum(n))
		<< "a wide position must not hash as the narrow one sharing its low 16 bits";
}

static const fixed_t FORMATTEST_WIDE_XLO = -1000 * FRACUNIT;
static const fixed_t FORMATTEST_WIDE_YHI = 999 * FRACUNIT + (FRACUNIT - 1);

// [Kaori-Chani] The limits on the wide axes, rounded and floored, and one past each.
TEST(SnapshotFormat, AWideFramesQuantizersRoundFloorAndClamp)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_WideFrame(&clamps);

	EXPECT_EQ(-1000, QuantPosIn(f, 0, FORMATTEST_WIDE_XLO));
	EXPECT_EQ(-1000, QuantPosIn(f, 0, FORMATTEST_WIDE_XLO - FRACUNIT / 2 - 1));
	EXPECT_EQ(999, QuantPosIn(f, 1, 999 * FRACUNIT + FRACUNIT / 2 - 1));
	EXPECT_EQ(999, QuantPosIn(f, 1, 999 * FRACUNIT + FRACUNIT / 2));

	EXPECT_EQ(-1000, FloorPosIn(f, 0, FORMATTEST_WIDE_XLO));
	EXPECT_EQ(-1000, FloorPosIn(f, 0, FORMATTEST_WIDE_XLO - 1));
	EXPECT_EQ(999, FloorPosIn(f, 1, FORMATTEST_WIDE_YHI));
	EXPECT_EQ(999, FloorPosIn(f, 1, FORMATTEST_WIDE_YHI + 1));

	EXPECT_EQ(4u, clamps);
}

// [Kaori-Chani] Three bytes an axis on x and y, two on z, at the limits and either side of the
// origin, through the writer, the three length functions and the reader.
TEST(SnapshotFormat, AWideFramesGroupsAndBranchEventRoundTripInThreeBytes)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_WideFrame(&clamps);
	const SDWORD xs[] = { -1000, 8387607, 8387608, 8387609, 16776215 };
	const SDWORD ys[] = { -16776216, -8387609, -8387608, -8387607, 999 };
	const SDWORD zs[] = { -32761, 6, 7, 8, 32774 };
	const BYTE mask = (BYTE)(SNAPGRP_POS | SNAPGRP_POSZ);

	EXPECT_EQ(8u, GroupBytes(f, mask));
	EXPECT_EQ(8u, GroupBytesBeforeAppear(f, mask));
	EXPECT_EQ((unsigned int)(SNAP_EVBYTES_BRANCH + 2), EventBytes(f, SNAPEV_BRANCH));

	for (unsigned int i = 0; i < sizeof(xs) / sizeof(xs[0]); ++i)
	{
		SnapEntityState e, r;
		memset(&e, 0, sizeof(e));
		memset(&r, 0, sizeof(r));
		e.qx = xs[i];
		e.qy = ys[i];
		e.qz = zs[i];
		BYTE buf[SNAP_MAX_EVENT_BYTES];
		const unsigned int n = WriteGroups(buf, f, e, mask);
		unsigned int need = 0;
		EXPECT_EQ(8u, n) << "case " << i;
		EXPECT_EQ(8u, GroupBytesFor(f, mask, e)) << "case " << i;
		EXPECT_TRUE(GroupBytesRead(f, mask, buf, n, need)) << "case " << i;
		EXPECT_EQ(8u, need) << "case " << i;
		EXPECT_FALSE(GroupBytesRead(f, mask, buf, n - 1, need)) << "case " << i;
		EXPECT_EQ(xs[i] - 8387608, ReadSigned(buf, 3)) << "case " << i;
		EXPECT_EQ(ys[i] + 8387608, ReadSigned(buf + 3, 3)) << "case " << i;
		EXPECT_EQ(zs[i] - 7, ReadSigned(buf + 6, 2)) << "case " << i;

		ReadGroups(buf, f, r, mask);
		EXPECT_EQ(e.qx, r.qx) << "case " << i;
		EXPECT_EQ(e.qy, r.qy) << "case " << i;
		EXPECT_EQ(e.qz, r.qz) << "case " << i;

		memset(&r, 0, sizeof(r));
		const unsigned int en = WriteEventBody(buf, f, SNAPEV_BRANCH, e, 0);
		ReadEvent(buf + SNAP_EVENT_MASK_BYTES, f, SNAPEV_BRANCH, r);
		EXPECT_EQ(SNAP_EVENT_MASK_BYTES + EventBytes(f, SNAPEV_BRANCH), en) << "case " << i;
		EXPECT_EQ(e.qx, r.qx) << "branch, case " << i;
		EXPECT_EQ(e.qy, r.qy) << "branch, case " << i;
		EXPECT_EQ(e.qz, r.qz) << "branch, case " << i;
	}
	EXPECT_EQ(0u, clamps);
}

// [Kaori-Chani] Five bytes of 16.16 on a wide axis: the limits exactly, then one step past each,
// which clamps to it.
TEST(SnapshotFormat, AWideFramesLocalBlockAndPolyobjectTakeFiveBytesAnAxis)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_WideFrame(&clamps);
	BYTE buf[SNAP_MAX_EVENT_BYTES];
	SnapLocalState s, b;
	memset(&s, 0, sizeof(s));
	s.x = FORMATTEST_WIDE_XLO;
	s.y = FORMATTEST_WIDE_YHI;
	s.z = 7 * FRACUNIT - 1;
	s.jumpTics = -2;
	memset(&b, 0, sizeof(b));

	const unsigned int n = WriteLocalBody(buf, f, s);
	ReadLocal(buf + SNAP_EVENT_MASK_BYTES, f, b);
	EXPECT_EQ(SNAP_EVENT_MASK_BYTES + EventBytes(f, SNAPEV_LOCAL), n);
	EXPECT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + SNAP_EVBYTES_LOCAL + 2), n);
	EXPECT_EQ(s.x, b.x);
	EXPECT_EQ(s.y, b.y);
	EXPECT_EQ(s.z, b.z);
	EXPECT_EQ(s.jumpTics, b.jumpTics);
	EXPECT_EQ(0u, clamps);

	s.x = FORMATTEST_WIDE_XLO - 1;
	s.y = FORMATTEST_WIDE_YHI + 1;
	WriteLocalBody(buf, f, s);
	ReadLocal(buf + SNAP_EVENT_MASK_BYTES, f, b);
	EXPECT_EQ(FORMATTEST_WIDE_XLO, b.x);
	EXPECT_EQ(FORMATTEST_WIDE_YHI, b.y);
	EXPECT_EQ(2u, clamps);

	SnapMoverPoly a, p;
	memset(&a, 0, sizeof(a));
	a.x = FORMATTEST_WIDE_XLO;
	a.y = FORMATTEST_WIDE_YHI;
	a.angle = 0x40000000u;
	a.count = 2;
	a.e[0].kind = SNAPPO_MOVETO;
	a.e[0].xTarget = FORMATTEST_WIDE_XLO - 1;
	a.e[0].yTarget = 12345;
	a.e[1].kind = SNAPPO_ROTATE;
	a.e[1].speed = -7;
	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int bn = WriteMoverPoly(body, f, a);
	const unsigned int want = PolyHeaderBytes(f) + PolyEntryBytes(f, SNAPPO_MOVETO)
		+ PolyEntryBytes(f, SNAPPO_ROTATE);
	memset(&p, 0, sizeof(p));
	EXPECT_EQ(want, bn);
	EXPECT_EQ((unsigned int)(SNAPPO_HEADER_BYTES + 2 + SNAPPO_MOVETO_BYTES + 2
		+ SNAPPO_ENTRY_BYTES), want);
	EXPECT_EQ(bn, MoverPolyBodyBytes(f, body, bn));
	EXPECT_EQ(0u, MoverPolyBodyBytes(f, body, bn - 1)) << "one byte short reads as no body";
	ASSERT_TRUE(ReadMoverPoly(body, f, bn, p));
	EXPECT_EQ(a.x, p.x);
	EXPECT_EQ(a.y, p.y);
	EXPECT_EQ(a.angle, p.angle);
	EXPECT_EQ(2u, p.count);
	EXPECT_EQ(FORMATTEST_WIDE_XLO, p.e[0].xTarget);
	EXPECT_EQ(12345, p.e[0].yTarget);
	EXPECT_EQ(-7, p.e[1].speed);
	EXPECT_EQ(3u, clamps);
}

// [Kaori-Chani] Floored, then relative in three bytes: the limits exactly, and one past each on a
// blood event, which clamps.
TEST(SnapshotFormat, AWideFramesCosmeticEventsTakeThreeBytesAnAxis)
{
	unsigned int clamps = 0;
	const SnapFrame f = formattest_WideFrame(&clamps);
	BYTE buf[SNAP_COSE_GENERIC_MAX + 8];
	bool bBig, bSmall;
	SnapCosGeneric g, gr;
	memset(&g, 0, sizeof(g));
	g.x = FORMATTEST_WIDE_XLO + FRACUNIT / 2;
	g.y = FORMATTEST_WIDE_YHI;
	g.z = 7 * FRACUNIT;
	g.stateCode = -1;
	const unsigned int gn = CosWriteGeneric(buf, f, SNAP_COSE_PLAIN, g, bBig, bSmall);
	SnapCosCursor gc = { buf + 1, buf + gn };
	CosReadGeneric(gc, f, buf[0], gr);
	EXPECT_EQ(13u + 2u, gn);
	EXPECT_EQ(gc.end, gc.p);
	EXPECT_EQ(-1000 - 8387608, ReadSigned(buf + 3, 3));
	EXPECT_EQ(999 + 8387608, ReadSigned(buf + 6, 3));
	EXPECT_EQ(0, ReadSigned(buf + 9, 2));
	EXPECT_EQ(FORMATTEST_WIDE_XLO, gr.x);
	EXPECT_EQ(999 * FRACUNIT, gr.y);
	EXPECT_EQ(7 * FRACUNIT, gr.z);
	EXPECT_EQ(0u, clamps);

	// [Kaori-Chani] Every generic field, so the longest body is the constant that sizes it.
	g.vel[0] = FRACUNIT / 2;
	g.stateCode = 0x3005;
	g.extras = SNAP_COSX_ACTED | SNAP_COSX_TRANSLATION | SNAP_COSX_ALPHA | SNAP_COSX_SCALE
		| SNAP_COSX_STYLE;
	const SnapFrame widest = FrameWidest();
	EXPECT_EQ((unsigned int)SNAP_COSE_GENERIC_MAX,
		CosWriteGeneric(buf, widest, SNAP_COSE_PLAIN, g, bBig, bSmall));

	SnapCosBlood c, cr;
	memset(&c, 0, sizeof(c));
	c.x = FORMATTEST_WIDE_XLO - 1;
	c.y = FORMATTEST_WIDE_YHI + 1;
	c.z = -1;
	const unsigned int cn = CosWriteBlood(buf, f, SNAP_COSE_SPLATTER, c);
	SnapCosCursor cc = { buf + 1, buf + cn };
	CosReadBlood(cc, f, buf[0], cr);
	EXPECT_EQ(9u + 2u, cn);
	EXPECT_EQ(cc.end, cc.p);
	EXPECT_EQ(FORMATTEST_WIDE_XLO, cr.x);
	EXPECT_EQ(999 * FRACUNIT, cr.y);
	EXPECT_EQ(-FRACUNIT, cr.z);
	EXPECT_EQ(2u, clamps);

	// [Kaori-Chani] One byte short reads past the end, as a truncated event is found.
	SnapCosCursor cut = { buf + 1, buf + cn - 1 };
	CosReadBlood(cut, f, buf[0], cr);
	EXPECT_GT(cut.p, cut.end);
}

//*****************************************************************************
//	THE RECORD AND EVENT CODECS
//
// The runner's remaining blocks: the codecs end to end, the lengths the writer
// and reader must agree on, and the quantizer sweeps over a fixed seed. The
// runner's time base block is not here; it tests i_tictime, which belongs with
// the posix timers rather than this module.

#define FORMATTEST_SAMPLES	200000

namespace
{

const SnapFrame FORMATTEST_NARROW = FrameNarrow();

// [Kaori-Chani] The blocks below run on the narrow frame and the widest.
const SnapFrame FORMATTEST_FRAMES[2] = { FrameNarrow(), FrameWidest() };

// [Kaori-Chani] A random 32-bit pattern read as a signed 16.16 value, which is what a 32-bit
// fixed_t did with it: the sweep must still cover negative positions and velocities.
fixed_t formattest_Sample(DWORD& s)
{
	return fixed_t::FromSignedBits(formattest_Next(s));
}

// [Kaori-Chani] Velocity clamps to plus or minus 8191/8192 steps and rounds to nearest, so the
// error against the clamped input is within half a step either way.
bool formattest_VelBad(fixed_t v)
{
	const fixed_t rt = DequantVel(QuantVel(v));
	if (v > DequantVel(8191)) v = DequantVel(8191);
	if (v < DequantVel(-8192)) v = DequantVel(-8192);
	return ((v - rt) < -(1 << 13)) || ((v - rt) >= (1 << 13));
}

} // namespace

TEST(SnapshotFormat, EffectsPackingRoundTripsOverEveryValue)
{
	for (unsigned int e = 0; e < 256; ++e)
		EXPECT_EQ((BYTE)e, PackEffects(UnpackEffects((BYTE)e))) << "effects 0x" << std::hex << e;
}

// [Kaori-Chani] Position rounds to nearest: within half a step either way, and within one step
// below above 0x7FFF7FFF, where the top step is held.
TEST(SnapshotFormat, PositionRoundsToNearestOverTheSweep)
{
	DWORD seed = FORMATTEST_SEED;
	int bad = 0;
	for (int i = 0; i < FORMATTEST_SAMPLES; ++i)
	{
		const fixed_t v = formattest_Sample(seed);
		const SQWORD d = (SQWORD)v - (SQWORD)DequantPos(QuantPos(v));
		if ((v > fixed_t::FromSignedBits(0x7FFF7FFFu)) ? ((d < 0x8000) || (d > 0xFFFF))
			: ((d < -0x8000) || (d > 0x7FFF)))
			++bad;
	}
	EXPECT_EQ(0, bad) << bad << " of " << FORMATTEST_SAMPLES << " samples (seed 0x"
		<< std::hex << (unsigned int)FORMATTEST_SEED << ")";
}

// [Kaori-Chani] The 32-bit extremes land on the narrow frame's ends, so a precision change that
// broke it shows here. On 48.16 a value past them clamps to the same ends rather than wrapping.
TEST(SnapshotFormat, PositionQuantizationSpansThe32BitRangeAndClampsPastIt)
{
	EXPECT_EQ(32767, QuantPos((fixed_t)0x7FFFFFFF));
	EXPECT_EQ(-32768, QuantPos(fixed_t::FromSignedBits(0x80000000u)));
	EXPECT_EQ(32767, QuantPos(FIXED_MAX)) << "a 48.16 position past the frame clamps to its end";
	EXPECT_EQ(-32768, QuantPos(FIXED_MIN));
}

// [Kaori-Chani] Written, read back, then walked as the decoder walks it, the payload past the
// length byte a self-describing type carries.
TEST(SnapshotFormat, AMoverRecordRoundTripsThroughTheDecodersWalk)
{
	SnapMoverPlat a;
	memset(&a, 0, sizeof(a));
	a.speed = 0x12345678; a.low = -4096; a.high = 0x7FFFFFFF;
	a.planeD = -123456; a.wait = 105; a.count = -3; a.tag = 999;
	a.platID = -1; a.crush = -1; a.status = 2; a.oldStatus = 1; a.type = 9;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int bn = WriteMoverPlat(body, a);
	BYTE mrec[SNAP_MAX_EVENT_BYTES];
	const unsigned int rn = WriteMoverBody(mrec, SNAPMV_PLAT, SNAPMVF_BLOCKED, 4242, 0xA5,
		body, bn);
	ASSERT_EQ((unsigned int)SNAPMV_PLAT_BYTES, bn);
	ASSERT_LE(rn, (unsigned int)SNAP_MAX_EVENT_BYTES);

	unsigned int n = 0;
	bool bSkip = true;
	const BYTE type = (BYTE)(mrec[0] & SNAPEV_TYPEMASK);
	ASSERT_EQ(SNAPEVLEN_OK,
		EventRecordBytes(FORMATTEST_NARROW, type, mrec + 1, rn - 1, n, bSkip));
	EXPECT_FALSE(bSkip);
	ASSERT_EQ(rn - 1, n);

	const BYTE * const p = mrec + 1 + 1;	// past mask, past length
	BYTE cls = 0, mflags = 0, gen = 0;
	WORD index = 0;
	SnapMoverPlat b;
	memset(&b, 0, sizeof(b));
	ReadMoverHeader(p, MoverHeaderBytes(ZSNAP_VERSION), cls, mflags, index, gen);
	ReadMoverPlat(p + SNAPMV_HEADER_BYTES, b);
	EXPECT_EQ(SNAPMV_PLAT, cls);
	EXPECT_EQ(SNAPMVF_BLOCKED, mflags);
	EXPECT_EQ(4242, index);
	EXPECT_EQ(0xA5, gen);
	EXPECT_EQ(n - 1, SNAPMV_HEADER_BYTES + MoverBodyBytes(cls, mflags));
	EXPECT_EQ(0, memcmp(&a, &b, sizeof(a)));
}

// [Kaori-Chani] A polyobject body with one entry of every shape, walked by the reader's own path:
// 102 bytes narrow, 106 with x and y wide.
TEST(SnapshotFormat, APolyobjectBodyRoundTripsInBothFrames)
{
	for (unsigned int fi = 0; fi < 2; ++fi)
	{
		const SnapFrame& fr = FORMATTEST_FRAMES[fi];
		SnapMoverPoly a;
		memset(&a, 0, sizeof(a));
		a.x = -123456; a.y = 0x7FFF0000; a.angle = 0xC0000000u; a.count = 4;
		a.e[0].kind = SNAPPO_ROTATE | SNAPPO_BLOCKED; a.e[0].gen = 7;
		a.e[0].speed = -5000; a.e[0].dist = -2;
		a.e[1].kind = SNAPPO_MOVE | SNAPPO_NOINTERP; a.e[1].gen = 8;
		a.e[1].dist = 99; a.e[1].angle = 4095; a.e[1].xSpeed = -3; a.e[1].ySpeed = 4;
		a.e[2].kind = SNAPPO_MOVETO | SNAPPO_SPECIAL; a.e[2].gen = 9;
		a.e[2].xTarget = 3; a.e[2].yTarget = -4;
		a.e[3].kind = SNAPPO_SWING; a.e[3].gen = 10; a.e[3].direction = -1;
		a.e[3].totalDist = 64 << FRACBITS; a.e[3].tics = 5;
		a.e[3].waitTics = 70; a.e[3].close = 1;

		BYTE body[SNAPMV_BODY_MAX];
		const unsigned int bn = WriteMoverPoly(body, fr, a);
		BYTE mrec[SNAP_MAX_EVENT_BYTES];
		const unsigned int rn = WriteMoverBody(mrec, SNAPMV_POLY, SNAPMVF_BLOCKED, 17, 0x5A,
			body, bn);
		const BYTE * const p = mrec + 1 + 1;	// past mask, past length
		BYTE cls = 0, mflags = 0, gen = 0;
		WORD index = 0;
		ReadMoverHeader(p, SNAPMV_HEADER_BYTES, cls, mflags, index, gen);
		const unsigned int want = MoverRecordBodyBytes(fr, cls, mflags,
			p + SNAPMV_HEADER_BYTES, rn - 2 - SNAPMV_HEADER_BYTES);
		SnapMoverPoly b;
		memset(&b, 0, sizeof(b));

		EXPECT_LE(bn, (unsigned int)SNAPMV_BODY_MAX) << "frame " << fi;
		EXPECT_EQ(bn, want) << "frame " << fi;
		EXPECT_EQ((fi == 0) ? 102u : 106u, bn) << "frame " << fi;
		EXPECT_LE(rn, (unsigned int)SNAP_MAX_EVENT_BYTES) << "frame " << fi;
		ASSERT_TRUE(ReadMoverPoly(p + SNAPMV_HEADER_BYTES, fr, want, b)) << "frame " << fi;
		EXPECT_EQ(SNAPMV_POLY, cls) << "frame " << fi;
		EXPECT_EQ(17, index) << "frame " << fi;
		EXPECT_EQ(0x5A, gen) << "frame " << fi;
		EXPECT_EQ(a.x, b.x) << "frame " << fi;
		EXPECT_EQ(a.y, b.y) << "frame " << fi;
		EXPECT_EQ(a.angle, b.angle) << "frame " << fi;
		ASSERT_EQ(a.count, b.count) << "frame " << fi;
		for (unsigned int i = 0; i < a.count; ++i)
		{
			const SnapPolyEntry& x = a.e[i];
			const SnapPolyEntry& y = b.e[i];
			EXPECT_EQ(x.kind, y.kind) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.gen, y.gen) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.speed, y.speed) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.dist, y.dist) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.angle, y.angle) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.direction, y.direction) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.xSpeed, y.xSpeed) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.ySpeed, y.ySpeed) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.xTarget, y.xTarget) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.yTarget, y.yTarget) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.totalDist, y.totalDist) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.tics, y.tics) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.waitTics, y.waitTics) << "frame " << fi << ", entry " << i;
			EXPECT_EQ(x.close, y.close) << "frame " << fi << ", entry " << i;
		}
		// [Kaori-Chani] One byte short must be refused, not read.
		EXPECT_EQ(0u, MoverPolyBodyBytes(fr, p + SNAPMV_HEADER_BYTES, bn - 1)) << "frame " << fi;
	}
}

// [Kaori-Chani] The skip path only a newer peer reaches, in both frames: a fixed type's width
// follows the frame, and a self-describing one's does not.
TEST(SnapshotFormat, EveryEventTypeReportsItsOwnLength)
{
	for (unsigned int fi = 0; fi < 2; ++fi)
	{
		const SnapFrame& fr = FORMATTEST_FRAMES[fi];
		BYTE ev[SNAP_MAX_EVENT_BYTES + 8];
		memset(ev, 0, sizeof(ev));
		for (unsigned int t = 0; t <= SNAPEV_MAX; ++t)
		{
			const BYTE type = (BYTE)t;
			const bool bSelf = EventSelfDescribing(type);
			EXPECT_EQ(t >= (unsigned int)SNAPEV_SELFDESC, bSelf)
				<< "frame " << fi << ", type " << t;

			unsigned int n = 0;
			bool bSkip = false;
			if (bSelf)
			{
				// Length byte then payload, and the reader must skip exactly that far without
				// knowing anything else about the type. Known types are handed over, unknown ones
				// skipped; both are SNAPEVLEN_OK over the same bytes, so the flag alone separates.
				const unsigned int len = 1 + (t % 7);
				const bool bWantSkip = (EventSelfDescKnown(type) == false);
				ev[0] = (BYTE)len;
				EXPECT_EQ(SNAPEVLEN_OK, EventRecordBytes(fr, type, ev, 1 + len, n, bSkip))
					<< "frame " << fi << ", type " << t;
				EXPECT_EQ(1 + len, n) << "frame " << fi << ", type " << t;
				EXPECT_EQ(bWantSkip, bSkip) << "frame " << fi << ", type " << t;

				// A zero length would collide with the unknown sentinel.
				ev[0] = 0;
				EXPECT_EQ(SNAPEVLEN_BAD, EventRecordBytes(fr, type, ev, 8, n, bSkip))
					<< "frame " << fi << ", type " << t << " accepted a zero length";
				ev[0] = (BYTE)len;
				for (unsigned int avail = 0; avail <= len; ++avail)
				{
					ASSERT_EQ(SNAPEVLEN_BAD, EventRecordBytes(fr, type, ev, avail, n, bSkip))
						<< "frame " << fi << ", type " << t << " accepted " << avail
						<< " of " << (1 + len) << " bytes";
				}
				continue;
			}

			// Fixed range: a known type reports its own width, an unknown one stops the parse
			// rather than being skipped.
			const unsigned int fixed = EventBytes(fr, type);
			const int r = EventRecordBytes(fr, type, ev, sizeof(ev), n, bSkip);
			if (fixed == 0)
			{
				EXPECT_EQ(SNAPEVLEN_UNKNOWN, r)
					<< "frame " << fi << ", unknown fixed type " << t << " did not stop the parse";
				continue;
			}
			EXPECT_EQ(SNAPEVLEN_OK, r) << "frame " << fi << ", type " << t;
			EXPECT_EQ(fixed, n) << "frame " << fi << ", type " << t;
			EXPECT_FALSE(bSkip) << "frame " << fi << ", type " << t;
			for (unsigned int avail = 0; avail < fixed; ++avail)
			{
				ASSERT_EQ(SNAPEVLEN_BAD, EventRecordBytes(fr, type, ev, avail, n, bSkip))
					<< "frame " << fi << ", type " << t << " accepted " << avail
					<< " of " << fixed << " bytes";
			}
		}
	}
}

// [Kaori-Chani] A zeroed state takes the narrow APPEAR; an unresolvable one is forced wide.
TEST(SnapshotFormat, AppearChoosesItsWidthFromTheState)
{
	SnapEntityState narrowState, wideState;
	memset(&narrowState, 0, sizeof(narrowState));
	memset(&wideState, 0, sizeof(wideState));
	wideState.qstate = SNAP_STATE_UNKNOWN;	// depth 7: forced wide
	wideState.qtics = 255;					// the no-duration sentinel, also wide
	EXPECT_TRUE(AppearIsNarrow(narrowState));
	EXPECT_FALSE(AppearIsNarrow(wideState));
}

// [Kaori-Chani] The three group lengths agree while every group is fixed, and the reader's helper
// refuses every truncation; the values survive the width the writer chose, sentinels included.
TEST(SnapshotFormat, TheThreeGroupLengthsAgreeOverEveryMask)
{
	SnapEntityState states[2];
	memset(&states[0], 0, sizeof(states[0]));
	memset(&states[1], 0, sizeof(states[1]));
	states[1].qstate = SNAP_STATE_UNKNOWN;
	states[1].qtics = 255;

	for (unsigned int s = 0; s < 4; ++s)
	{
		const SnapEntityState& g = states[s & 1];
		const SnapFrame& fr = FORMATTEST_FRAMES[s >> 1];
		BYTE gbuf[SNAP_MAX_RECORD_BYTES];
		for (unsigned int m = 0; m <= SNAPGRP_GROUPMASK; ++m)
		{
			const BYTE mask = (BYTE)(m & SNAPGRP_GROUPMASK);
			if (mask != (BYTE)m)
				continue;
			memset(gbuf, 0, sizeof(gbuf));
			const unsigned int written = WriteGroups(gbuf, fr, g, mask);
			const unsigned int wide = GroupBytes(fr, mask);
			const unsigned int actual = GroupBytesFor(fr, mask, g);
			EXPECT_EQ(actual, written) << "state " << s << ", mask " << (unsigned int)mask;
			EXPECT_LE(actual, wide) << "state " << s << ", mask " << (unsigned int)mask;

			unsigned int need = 0;
			EXPECT_TRUE(GroupBytesRead(fr, mask, gbuf, wide, need))
				<< "state " << s << ", mask " << (unsigned int)mask;
			EXPECT_EQ(actual, need) << "state " << s << ", mask " << (unsigned int)mask;

			for (unsigned int avail = 0; avail < actual; ++avail)
			{
				unsigned int bad = 0;
				ASSERT_FALSE(GroupBytesRead(fr, mask, gbuf, avail, bad))
					<< "state " << s << ", mask " << (unsigned int)mask << " accepted "
					<< avail << " of " << actual << " bytes";
			}

			if (mask & SNAPGRP_APPEAR)
			{
				SnapEntityState back;
				memset(&back, 0, sizeof(back));
				ReadGroups(gbuf, fr, back, mask);
				EXPECT_EQ(g.qtics, back.qtics) << "state " << s << ", mask " << (unsigned int)mask;
				EXPECT_EQ(g.qstate, back.qstate) << "state " << s << ", mask " << (unsigned int)mask;
			}
		}
	}
}

// [Kaori-Chani] The record size constants agree with GroupBytes in the narrow frame and the
// widest, as does the budget's per-frame worst case.
TEST(SnapshotFormat, TheRecordSizeConstantsAgreeWithTheFrames)
{
	for (unsigned int fi = 0; fi < 2; ++fi)
	{
		const unsigned int worst = SNAP_IDGAP_MAXBYTES + 1 + 2 + 2
			+ GroupBytes(FORMATTEST_FRAMES[fi], SNAPGRP_ALL);
		EXPECT_LE(worst, (unsigned int)SNAP_MAX_RECORD_BYTES) << "frame " << fi;
		EXPECT_EQ(RecordBytesMax(FORMATTEST_FRAMES[fi]), worst) << "frame " << fi;
	}
	EXPECT_EQ((unsigned int)SNAP_MAX_RECORD_BYTES_NARROW, RecordBytesMax(FORMATTEST_NARROW));
	EXPECT_EQ((unsigned int)SNAP_MAX_RECORD_BYTES, RecordBytesMax(FrameWidest()));
	EXPECT_LE(SNAP_EVENT_HEADER_MAX + EventBytes(FrameWidest(), SNAPEV_BRANCH),
		(unsigned int)SNAP_MAX_EVENT_BYTES);
}

// [Kaori-Chani] An enter with a target: the bit, the field and the length.
TEST(SnapshotFormat, AnEnterCarriesItsTargetInTwoMoreBytes)
{
	SnapEntityState z;
	memset(&z, 0, sizeof(z));
	BYTE eb[SNAP_MAX_RECORD_BYTES];
	const unsigned int plain = WriteRecordBody(eb, FORMATTEST_NARROW,
		(BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1234, z);
	const unsigned int withT = WriteRecordBody(eb, FORMATTEST_NARROW,
		(BYTE)(SNAPGRP_ALL | SNAPGRP_ENTER), 1234, z, NULL, 77);
	const WORD cls = Read16(eb + 1);
	EXPECT_EQ(plain + 2, withT);
	EXPECT_NE(0, cls & SNAP_CLASS_TARGET);
	EXPECT_EQ(1234, cls & SNAP_CLASS_MASK);
	EXPECT_EQ(77, Read16(eb + 3));
}

// [Kaori-Chani] The event codec end to end: byte counts, what the writer touched against what it
// reported, and the values against what went in.
TEST(SnapshotFormat, TheEventCodecRoundTripsAndTouchesNothingPastItsLength)
{
	const BYTE canary = 0xAA;
	BYTE buf[SNAP_MAX_EVENT_BYTES + 8];
	memset(buf, canary, sizeof(buf));

	SnapEntityState e;
	memset(&e, 0, sizeof(e));
	e.qstate = 0x1234;
	e.qtics = 7;
	e.qx = -1000;
	e.qy = 2000;
	e.qz = 40;
	const unsigned int evTic = 0xBEEF;
	const unsigned int n = WriteEventBody(buf, FORMATTEST_NARROW, SNAPEV_BRANCH, e, evTic);
	EXPECT_EQ(SNAP_EVENT_MASK_BYTES + EventBytes(FORMATTEST_NARROW, SNAPEV_BRANCH), n);
	ASSERT_LE(n, (unsigned int)SNAP_MAX_EVENT_BYTES);

	for (unsigned int i = n; i < (unsigned int)sizeof(buf); ++i)
		ASSERT_EQ(canary, buf[i]) << "byte " << i << " past the reported length " << n;

	EXPECT_NE(0, buf[0] & SNAPGRP_EVENT) << "the body's byte 0 announces EVENT";
	EXPECT_EQ(SNAPEV_BRANCH, buf[0] & SNAPEV_TYPEMASK);

	SnapEntityState r;
	memset(&r, 0, sizeof(r));
	const WORD ticBack = ReadEvent(buf + SNAP_EVENT_MASK_BYTES, FORMATTEST_NARROW,
		SNAPEV_BRANCH, r);
	EXPECT_EQ((WORD)evTic, ticBack);
	EXPECT_EQ(e.qstate, r.qstate);
	EXPECT_EQ(e.qtics, r.qtics);
	EXPECT_EQ(e.qx, r.qx);
	EXPECT_EQ(e.qy, r.qy);
	EXPECT_EQ(e.qz, r.qz);
}

// [Kaori-Chani] Both forms, a gap of each width, and the backwards step of the unsorted prefix,
// which decodes only because the gap is taken mod 2^16.
TEST(SnapshotFormat, TheEntityIdCodecRoundTrips)
{
	static const WORD ids[6] = { 1, 2, 900, 1000, 65530, 3 };
	BYTE idbuf[6 * SNAP_IDGAP_MAXBYTES];
	SnapIdCursor w;
	IdCursorReset(w);
	unsigned int off = 0;
	for (unsigned int i = 0; i < 6; ++i)
	{
		const unsigned int want = RecordIdBytes(ids[i], w);
		const unsigned int got = WriteRecordId(idbuf + off, ids[i], w);
		// The layout pass sizes a chunk from RecordIdBytes, so a disagreement is a chunk
		// overrunning its datagram.
		EXPECT_EQ(want, got) << "id " << ids[i];
		off += got;
	}

	SnapIdCursor r;
	IdCursorReset(r);
	unsigned int ro = 0;
	for (unsigned int i = 0; i < 6; ++i)
	{
		WORD back = 0;
		const unsigned int used = ReadRecordId(idbuf + ro, off - ro, back, r);
		ASSERT_NE(0u, used) << "id " << ids[i];
		ASSERT_EQ(ids[i], back) << "id " << ids[i];
		ro += used;
	}
}

// [Kaori-Chani] A gap cut short by the payload's end is refused, as is a fourth continuation byte.
TEST(SnapshotFormat, AnOverlongOrTruncatedIdGapIsRefused)
{
	BYTE bad[8];
	memset(bad, 0xFF, sizeof(bad));
	unsigned int gap = 123;
	EXPECT_EQ(0u, ReadIdGap(bad, sizeof(bad), gap)) << "an overlong gap";

	const unsigned int wide = PutIdGap(bad, 40000);
	for (unsigned int avail = 0; avail < wide; ++avail)
		ASSERT_EQ(0u, ReadIdGap(bad, avail, gap)) << avail << " of " << wide << " bytes";
}

// [Kaori-Chani] The local block through the same three checks, every fixed_t unchanged, with
// values in each field's sign bit and top byte.
TEST(SnapshotFormat, TheLocalBlockRoundTripsEveryField)
{
	const BYTE canary = 0x5C;
	BYTE buf[SNAP_MAX_EVENT_BYTES + 8];
	memset(buf, canary, sizeof(buf));

	SnapLocalState s;
	s.clientTic = 0xDEADBEEF;
	s.x = (fixed_t)0x7FFFFFFF;
	s.y = fixed_t::FromSignedBits(0x80000000u);
	s.z = -1;
	s.velx = (fixed_t)0x01020304;
	s.vely = (fixed_t)-0x01020304;
	s.velz = 0;
	s.jumpTics = -2;

	const unsigned int n = WriteLocalBody(buf, FORMATTEST_NARROW, s);
	EXPECT_EQ(SNAP_EVENT_MASK_BYTES + EventBytes(FORMATTEST_NARROW, SNAPEV_LOCAL), n);
	ASSERT_LE(n, (unsigned int)SNAP_MAX_EVENT_BYTES);
	for (unsigned int i = n; i < (unsigned int)sizeof(buf); ++i)
		ASSERT_EQ(canary, buf[i]) << "byte " << i << " past the reported length " << n;

	EXPECT_NE(0, buf[0] & SNAPGRP_EVENT);
	EXPECT_EQ(SNAPEV_LOCAL, buf[0] & SNAPEV_TYPEMASK);

	SnapLocalState b;
	memset(&b, 0, sizeof(b));
	ReadLocal(buf + SNAP_EVENT_MASK_BYTES, FORMATTEST_NARROW, b);
	EXPECT_EQ(s.clientTic, b.clientTic);
	EXPECT_EQ(s.x, b.x);
	EXPECT_EQ(s.y, b.y);
	EXPECT_EQ(s.z, b.z);
	EXPECT_EQ(s.velx, b.velx);
	EXPECT_EQ(s.vely, b.vely);
	EXPECT_EQ(s.velz, b.velz);
	EXPECT_EQ(s.jumpTics, b.jumpTics);
}

// [Kaori-Chani] Angle truncates: reconstruction error under one step (2^20 BAM).
TEST(SnapshotFormat, AngleTruncatesWithinOneStep)
{
	DWORD seed = FORMATTEST_SEED;
	int bad = 0;
	for (int i = 0; i < FORMATTEST_SAMPLES; ++i)
	{
		const angle_t a = (angle_t)formattest_Next(seed);
		if (a - DequantAngle(QuantAngle(a)) >= (1u << 20))
			++bad;
	}
	EXPECT_EQ(0, bad) << bad << " of " << FORMATTEST_SAMPLES << " samples (seed 0x"
		<< std::hex << (unsigned int)FORMATTEST_SEED << ")";
}

// [Kaori-Chani] Full range, which is mostly the clamp, then the unclamped range alone (plus or
// minus 8191 steps inclusive), so the rounding is exercised too.
TEST(SnapshotFormat, VelocityRoundsWithinHalfAStepOverTheSweep)
{
	DWORD seed = FORMATTEST_SEED;
	int bad = 0;
	const DWORD span = ((DWORD)8191 << 14) * 2 + 1;
	for (int i = 0; i < FORMATTEST_SAMPLES; ++i)
	{
		if (formattest_VelBad(formattest_Sample(seed)))
			++bad;
		if (formattest_VelBad((fixed_t)(long long)(formattest_Next(seed) % span)
			- (fixed_t)(8191 << 14)))
			++bad;
	}
	EXPECT_EQ(0, bad) << bad << " of " << (2 * FORMATTEST_SAMPLES) << " samples (seed 0x"
		<< std::hex << (unsigned int)FORMATTEST_SEED << ")";
}

TEST(SnapshotFormat, VelocityRoundsCorrectlyNearZero)
{
	EXPECT_EQ(0, QuantVel(-1));
	EXPECT_EQ(0, QuantVel(1));
	EXPECT_EQ(0, QuantVel(-(1 << 13)));
	EXPECT_EQ(1, QuantVel(1 << 13));
}

// [Kaori-Chani] The cosmetic codec's exact bytes, which pin the wire rather than only its
// round trip: 8.5 quarters rounds to 9 and -4.5 to -4, and a damage of 300 clamps to 255.
TEST(SnapshotFormat, TheCosmeticCodecWritesItsExactBytes)
{
	BYTE buf[40];
	bool bBig, bSmall;
	SnapCosGeneric g;
	memset(&g, 0, sizeof(g));
	g.classId = 0x1234;
	g.x = (5 << FRACBITS) + 123;
	g.y = -(3 << FRACBITS);
	g.z = 100 << FRACBITS;
	g.angle = 0x80000000u;
	g.vel[0] = 2 * FRACUNIT + FRACUNIT / 8;
	g.vel[1] = -FRACUNIT - FRACUNIT / 8;
	g.stateCode = -1;
	static const BYTE gWant[] = { 0x00, 0x34, 0x12, 0x05, 0x00, 0xFD, 0xFF, 0x64, 0x00,
		0x80, 0x09, 0xFC, 0x00 };
	EXPECT_EQ(sizeof(gWant),
		CosWriteGeneric(buf, FORMATTEST_NARROW, SNAP_COSE_COPY, g, bBig, bSmall));
	EXPECT_EQ(0, memcmp(buf, gWant, sizeof(gWant)));
	EXPECT_FALSE(bBig);
	EXPECT_FALSE(bSmall);

	SnapCosBlood b;
	b.x = 10 << FRACBITS;
	b.y = -FRACUNIT;
	b.z = 0;
	b.dir = 0x40000000u;
	b.damage = 300;						// clamps to 255
	b.originator = 0x0102;
	static const BYTE bWant[] = { 0x22, 0x0A, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x40, 0xFF,
		0x02, 0x01 };
	EXPECT_EQ(sizeof(bWant), CosWriteBlood(buf, FORMATTEST_NARROW,
		SNAP_COSE_BLOOD | SNAP_COSE_BLOODPLAIN, b));
	EXPECT_EQ(0, memcmp(buf, bWant, sizeof(bWant)));

	// [Kaori-Chani] A splatter carries no direction or damage.
	EXPECT_EQ(9u, CosWriteBlood(buf, FORMATTEST_NARROW,
		SNAP_COSE_SPLATTER | SNAP_COSE_SPLATTER2, b));

	SnapCosBlood rb;
	SnapCosCursor bcur = { bWant + 1, bWant + sizeof(bWant) };
	CosReadBlood(bcur, FORMATTEST_NARROW, bWant[0], rb);
	EXPECT_EQ(bcur.end, bcur.p);
	EXPECT_EQ(b.x, rb.x);
	EXPECT_EQ(b.y, rb.y);
	EXPECT_EQ(b.dir, rb.dir);
	EXPECT_EQ(255, rb.damage);
	EXPECT_EQ(0x0102, rb.originator);
}

// [Kaori-Chani] The longest body, 31 bytes: fine velocity (one axis under a unit, one past 32), a
// state and all four extras.
TEST(SnapshotFormat, TheCosmeticCodecRoundTripsItsLongestBody)
{
	BYTE buf[40];
	bool bBig, bSmall;
	SnapCosGeneric g;
	memset(&g, 0, sizeof(g));
	g.classId = 0x1234;
	g.x = (5 << FRACBITS) + 123;
	g.y = -(3 << FRACBITS);
	g.z = 100 << FRACBITS;
	g.angle = 0x80000000u;
	g.vel[0] = FRACUNIT / 2;
	g.vel[1] = -40 * FRACUNIT;
	g.vel[2] = 100 * FRACUNIT;
	g.stateCode = 0x3005;
	g.extras = SNAP_COSX_ACTED | SNAP_COSX_TRANSLATION | SNAP_COSX_ALPHA | SNAP_COSX_SCALE
		| SNAP_COSX_STYLE;
	g.transType = 3;
	g.transIndex = 0x0102;
	g.alpha = FRACUNIT / 2;
	g.scaleX = FRACUNIT * 3 / 2;
	g.scaleY = FRACUNIT / 4;
	g.style = 0xDEADBEEFu;

	const unsigned int len = CosWriteGeneric(buf, FORMATTEST_NARROW, SNAP_COSE_PLAIN, g,
		bBig, bSmall);
	SnapCosGeneric r;
	SnapCosCursor cur = { buf + 1, buf + len };
	CosReadGeneric(cur, FORMATTEST_NARROW, buf[0], r);
	EXPECT_EQ(31u, len);
	EXPECT_EQ(buf + len, cur.p);
	EXPECT_TRUE(bBig);
	EXPECT_TRUE(bSmall);
	EXPECT_EQ(SNAP_COSE_PLAIN, buf[0] & SNAP_COSE_KIND_MASK);
	EXPECT_EQ(0x1234, r.classId);
	EXPECT_EQ(5 << FRACBITS, r.x);
	EXPECT_EQ(g.y, r.y);
	EXPECT_EQ(g.z, r.z);
	EXPECT_EQ(g.angle, r.angle);
	EXPECT_EQ(g.vel[0], r.vel[0]);
	EXPECT_EQ(g.vel[1], r.vel[1]);
	EXPECT_EQ(g.vel[2], r.vel[2]);
	EXPECT_EQ(0x3005, r.stateCode);
	EXPECT_EQ(g.extras, r.extras);
	EXPECT_EQ(3, r.transType);
	EXPECT_EQ(0x0102, r.transIndex);
	EXPECT_LE(abs(r.alpha - g.alpha), 257);
	EXPECT_EQ(g.scaleX, r.scaleX);
	EXPECT_EQ(g.scaleY, r.scaleY);
	EXPECT_EQ(g.style, r.style);

	// [Kaori-Chani] One byte short: the read runs past the end, which is how the client finds a
	// truncated event.
	SnapCosCursor cut = { buf + 1, buf + len - 1 };
	CosReadGeneric(cut, FORMATTEST_NARROW, buf[0], r);
	EXPECT_GT(cut.p, cut.end);
}

// [Kaori-Chani] Alpha, byte 22, clamps to [0, FRACUNIT] before it scales, so 200.0 is 255 and
// below zero is 0.
TEST(SnapshotFormat, TheCosmeticCodecClampsAlphaBeforeItScales)
{
	BYTE buf[40];
	bool bBig, bSmall;
	SnapCosGeneric g;
	memset(&g, 0, sizeof(g));
	g.stateCode = 0x3005;
	g.extras = SNAP_COSX_ACTED | SNAP_COSX_TRANSLATION | SNAP_COSX_ALPHA | SNAP_COSX_SCALE
		| SNAP_COSX_STYLE;
	g.vel[1] = -40 * FRACUNIT;

	g.alpha = 200 * FRACUNIT;
	CosWriteGeneric(buf, FORMATTEST_NARROW, SNAP_COSE_PLAIN, g, bBig, bSmall);
	EXPECT_EQ(255, buf[22]);

	g.alpha = -FRACUNIT;
	CosWriteGeneric(buf, FORMATTEST_NARROW, SNAP_COSE_PLAIN, g, bBig, bSmall);
	EXPECT_EQ(0, buf[22]);
}

//*****************************************************************************
//	THE ENTRY POINTS THE LAYERS ABOVE CALL
//
// These are called by the server, the client and the recorder rather than by
// anything in the format layer, so nothing above reaches them yet. They are
// pure, so they are tested here instead of waiting for their callers.

TEST(SnapshotFormat, TheZsnapHeaderCarriesTheFrameFromVersion33)
{
	EXPECT_EQ((unsigned int)ZSNAP_HEADER_SIZE, ZsnapHeaderBytes(32));
	EXPECT_EQ((unsigned int)(ZSNAP_HEADER_SIZE + ZSNAP_HEADER_FRAME), ZsnapHeaderBytes(33));
	EXPECT_EQ((unsigned int)(ZSNAP_HEADER_SIZE + ZSNAP_HEADER_FRAME),
		ZsnapHeaderBytes(ZSNAP_VERSION));
}

// [Kaori-Chani] Each field names its own group, and a pitch moving inside one wire step names
// none: comparing all ten bits would dirty ANG forever, since the reference holds it truncated.
TEST(SnapshotFormat, DiffMaskNamesTheGroupEachFieldBelongsTo)
{
	SnapEntityState a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	EXPECT_EQ(0, DiffMask(a, b)) << "two identical states differ in nothing";

	b = a; b.qx = 1;          EXPECT_EQ(SNAPGRP_POS, DiffMask(a, b)) << "qx";
	b = a; b.qy = 1;          EXPECT_EQ(SNAPGRP_POS, DiffMask(a, b)) << "qy";
	b = a; b.qz = 1;          EXPECT_EQ(SNAPGRP_POSZ, DiffMask(a, b)) << "qz";
	b = a; b.qangle = 1;      EXPECT_EQ(SNAPGRP_ANG, DiffMask(a, b)) << "qangle";
	b = a; b.qvx = 1;         EXPECT_EQ(SNAPGRP_VELMISC, DiffMask(a, b)) << "qvx";
	b = a; b.qvy = 1;         EXPECT_EQ(SNAPGRP_VELMISC, DiffMask(a, b)) << "qvy";
	b = a; b.qvz = 1;         EXPECT_EQ(SNAPGRP_VELMISC, DiffMask(a, b)) << "qvz";
	b = a; b.qstate = 1;      EXPECT_EQ(SNAPGRP_APPEAR, DiffMask(a, b)) << "qstate";
	b = a; b.renderstate = 1; EXPECT_EQ(SNAPGRP_RENDER, DiffMask(a, b)) << "renderstate";
	b = a; b.misc = 1;        EXPECT_EQ(SNAPGRP_VELMISC, DiffMask(a, b)) << "misc";
	b = a; b.effects = 1;     EXPECT_EQ(SNAPGRP_VELMISC, DiffMask(a, b)) << "effects";

	b = a; b.qpitch = 1;
	ASSERT_EQ(WirePitch(a.qpitch), WirePitch(b.qpitch)) << "the two must share one wire step";
	EXPECT_EQ(0, DiffMask(a, b)) << "a pitch inside one wire step is not a change";
	b.qpitch = 4;
	ASSERT_NE(WirePitch(a.qpitch), WirePitch(b.qpitch));
	EXPECT_EQ(SNAPGRP_ANG, DiffMask(a, b)) << "a pitch across a wire step is";
}

// [Kaori-Chani] Overlaid through the codec, so only the groups named move, and VELMISC's round
// trip leaves nothing in misc but the checked bit a captured state never holds.
TEST(SnapshotFormat, MergeGroupsOverlaysOnlyTheGroupsNamed)
{
	const SnapFrame f = FrameNarrow();
	SnapEntityState src, dst;
	memset(&src, 0, sizeof(src));
	memset(&dst, 0, sizeof(dst));
	src.qx = 120; src.qy = -34; src.qz = 9;
	src.qangle = 0x0ABC; src.qpitch = 0x154;
	src.qvx = 5; src.qvy = -6; src.qvz = 7;
	src.qstate = 0x0333; src.qtics = 4;
	src.renderstate = 0xDEADBEEF;
	src.effects = 3;
	dst.qz = 99;
	dst.qangle = 0x7000;
	dst.renderstate = 1;

	MergeGroups(dst, f, src, (BYTE)SNAPGRP_POS);
	EXPECT_EQ(src.qx, dst.qx);
	EXPECT_EQ(src.qy, dst.qy);
	EXPECT_EQ(99, dst.qz) << "POSZ was not named";
	EXPECT_EQ(0x7000, dst.qangle) << "ANG was not named";
	EXPECT_EQ(1u, dst.renderstate) << "RENDER was not named";

	MergeGroups(dst, f, src, (BYTE)(SNAPGRP_POSZ | SNAPGRP_RENDER));
	EXPECT_EQ(src.qz, dst.qz);
	EXPECT_EQ(src.renderstate, dst.renderstate);

	dst.misc = 0xFFFF;
	MergeGroups(dst, f, src, (BYTE)SNAPGRP_VELMISC);
	EXPECT_EQ(src.qvx, dst.qvx);
	EXPECT_EQ(src.qvy, dst.qvy);
	EXPECT_EQ(src.qvz, dst.qvz);
	EXPECT_EQ(0, (WORD)(dst.misc & ~SNAP_MISC_CHECKED)) << "only the checked bit survives";
}

TEST(SnapshotFormat, ASpriteAndItsFrameShareOneWord)
{
	EXPECT_EQ((WORD)((7 << 5) | 3), PackSpriteFrame(7, 3));
	EXPECT_EQ((WORD)((2047 << 5) | 31), PackSpriteFrame(2047, 31));
	EXPECT_EQ((WORD)((2047 << 5) | 0), PackSpriteFrame(5000, 32))
		<< "the sprite clamps at 2047 and the frame takes five bits";
}

// [Kaori-Chani] Up is negative in BAM, which is the sign the field must keep: this is the pair
// the widening would have broken, zero-extending a shot into the floor.
TEST(SnapshotFormat, PitchTakesTenBitsAndComesBackSigned)
{
	const fixed_t up = fixed_t::FromSignedBits(0xC0000000u);		// a quarter turn above level
	const WORD q = QuantPitch(up);
	EXPECT_LT(DequantPitch(q), 0) << "a pitch above level stays negative";
	EXPECT_EQ(q, QuantPitch(DequantPitch(q))) << "the field round-trips";
	EXPECT_EQ(0, QuantPitch(0));
	EXPECT_EQ(0, DequantPitch(0));
}

// [Kaori-Chani] The render state packs a translation, a style and an alpha into one DWORD; alpha
// is eight bits, so it comes back within a step.
TEST(SnapshotFormat, TheRenderStatePacksAndUnpacks)
{
	static const DWORD translations[] = { 0u, 0x0123u, (DWORD)SNAP_RS_TRANS_MASK };
	for (unsigned int i = 0; i < sizeof(translations) / sizeof(translations[0]); ++i)
	{
		const DWORD rs = PackRenderState(translations[i], 5, FRACUNIT / 2);
		EXPECT_EQ(translations[i], TranslationFromState(rs)) << "translation " << i;
		EXPECT_EQ(5, StyleIndexFromState(rs)) << "translation " << i;
		// [Kaori-Chani] Alpha rides in six bits as 1/63rds, so it comes back on its own step
		// rather than exactly: that step is what this pins, not an invented tolerance.
		EXPECT_EQ(DequantAlpha(QuantAlpha(FRACUNIT / 2)), AlphaFromState(rs))
			<< "translation " << i;
	}
	// [Kaori-Chani] A translation type rides above the index and survives with it.
	const DWORD typed = (DWORD)(((DWORD)1 << SNAP_RS_TRANS_SHIFT) | 0x0045u);
	EXPECT_EQ(typed, TranslationFromState(PackRenderState(typed, 0, 0)));
}

TEST(SnapshotFormat, EveryStrideOfIdsIsExemptFromAppear)
{
	EXPECT_TRUE(AppearExempt(0));
	EXPECT_TRUE(AppearExempt(SNAP_APPEAR_EXEMPT_STRIDE));
	EXPECT_TRUE(AppearExempt(2 * SNAP_APPEAR_EXEMPT_STRIDE));
	EXPECT_FALSE(AppearExempt(1));
	EXPECT_FALSE(AppearExempt(SNAP_APPEAR_EXEMPT_STRIDE - 1));
}

// [Kaori-Chani] A width read from a file or a command is refused unless this build encodes it.
TEST(SnapshotFormat, OnlyTheTwoEncodedWidthsAreKnown)
{
	EXPECT_TRUE(FrameWidthKnown(SNAP_FRAME_NARROW));
	EXPECT_TRUE(FrameWidthKnown(SNAP_FRAME_WIDE));
	EXPECT_FALSE(FrameWidthKnown(0));
	EXPECT_FALSE(FrameWidthKnown(1));
	EXPECT_FALSE(FrameWidthKnown(SNAP_FRAME_WIDE + 1));
}

// [Kaori-Chani] Four bits from half a unit up: standing reads 0 and fully crouched 0xF, and a
// value one step off standing cannot read as standing.
TEST(SnapshotFormat, CrouchTakesFourBitsFromHalfAUnitUp)
{
	const int fu = 65536, half = fu / 2;
	EXPECT_EQ(0, QuantCrouch(fu, fu)) << "standing";
	EXPECT_EQ(0, QuantCrouch(fu + 1000, fu)) << "past standing";
	EXPECT_EQ(0xF, QuantCrouch(half, fu)) << "fully crouched";
	EXPECT_EQ(0xF, QuantCrouch(half - 1000, fu)) << "past fully crouched";
	EXPECT_EQ(fu, CrouchFromWire(0, fu));
	EXPECT_EQ(half, CrouchFromWire(0xF, fu));
	EXPECT_EQ(half, CrouchFromWire(0x10, fu)) << "past the field reads as fully crouched";

	const int step = (fu - half) / 15;
	EXPECT_NE(0, QuantCrouch(fu - step, fu)) << "one step off standing is not standing";
	for (WORD q = 1; q < 0xF; ++q)
		EXPECT_EQ(q, QuantCrouch(CrouchFromWire(q, fu), fu)) << "step " << q;
}

// [Kaori-Chani] The two bits are read together: ballistic alone is suppressible, steering alone
// steers, and a missile carrying both is neither.
TEST(SnapshotFormat, TheMissileBitsAreReadTogether)
{
	const WORD both = (WORD)(SNAP_MISC_BALLISTIC | SNAP_MISC_STEERING);
	EXPECT_TRUE(IsSuppressible(SNAP_MISC_BALLISTIC));
	EXPECT_FALSE(IsSuppressible(SNAP_MISC_STEERING));
	EXPECT_FALSE(IsSuppressible(both));
	EXPECT_FALSE(IsSuppressible(0));

	EXPECT_TRUE(IsSteering(SNAP_MISC_STEERING));
	EXPECT_FALSE(IsSteering(SNAP_MISC_BALLISTIC));
	EXPECT_FALSE(IsSteering(both));
	EXPECT_FALSE(IsSteering(0));
}

// [Kaori-Chani] Every class is tracked but the animated door, which the reader refuses.
TEST(SnapshotFormat, EveryMoverClassButTheAnimatedDoorIsTracked)
{
	for (unsigned int c = 0; c < SNAPMV_COUNT; ++c)
		EXPECT_EQ(c != (unsigned int)SNAPMV_ANIMDOOR, MoverClassTracked(c)) << "class " << c;
	EXPECT_FALSE(MoverClassTracked(SNAPMV_COUNT));
	EXPECT_FALSE(MoverClassTracked(255));
}

// [Kaori-Chani] A record's planes are its class's, less the ones its flags say it does not carry.
TEST(SnapshotFormat, ARecordsPlanesAreItsClassLessWhatItNarrows)
{
	for (BYTE c = 0; c < SNAPMV_COUNT; ++c)
	{
		const BYTE all = MoverPlanes(c);
		EXPECT_EQ(all, RecordPlanes(c, 0)) << "class " << (int)c;
		EXPECT_EQ((BYTE)(all & ~SNAPMV_PLANE_FLOOR), RecordPlanes(c, SNAPMVF_NOFLOOR))
			<< "class " << (int)c;
		EXPECT_EQ((BYTE)(all & ~SNAPMV_PLANE_CEILING), RecordPlanes(c, SNAPMVF_NOCEILING))
			<< "class " << (int)c;
		EXPECT_EQ((BYTE)(all & ~(SNAPMV_PLANE_FLOOR | SNAPMV_PLANE_CEILING)),
			RecordPlanes(c, (BYTE)(SNAPMVF_NOFLOOR | SNAPMVF_NOCEILING))) << "class " << (int)c;
	}
}

// [Kaori-Chani] One record is its id then its body, which is what the chunk layout sizes.
TEST(SnapshotFormat, ARecordIsItsIdThenItsBody)
{
	const SnapFrame f = FrameNarrow();
	SnapEntityState e, held;
	memset(&e, 0, sizeof(e));
	e.qx = 11; e.qy = -22; e.qz = 3; e.qstate = 7; e.qtics = 2;
	held = e;
	held.qx = 99;
	const BYTE mask = (BYTE)(SNAPGRP_POS | SNAPGRP_POSZ | SNAPGRP_APPEAR);

	BYTE whole[SNAP_MAX_RECORD_BYTES], parts[SNAP_MAX_RECORD_BYTES];
	SnapIdCursor cw, cp;
	IdCursorReset(cw);
	IdCursorReset(cp);
	const unsigned int n = WriteRecord(whole, f, 4242, cw, mask, 1234, e);
	unsigned int m = WriteRecordId(parts, 4242, cp);
	m += WriteRecordBody(parts + m, f, mask, 1234, e);
	EXPECT_EQ(m, n);
	EXPECT_EQ(0, memcmp(whole, parts, n));

	// [Kaori-Chani] The held state and the target id reach the body the same way.
	IdCursorReset(cw);
	IdCursorReset(cp);
	const unsigned int nh = WriteRecord(whole, f, 7, cw, (BYTE)(mask | SNAPGRP_ENTER), 1234, e,
		&held, 55);
	unsigned int mh = WriteRecordId(parts, 7, cp);
	mh += WriteRecordBody(parts + mh, f, (BYTE)(mask | SNAPGRP_ENTER), 1234, e, &held, 55);
	EXPECT_EQ(mh, nh);
	EXPECT_EQ(0, memcmp(whole, parts, nh));
}

// [Kaori-Chani] An axis whose extent is empty keeps the narrow frame: a map can hold nothing on
// one axis, and the sweep has no bounds there to widen from.
TEST(SnapshotFormat, AnAxisWithNoExtentKeepsItsNarrowFrame)
{
	const long long lo[3] = { 1, -60000, 0 }, hi[3] = { -1, 60000, 0 };
	bool bOver = true;
	const SnapFrame f = FrameFromExtent(lo, hi, -0x80000000LL, 0x7FFFFFFFLL, &bOver);
	EXPECT_FALSE(bOver);
	EXPECT_EQ(0, f.origin[0]);
	EXPECT_EQ(SNAP_FRAME_NARROW, f.width[0]) << "the empty axis";
	EXPECT_EQ(SNAP_FRAME_WIDE, f.width[1]) << "the axis that does have an extent";
}

// [Kaori-Chani] The record id's own two refusals, apart from the gap reader's: a first id that
// does not fit in what is left, and a gap the reader rejects. Each leaves the cursor as it was,
// so a payload cut short mid-record cannot shift every id after it.
TEST(SnapshotFormat, ATruncatedOrMalformedRecordIdIsRefused)
{
	BYTE buf[8];
	memset(buf, 0xFF, sizeof(buf));
	WORD id = 7;
	SnapIdCursor c;
	IdCursorReset(c);
	for (unsigned int avail = 0; avail < SNAP_REC_IDBYTES_RAW; ++avail)
	{
		ASSERT_EQ(0u, ReadRecordId(buf, avail, id, c)) << avail << " bytes for the first id";
		ASSERT_TRUE(c.first) << "the cursor stands where it was, at " << avail << " bytes";
	}

	// Past the first record an id is a gap, and an overlong one is refused here too.
	IdCursorReset(c);
	ASSERT_EQ((unsigned int)SNAP_REC_IDBYTES_RAW, ReadRecordId(buf, sizeof(buf), id, c));
	EXPECT_EQ(0u, ReadRecordId(buf, sizeof(buf), id, c)) << "an overlong gap";
}

// [Kaori-Chani] A leave is its mask alone: no class and no groups, so nothing past the mask is
// written for a reader to walk into.
TEST(SnapshotFormat, ALeaveIsItsMaskAlone)
{
	const SnapFrame f = FrameNarrow();
	SnapEntityState e;
	memset(&e, 0, sizeof(e));
	e.qx = 5; e.qy = 6;

	BYTE rec[SNAP_MAX_RECORD_BYTES];
	memset(rec, 0xAB, sizeof(rec));
	const unsigned int n = WriteRecordBody(rec, f, 0, 1234, e);
	EXPECT_EQ((unsigned int)SNAP_REC_MASK_BYTES, n);
	EXPECT_EQ(0, rec[0]);
	EXPECT_EQ(0xAB, rec[n]) << "nothing is written past the mask";
}

// [Kaori-Chani] The polyobject walk refuses before it reads: too little for the header, a count
// past the array it would fill, an entry with no room left, and a kind this build does not know.
TEST(SnapshotFormat, APolyobjectBodyIsRefusedBeforeItIsWalked)
{
	const SnapFrame f = FrameNarrow();
	const unsigned int header = PolyHeaderBytes(f);
	BYTE body[SNAPMV_BODY_MAX];
	memset(body, 0, sizeof(body));

	for (unsigned int avail = 0; avail < header; ++avail)
		ASSERT_EQ(0u, MoverPolyBodyBytes(f, body, avail)) << avail << " of " << header << " bytes";

	body[header - 1] = (BYTE)(SNAPPO_MAXENTRIES + 1);
	EXPECT_EQ(0u, MoverPolyBodyBytes(f, body, sizeof(body))) << "a count past the array";

	body[header - 1] = 1;
	EXPECT_EQ(0u, MoverPolyBodyBytes(f, body, header)) << "an entry with no room at all";

	body[header] = 6;	// no SNAPPO_ type is 6
	EXPECT_EQ(0u, PolyEntryBytes(f, 6)) << "a kind this build does not know";
	EXPECT_EQ(0u, MoverPolyBodyBytes(f, body, header + 1)) << "that kind inside the walk";

	SnapMoverPoly s;
	memset(&s, 0, sizeof(s));
	EXPECT_FALSE(ReadMoverPoly(body, f, header + 1, s)) << "a body the walk refuses";
}

// [Kaori-Chani] A sliding door and a swinging one share one entry shape, so the writer and the
// reader treat them as one arm: this pins the sliding half, which the round trip above does not.
TEST(SnapshotFormat, ASlidingPolyobjectDoorRoundTrips)
{
	const SnapFrame f = FrameNarrow();
	SnapMoverPoly a;
	memset(&a, 0, sizeof(a));
	a.x = 7 << FRACBITS; a.y = -(9 << FRACBITS); a.angle = 0x40000000u; a.count = 1;
	a.e[0].kind = SNAPPO_SLIDE; a.e[0].gen = 3;
	a.e[0].speed = 1 << FRACBITS; a.e[0].dist = 2;
	a.e[0].xSpeed = -4; a.e[0].ySpeed = 5; a.e[0].direction = -2;
	a.e[0].totalDist = 128 << FRACBITS; a.e[0].tics = 6;
	a.e[0].waitTics = 35; a.e[0].close = 1;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int bn = WriteMoverPoly(body, f, a);
	EXPECT_EQ(PolyHeaderBytes(f) + (unsigned int)SNAPPO_DOOR_BYTES, bn);
	EXPECT_EQ(bn, MoverPolyBodyBytes(f, body, bn));

	SnapMoverPoly b;
	memset(&b, 0, sizeof(b));
	ASSERT_TRUE(ReadMoverPoly(body, f, bn, b));
	ASSERT_EQ(1, (int)b.count);
	EXPECT_EQ(SNAPPO_SLIDE, b.e[0].kind);
	EXPECT_EQ(a.x, b.x);
	EXPECT_EQ(a.y, b.y);
	EXPECT_EQ(a.e[0].speed, b.e[0].speed);
	EXPECT_EQ(a.e[0].dist, b.e[0].dist);
	EXPECT_EQ(a.e[0].xSpeed, b.e[0].xSpeed);
	EXPECT_EQ(a.e[0].ySpeed, b.e[0].ySpeed);
	EXPECT_EQ(a.e[0].direction, b.e[0].direction);
	EXPECT_EQ(a.e[0].totalDist, b.e[0].totalDist);
	EXPECT_EQ(a.e[0].tics, b.e[0].tics);
	EXPECT_EQ(a.e[0].waitTics, b.e[0].waitTics);
	EXPECT_EQ(a.e[0].close, b.e[0].close);
}

// [Kaori-Chani] Alpha's two clamp ends: opaque must come back exactly opaque, since 1/63rds were
// chosen over a shift so that a fully opaque actor does not draw slightly translucent.
TEST(SnapshotFormat, AlphaClampsAtBothEndsAndOpaqueIsExact)
{
	EXPECT_EQ(63, QuantAlpha(FRACUNIT)) << "exactly opaque";
	EXPECT_EQ(63, QuantAlpha(FRACUNIT * 2)) << "past opaque clamps, it does not wrap";
	EXPECT_EQ(0, QuantAlpha(0));
	EXPECT_EQ(0, QuantAlpha(-FRACUNIT)) << "a negative alpha floors at clear";
	EXPECT_EQ(FRACUNIT, DequantAlpha(63)) << "opaque round-trips exactly";
	EXPECT_EQ(FRACUNIT, DequantAlpha(255)) << "a wire value past the six bits still reads opaque";
	EXPECT_EQ(0, DequantAlpha(0));
	EXPECT_EQ(63, QuantAlpha(DequantAlpha(63))) << "and back again";
}

// [Kaori-Chani] The sprite and its frame share one word, so the pair that reads them back is what
// makes the packing worth anything.
TEST(SnapshotFormat, ASpriteAndItsFrameComeBackApart)
{
	static const WORD sprites[] = { 0, 1, 1000, 2047 };
	static const BYTE frames[] = { 0, 1, 30, 31 };
	for (unsigned int i = 0; i < sizeof(sprites) / sizeof(sprites[0]); ++i)
	{
		for (unsigned int j = 0; j < sizeof(frames) / sizeof(frames[0]); ++j)
		{
			const WORD sf = PackSpriteFrame(sprites[i], frames[j]);
			EXPECT_EQ(sprites[i], SpriteFromPack(sf)) << i << ", " << j;
			EXPECT_EQ(frames[j], FrameFromPack(sf)) << i << ", " << j;
		}
	}
	// [Kaori-Chani] The clamped sprite reads back as the clamp, not as what was asked for.
	EXPECT_EQ(2047, SpriteFromPack(PackSpriteFrame(5000, 0)));
}

// [Kaori-Chani] The psprite event's body and its reader, which the client's weapon layers go
// through: the mask byte leads, and the reader is handed the payload past it.
TEST(SnapshotFormat, ThePSpriteBodyRoundTripsBothLayers)
{
	SnapPSpriteState a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0xEE, sizeof(b));
	a.weaponClass = 0xBEEF;
	a.qstate[0] = 1234;					a.qtics[0] = 7;
	a.qstate[1] = SNAP_STATE_UNKNOWN;	a.qtics[1] = 255;

	BYTE body[SNAP_MAX_EVENT_BYTES];
	memset(body, 0, sizeof(body));
	const unsigned int n = WritePSpriteBody(body, a);
	EXPECT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + 2 + (2 + 1) * 2), n);
	EXPECT_EQ((BYTE)(SNAPGRP_EVENT | SNAPEV_PSPRITE), body[0]) << "the mask leads the body";

	ReadPSprite(body + SNAP_EVENT_MASK_BYTES, b);
	EXPECT_EQ(a.weaponClass, b.weaponClass);
	for (unsigned int L = 0; L < 2; ++L)
	{
		EXPECT_EQ(a.qstate[L], b.qstate[L]) << "layer " << L;
		EXPECT_EQ(a.qtics[L], b.qtics[L]) << "layer " << L;
	}
}

// [Kaori-Chani] The property event: only what a script changed travels, so each flag is read
// back exactly as it was written and a flag left out stays the class default.
TEST(SnapshotFormat, APropsBodyCarriesOnlyTheFlagsItSets)
{
	static const BYTE combos[] = { 0, SNAP_PROPS_SCALE, SNAP_PROPS_ROLL, SNAP_PROPS_MATERIAL,
		SNAP_PROPS_SCALE | SNAP_PROPS_ROLL,
		SNAP_PROPS_SCALE | SNAP_PROPS_ROLL | SNAP_PROPS_MATERIAL };

	for (unsigned int i = 0; i < sizeof(combos); ++i)
	{
		SnapProps a, b;
		memset(&a, 0, sizeof(a));
		memset(&b, 0xEE, sizeof(b));
		a.flags = combos[i];
		a.scaleX = QuantScale(DequantScale(358));	// 1.398, a scale a script would set
		a.scaleY = 256;								// 1.0 exactly
		a.roll = 0x4000;							// a quarter turn in the top 16 bits
		a.matLen = 9;
		memcpy(a.material, "fua_glow1", 9);

		BYTE body[SNAP_MAX_EVENT_BYTES];
		memset(body, 0, sizeof(body));
		const unsigned int n = WritePropsBody(body, a);
		EXPECT_EQ((BYTE)(SNAPGRP_EVENT | SNAPEV_PROPS), body[0]) << "the mask leads the body";
		// The length byte counts everything after itself, and is never 0: a zero
		// length is EventRecordBytes' sentinel for a body it cannot trust.
		ASSERT_EQ((unsigned int)body[1], n - 2) << "combo " << (int)combos[i];
		EXPECT_GT((unsigned int)body[1], 0u) << "combo " << (int)combos[i];
		EXPECT_LE(n, (unsigned int)(SNAP_EVENT_MASK_BYTES + SNAP_EVBYTES_PROPS));

		ASSERT_TRUE(ReadProps(body + 2, body[1], b)) << "combo " << (int)combos[i];
		EXPECT_EQ(a.flags, b.flags);
		EXPECT_EQ((combos[i] & SNAP_PROPS_SCALE) ? a.scaleX : 0, b.scaleX);
		EXPECT_EQ((combos[i] & SNAP_PROPS_SCALE) ? a.scaleY : 0, b.scaleY);
		EXPECT_EQ((combos[i] & SNAP_PROPS_ROLL) ? a.roll : 0, b.roll);
		if (combos[i] & SNAP_PROPS_MATERIAL)
		{
			ASSERT_EQ(a.matLen, b.matLen);
			EXPECT_EQ(0, memcmp(a.material, b.material, a.matLen));
		}
		else
		{
			EXPECT_EQ(0, b.matLen);
		}
	}
}

// [Kaori-Chani] A uniform costs bytes only where its slot is set, and an unset row is never
// written, so it comes back as nothing rather than as stale values.
TEST(SnapshotFormat, APropsCarriesOnlyTheUniformSlotsThatAreSet)
{
	SnapProps a, b;
	memset(&a, 0, sizeof(a));
	a.flags = SNAP_PROPS_UNIFORMS;
	a.uniSet = 0xA5;					// slots 0, 2, 5 and 7
	for (unsigned int u = 0; u < SNAP_PROPS_UNIFORM_SLOTS; ++u)
		for (unsigned int k = 0; k < 4; ++k)
			a.uni[u][k] = (float)(u + 1) + (float)k / 8.f;

	BYTE body[SNAP_MAX_EVENT_BYTES];
	const unsigned int n = WritePropsBody(body, a);
	EXPECT_EQ(4u, PropsUniformCount(a.uniSet));
	ASSERT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + 1 + 1 + 1 + 4 * 16), n);

	memset(&b, 0xEE, sizeof(b));
	ASSERT_TRUE(ReadProps(body + 2, body[1], b));
	EXPECT_EQ(a.uniSet, b.uniSet);
	for (unsigned int u = 0; u < SNAP_PROPS_UNIFORM_SLOTS; ++u)
		for (unsigned int k = 0; k < 4; ++k)
			EXPECT_EQ((a.uniSet & (1u << u)) ? a.uni[u][k] : 0.f, b.uni[u][k])
				<< "slot " << u << " component " << k;

	// A float crosses as its bits: the value read back is the one written, not a
	// rounding of it.
	EXPECT_EQ(a.uni[0][1], b.uni[0][1]);

	// Every flag at once, the widest body there is, which is what the envelope is
	// sized for and what a buffer must hold.
	a.flags = SNAP_PROPS_SCALE | SNAP_PROPS_ROLL | SNAP_PROPS_MATERIAL | SNAP_PROPS_UNIFORMS;
	a.uniSet = 0xFF;
	a.matLen = SNAP_PROPS_MATNAME_MAX;
	memset(a.material, 'm', SNAP_PROPS_MATNAME_MAX);
	const unsigned int widest = WritePropsBody(body, a);
	EXPECT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + SNAP_EVBYTES_PROPS), widest);
	EXPECT_LE(SNAP_EVBYTES_PROPS, SNAP_EVBYTES_MAX) << "the envelope sizes every event buffer";
	ASSERT_TRUE(ReadProps(body + 2, body[1], b));
	EXPECT_EQ(0xFF, b.uniSet);
	EXPECT_EQ(SNAP_PROPS_MATNAME_MAX, b.matLen);
}

// [Kaori-Chani] A body whose mask claims rows it does not hold is refused, which is what a
// mangled packet or a bad sender produces.
TEST(SnapshotFormat, APropsBodyShortOfItsUniformRowsIsRefused)
{
	SnapProps a, b;
	memset(&a, 0, sizeof(a));
	a.flags = SNAP_PROPS_UNIFORMS;
	a.uniSet = 0x03;					// two slots, 32 bytes of rows
	BYTE body[SNAP_MAX_EVENT_BYTES];
	const unsigned int n = WritePropsBody(body, a);
	const unsigned int len = body[1];
	EXPECT_EQ(n, len + 2);
	for (unsigned int cut = 0; cut < len; ++cut)
		EXPECT_FALSE(ReadProps(body + 2, cut, b)) << "cut at " << cut;
	EXPECT_TRUE(ReadProps(body + 2, len, b));

	// The whole body, with the mask raised to claim rows that are not there.
	body[2 + 1] = 0xFF;
	EXPECT_FALSE(ReadProps(body + 2, len, b));
}

// [Kaori-Chani] A material flag with no name is how "no material at all" travels, which is a
// different thing from the flag being absent, and the name is capped rather than overrunning.
TEST(SnapshotFormat, APropsMaterialCarriesTheEmptyNameAndCapsALongOne)
{
	SnapProps a, b;
	memset(&a, 0, sizeof(a));
	a.flags = SNAP_PROPS_MATERIAL;
	a.matLen = 0;

	BYTE body[SNAP_MAX_EVENT_BYTES];
	const unsigned int n = WritePropsBody(body, a);
	EXPECT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + 1 + 1 + 1), n);
	ASSERT_TRUE(ReadProps(body + 2, body[1], b));
	EXPECT_EQ(SNAP_PROPS_MATERIAL, b.flags);
	EXPECT_EQ(0, b.matLen);

	memset(&a, 0, sizeof(a));
	a.flags = SNAP_PROPS_MATERIAL;
	a.matLen = 255;					// past the cap, as a hostile or careless caller
	memset(a.material, 'x', SNAP_PROPS_MATNAME_MAX);
	const unsigned int wide = WritePropsBody(body, a);
	EXPECT_EQ((unsigned int)(SNAP_EVENT_MASK_BYTES + 1 + 1 + 1 + SNAP_PROPS_MATNAME_MAX), wide);
	ASSERT_TRUE(ReadProps(body + 2, body[1], b));
	EXPECT_EQ(SNAP_PROPS_MATNAME_MAX, b.matLen);
}

// [Kaori-Chani] A body cut anywhere, or one claiming a name longer than it holds, is refused
// rather than read past its end.
TEST(SnapshotFormat, APropsBodyShorterThanItsFlagsIsRefused)
{
	SnapProps a, b;
	memset(&a, 0, sizeof(a));
	a.flags = SNAP_PROPS_SCALE | SNAP_PROPS_ROLL | SNAP_PROPS_MATERIAL;
	a.scaleX = 300;
	a.scaleY = 256;
	a.roll = 1;
	a.matLen = 4;
	memcpy(a.material, "burn", 4);

	BYTE body[SNAP_MAX_EVENT_BYTES];
	const unsigned int n = WritePropsBody(body, a);
	const unsigned int len = body[1];
	for (unsigned int cut = 0; cut < len; ++cut)
		EXPECT_FALSE(ReadProps(body + 2, cut, b)) << "cut at " << cut;
	EXPECT_TRUE(ReadProps(body + 2, len, b)) << "the whole body reads";
	EXPECT_EQ(n, len + 2);

	// A name longer than the body holds, which only a bad sender or a mangled
	// packet produces.
	body[2 + 1 + 4 + 2] = (BYTE)(SNAP_PROPS_MATNAME_MAX);
	EXPECT_FALSE(ReadProps(body + 2, len, b));
}

// [Kaori-Chani] A scale crosses at 1/256: the quantizer clamps instead of wrapping, since a
// negative or enormous scale would otherwise come back as something else entirely.
TEST(SnapshotFormat, AScaleQuantizesToTwoBytesAndClamps)
{
	EXPECT_EQ(256, QuantScale(DequantScale(256)));
	EXPECT_EQ((long long)(256 << 8), (long long)DequantScale(256));
	EXPECT_EQ(0, QuantScale((fixed_t)-1));
	EXPECT_EQ(0, QuantScale((fixed_t)(-70000)));
	EXPECT_EQ(65535, QuantScale((fixed_t)(70000 << 8)));
	EXPECT_EQ(65535, QuantScale((fixed_t)((long long)1 << 40)));
	// A scale set by A_SetScale(1.4) lands within a 1/256 step of itself.
	const fixed_t set = (fixed_t)((long long)(1.4 * 65536.0));
	const fixed_t back = DequantScale(QuantScale(set));
	const long long off = (long long)set - (long long)back;
	EXPECT_LT((off < 0) ? -off : off, 256) << "within one step of 1/256";
}

// [Kaori-Chani] Each mover body writes its fixed_t fields as absolute 32-bit 16.16, so the tests
// below pin both extremes a wire field can carry, in both signs: a writer that narrowed a 48.16
// value without the reader sign-extending it back would turn every downward speed and every
// negative plane constant into a huge positive one. The byte count is pinned with them, since the
// chunk layout sizes a record from the constant and a writer disagreeing with it overruns a
// datagram.
namespace {

const fixed_t MOVERTEST_LOW  = fixed_t::FromSignedBits(0x80000000u);
const fixed_t MOVERTEST_HIGH = fixed_t::FromSignedBits(0x7FFFFFFFu);

} // namespace

TEST(SnapshotFormat, ACeilingBodyRoundTripsEveryField)
{
	SnapMoverCeiling a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0xEE, sizeof(b));
	a.speed = MOVERTEST_LOW; a.speed1 = MOVERTEST_HIGH; a.speed2 = -FRACUNIT;
	a.bottom = -1; a.top = FRACUNIT * 64; a.planeD = MOVERTEST_LOW;
	a.tag = -2; a.ceilingID = 0x7FFF; a.crush = -1;
	a.type = 9; a.silent = 1; a.hexencrush = 1;
	a.direction = -1; a.oldDirection = 1;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int n = WriteMoverCeiling(body, a);
	ASSERT_EQ((unsigned int)SNAPMV_CEILING_BYTES, n);
	ReadMoverCeiling(body, b);
	EXPECT_EQ(a.speed, b.speed);
	EXPECT_EQ(a.speed1, b.speed1);
	EXPECT_EQ(a.speed2, b.speed2);
	EXPECT_EQ(a.bottom, b.bottom);
	EXPECT_EQ(a.top, b.top);
	EXPECT_EQ(a.planeD, b.planeD);
	EXPECT_EQ(a.tag, b.tag);
	EXPECT_EQ(a.ceilingID, b.ceilingID);
	EXPECT_EQ(a.crush, b.crush);
	EXPECT_EQ(a.type, b.type);
	EXPECT_EQ(a.silent, b.silent);
	EXPECT_EQ(a.hexencrush, b.hexencrush);
	EXPECT_EQ(a.direction, b.direction);
	EXPECT_EQ(a.oldDirection, b.oldDirection);
}

TEST(SnapshotFormat, AFloorBodyRoundTripsInBothItsShapes)
{
	for (unsigned int s = 0; s < 2; ++s)
	{
		const bool bStair = (s == 1);
		SnapMoverFloor a, b;
		memset(&a, 0, sizeof(a));
		memset(&b, 0xEE, sizeof(b));
		a.speed = MOVERTEST_HIGH; a.destDist = MOVERTEST_LOW; a.planeD = -1;
		a.floorID = -3; a.crush = 0x7FFF;
		a.type = 4; a.hexencrush = 1; a.direction = -1;
		a.orgDist = MOVERTEST_LOW;
		a.resetCount = -1; a.delay = 0x7FFFFFFF; a.pauseTime = -2;
		a.stepTime = 8; a.perStepTime = -8;

		BYTE body[SNAPMV_BODY_MAX];
		const unsigned int n = WriteMoverFloor(body, a, bStair);
		ASSERT_EQ(bStair ? (unsigned int)SNAPMV_FLOOR_STAIR_BYTES
			: (unsigned int)SNAPMV_FLOOR_BYTES, n) << "stair " << bStair;
		ReadMoverFloor(body, b, bStair);
		EXPECT_EQ(a.speed, b.speed) << "stair " << bStair;
		EXPECT_EQ(a.destDist, b.destDist) << "stair " << bStair;
		EXPECT_EQ(a.planeD, b.planeD) << "stair " << bStair;
		EXPECT_EQ(a.floorID, b.floorID) << "stair " << bStair;
		EXPECT_EQ(a.crush, b.crush) << "stair " << bStair;
		EXPECT_EQ(a.type, b.type) << "stair " << bStair;
		EXPECT_EQ(a.hexencrush, b.hexencrush) << "stair " << bStair;
		EXPECT_EQ(a.direction, b.direction) << "stair " << bStair;
		if (bStair)
		{
			EXPECT_EQ(a.orgDist, b.orgDist);
			EXPECT_EQ(a.resetCount, b.resetCount);
			EXPECT_EQ(a.delay, b.delay);
			EXPECT_EQ(a.pauseTime, b.pauseTime);
			EXPECT_EQ(a.stepTime, b.stepTime);
			EXPECT_EQ(a.perStepTime, b.perStepTime);
		}
		else
		{
			// [Kaori-Chani] A decoded record carries only what the wire did, so the stair
			// fields read as zero rather than as whatever the caller left in the struct.
			EXPECT_EQ(0, b.orgDist);
			EXPECT_EQ(0, b.resetCount);
			EXPECT_EQ(0, b.delay);
			EXPECT_EQ(0, b.pauseTime);
			EXPECT_EQ(0, b.stepTime);
			EXPECT_EQ(0, b.perStepTime);
		}
	}
}

TEST(SnapshotFormat, ADoorBodyRoundTripsEveryField)
{
	SnapMoverDoor a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0xEE, sizeof(b));
	a.speed = MOVERTEST_LOW; a.topDist = MOVERTEST_HIGH; a.botDist = -1;
	a.oldFloorDist = FRACUNIT * 8; a.planeD = MOVERTEST_LOW;
	a.topWait = -1; a.topCountdown = 0x7FFFFFFF; a.botSpot = -2;
	a.lightTag = -4; a.doorID = 0x7FFF;
	a.type = 3; a.direction = 2;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int n = WriteMoverDoor(body, a);
	ASSERT_EQ((unsigned int)SNAPMV_DOOR_BYTES, n);
	ReadMoverDoor(body, b);
	EXPECT_EQ(a.speed, b.speed);
	EXPECT_EQ(a.topDist, b.topDist);
	EXPECT_EQ(a.botDist, b.botDist);
	EXPECT_EQ(a.oldFloorDist, b.oldFloorDist);
	EXPECT_EQ(a.planeD, b.planeD);
	EXPECT_EQ(a.topWait, b.topWait);
	EXPECT_EQ(a.topCountdown, b.topCountdown);
	EXPECT_EQ(a.botSpot, b.botSpot);
	EXPECT_EQ(a.lightTag, b.lightTag);
	EXPECT_EQ(a.doorID, b.doorID);
	EXPECT_EQ(a.type, b.type);
	EXPECT_EQ(a.direction, b.direction) << "a first wait of 2 is a phase, not a sign";
}

TEST(SnapshotFormat, AnElevatorBodyRoundTripsEveryField)
{
	SnapMoverElevator a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0xEE, sizeof(b));
	a.speed = -FRACUNIT; a.floorDestDist = MOVERTEST_LOW;
	a.ceilingDestDist = MOVERTEST_HIGH; a.floorPlaneD = -1;
	a.ceilingPlaneD = FRACUNIT * 128;
	a.elevatorID = -5; a.type = 2; a.direction = -1;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int n = WriteMoverElevator(body, a);
	ASSERT_EQ((unsigned int)SNAPMV_ELEVATOR_BYTES, n);
	ReadMoverElevator(body, b);
	EXPECT_EQ(a.speed, b.speed);
	EXPECT_EQ(a.floorDestDist, b.floorDestDist);
	EXPECT_EQ(a.ceilingDestDist, b.ceilingDestDist);
	EXPECT_EQ(a.floorPlaneD, b.floorPlaneD);
	EXPECT_EQ(a.ceilingPlaneD, b.ceilingPlaneD);
	EXPECT_EQ(a.elevatorID, b.elevatorID);
	EXPECT_EQ(a.type, b.type);
	EXPECT_EQ(a.direction, b.direction);
}

TEST(SnapshotFormat, APillarBodyRoundTripsEveryField)
{
	SnapMoverPillar a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0xEE, sizeof(b));
	a.floorSpeed = MOVERTEST_LOW; a.ceilingSpeed = MOVERTEST_HIGH;
	a.floorTarget = -1; a.ceilingTarget = FRACUNIT * 32;
	a.floorPlaneD = MOVERTEST_LOW; a.ceilingPlaneD = -FRACUNIT;
	a.pillarID = -6; a.crush = 0x7FFF;
	a.type = 1; a.hexencrush = 1; a.blocked = SNAPMV_PLANE_BOTH;

	BYTE body[SNAPMV_BODY_MAX];
	const unsigned int n = WriteMoverPillar(body, a);
	ASSERT_EQ((unsigned int)SNAPMV_PILLAR_BYTES, n);
	ReadMoverPillar(body, b);
	EXPECT_EQ(a.floorSpeed, b.floorSpeed);
	EXPECT_EQ(a.ceilingSpeed, b.ceilingSpeed);
	EXPECT_EQ(a.floorTarget, b.floorTarget);
	EXPECT_EQ(a.ceilingTarget, b.ceilingTarget);
	EXPECT_EQ(a.floorPlaneD, b.floorPlaneD);
	EXPECT_EQ(a.ceilingPlaneD, b.ceilingPlaneD);
	EXPECT_EQ(a.pillarID, b.pillarID);
	EXPECT_EQ(a.crush, b.crush);
	EXPECT_EQ(a.type, b.type);
	EXPECT_EQ(a.hexencrush, b.hexencrush);
	EXPECT_EQ(a.blocked, b.blocked) << "which plane stalled";
}
