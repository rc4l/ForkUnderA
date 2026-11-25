// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: fuzzgen.h
//
// Description: Fuzz inputs for the snapshot decoder: seeds built by the
// writers, and the mutation. Engine-free, shared by the standalone fuzzer
// (tests/snapfuzz, the snap_fuzz target) and the engine's fua_snap_fuzz command.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_FUZZGEN_H
#define ZX_NETSNAPSHOT_FUZZGEN_H

#include <string.h>
#include <vector>

#include "features/net-snapshot/format.h"

// MSVC runs out of heap optimizing the seed builders (C1002), so it builds
// these out of line and unoptimized; the fuzzers need no speed from them.
#ifdef _MSC_VER
#define SNAP_FUZZ_NOINLINE	__declspec(noinline)
#pragma optimize("", off)
#else
#define SNAP_FUZZ_NOINLINE	__attribute__((noinline))
#endif

// Xorshift: the same sequence on every platform for a given seed.
struct SnapFuzzRng
{
	DWORD	s;
};

namespace zx { namespace netsnapshot {

static inline DWORD FuzzRand(SnapFuzzRng& r)
{
	r.s ^= r.s << 13;
	r.s ^= r.s >> 17;
	r.s ^= r.s << 5;
	return r.s;
}

static SNAP_FUZZ_NOINLINE void FuzzFill(SnapFuzzRng& r, void* p, size_t n)
{
	for (size_t b = 0; b < n; ++b)
		((BYTE*)p)[b] = (BYTE)FuzzRand(r);
}

}} // namespace zx::netsnapshot

// A valid input, the offsets of the bytes its readers branch on, and the
// frame it was written in (FuzzFrame).
struct SnapFuzzSeed
{
	std::vector<BYTE>			bytes;
	std::vector<unsigned int>	hot;
	unsigned int				frameBits;

	SnapFuzzSeed() : frameBits(0) {}
};

namespace zx { namespace netsnapshot {

// The frame a fuzz input is read in: bit k of `bits` makes axis k wide, at
// an origin putting one limit near 0, as the runner's wide frame does. 0 is
// the narrow frame every recorded map has.
static inline SnapFrame FuzzFrame(unsigned int bits)
{
	SnapFrame f = FrameNarrow();
	for (unsigned int k = 0; k < 3; ++k)
	{
		if (bits & (1u << k))
		{
			f.width[k] = SNAP_FRAME_WIDE;
			f.origin[k] = (k == 1) ? -8387608 : 8387608;
		}
	}
	return f;
}

}} // namespace zx::netsnapshot

// How many sectors and polyobjects a mover record may index: the loaded level's
// in fua_snap_fuzz's sinks mode, else 65536, with 0 also drawing any index.
struct SnapFuzzLevel
{
	unsigned int	sectors;
	unsigned int	polys;
};

namespace zx { namespace netsnapshot {

static inline void FuzzAppend(SnapFuzzSeed& s, const BYTE* p, unsigned int n)
{
	s.bytes.insert(s.bytes.end(), p, p + n);
}

static SNAP_FUZZ_NOINLINE void FuzzSeedMover(SnapFuzzRng& r, const SnapFrame& f,
	SnapFuzzSeed& s, BYTE cls, const SnapFuzzLevel& lv)
{
	BYTE body[SNAPMV_BODY_MAX];
	unsigned int n = 0;
	BYTE flags = (BYTE)(FuzzRand(r) & (SNAPMVF_BLOCKED | SNAPMVF_GONE));
	switch (cls)
	{
	case SNAPMV_PLAT: { SnapMoverPlat m; FuzzFill(r, &m, sizeof(m)); n = WriteMoverPlat(body, m); break; }
	case SNAPMV_CEILING: { SnapMoverCeiling m; FuzzFill(r, &m, sizeof(m)); n = WriteMoverCeiling(body, m); break; }
	case SNAPMV_FLOOR:
	{
		SnapMoverFloor m;
		FuzzFill(r, &m, sizeof(m));
		const bool bStair = (FuzzRand(r) & 1) != 0;
		if (bStair)
			flags |= SNAPMVF_STAIR;
		n = WriteMoverFloor(body, m, bStair);
		break;
	}
	case SNAPMV_DOOR: { SnapMoverDoor m; FuzzFill(r, &m, sizeof(m)); n = WriteMoverDoor(body, m); break; }
	case SNAPMV_ELEVATOR: { SnapMoverElevator m; FuzzFill(r, &m, sizeof(m)); n = WriteMoverElevator(body, m); break; }
	case SNAPMV_PILLAR: { SnapMoverPillar m; FuzzFill(r, &m, sizeof(m)); n = WriteMoverPillar(body, m); break; }
	default:
	{
		SnapMoverPoly m;
		FuzzFill(r, &m, sizeof(m));
		m.count = (BYTE)(FuzzRand(r) % 4);
		for (unsigned int e = 0; e < m.count; ++e)
			m.e[e].kind = (BYTE)(FuzzRand(r) % 5);
		n = WriteMoverPoly(body, f, m);
		break;
	}
	}
	BYTE rec[1 + 1 + SNAPMV_HEADER_BYTES + SNAPMV_BODY_MAX];
	const unsigned int at = (unsigned int)s.bytes.size();
	// Drawn as statements, since argument order differs between compilers; this
	// is the order GCC used, which the published figures came from.
	const BYTE gen = (BYTE)(1 + FuzzRand(r) % 255);
	const unsigned int span = (cls == SNAPMV_POLY) ? lv.polys : lv.sectors;
	const WORD index = (WORD)(FuzzRand(r) % ((span > 0) ? span : 65536u));
	const unsigned int len = WriteMoverBody(rec, cls, flags, index, gen, body, n);
	FuzzAppend(s, rec, len);
	s.hot.push_back(at + 1);	// length byte
	s.hot.push_back(at + 2);										// class
	s.hot.push_back(at + 3);										// flags
	if (cls == SNAPMV_POLY)								// entry count
		s.hot.push_back(at + 1 + SNAPMV_HEADER_BYTES + PolyHeaderBytes(f));
}

// One payload in the frame `frameBits` names: entity records of every group
// combination, the fixed events, every mover class, and a self-describing type
// this build skips.
static SNAP_FUZZ_NOINLINE SnapFuzzSeed FuzzSeedPayload(SnapFuzzRng& r,
	unsigned int frameBits, const SnapFuzzLevel& lv)
{
	const SnapFrame f = FuzzFrame(frameBits);
	SnapFuzzSeed s;
	s.frameBits = frameBits;
	SnapIdCursor ids;
	IdCursorReset(ids);
	const unsigned int records = 2 + FuzzRand(r) % 10;
	for (unsigned int k = 0; k < records; ++k)
	{
		BYTE idbuf[SNAP_IDGAP_MAXBYTES];
		const WORD id = (WORD)(1 + FuzzRand(r) % 4000);
		FuzzAppend(s, idbuf, WriteRecordId(idbuf, id, ids));
		const unsigned int at = (unsigned int)s.bytes.size();
		BYTE rec[256];
		unsigned int n = 0;
		const unsigned int kind = FuzzRand(r) % 9;
		if (kind <= 3)
		{
			SnapEntityState e;
			FuzzFill(r, &e, sizeof(e));
			const BYTE groups = (BYTE)((FuzzRand(r) & SNAPGRP_GROUPMASK) | SNAPGRP_POS);
			const bool bEnter = (kind <= 1);
			const WORD target = (bEnter && (kind == 0)) ? (WORD)(1 + FuzzRand(r) % 4000) : 0;
			n = WriteRecordBody(rec, f,
				(BYTE)(groups | (bEnter ? SNAPGRP_ENTER : 0)),
				(WORD)(FuzzRand(r) & SNAP_CLASS_MASK), e, NULL, target);
			s.hot.push_back(at);	// mask: ENTER
			if (bEnter)
				s.hot.push_back(at + 2);	// classId's target bit
			if (groups & SNAPGRP_APPEAR)
				s.hot.push_back(at + 1 + (bEnter ? 2 : 0) + (target ? 2 : 0)
					+ GroupBytesBeforeAppear(f, groups));		// wide flag
		}
		else if (kind == 4)
		{
			rec[0] = 0;												// leave
			n = 1;
		}
		else if (kind == 5)
		{
			SnapEntityState e;
			FuzzFill(r, &e, sizeof(e));
			n = WriteEventBody(rec, f, SNAPEV_BRANCH, e, FuzzRand(r));
		}
		else if (kind == 6)
		{
			SnapPSpriteState ps;
			FuzzFill(r, &ps, sizeof(ps));
			n = (FuzzRand(r) & 1) ? WritePSpriteBody(rec, ps) : 0;
			if (n == 0)
			{
				SnapLocalState ls;
				FuzzFill(r, &ls, sizeof(ls));
				n = WriteLocalBody(rec, f, ls);
			}
		}
		else if (kind == 7)
		{
			FuzzSeedMover(r, f, s, (BYTE)(FuzzRand(r) % (SNAPMV_POLY + 1)), lv);
			continue;
		}
		else
		{
			rec[0] = (BYTE)(SNAPGRP_EVENT | SNAPEV_SELFDESC | (1 + FuzzRand(r) % 15));
			rec[1] = (BYTE)(1 + FuzzRand(r) % 40);
			FuzzFill(r, rec + 2, rec[1]);
			n = 2 + rec[1];
			s.hot.push_back(at + 1);	// length byte
		}
		FuzzAppend(s, rec, n);
	}
	return s;
}

// A cosmetic events command after its header, in the frame `frameBits` names:
// the map stamp, the count, then generic and blood events with their age.
static SNAP_FUZZ_NOINLINE SnapFuzzSeed FuzzSeedCosmetic(SnapFuzzRng& r,
	unsigned int frameBits)
{
	const SnapFrame f = FuzzFrame(frameBits);
	SnapFuzzSeed s;
	s.frameBits = frameBits;
	const unsigned int count = 1 + FuzzRand(r) % 6;
	s.bytes.push_back((BYTE)FuzzRand(r));
	s.bytes.push_back((BYTE)count);
	s.bytes.push_back(0);
	s.hot.push_back(1);
	for (unsigned int e = 0; e < count; ++e)
	{
		BYTE buf[64];
		unsigned int n;
		const unsigned int at = (unsigned int)s.bytes.size();
		bool bBig, bSmall;
		if (FuzzRand(r) & 1)
		{
			SnapCosGeneric g;
			FuzzFill(r, &g, sizeof(g));
			g.stateCode = (FuzzRand(r) & 1) ? (int)(FuzzRand(r) & 0xFFFF) : -1;
			g.extras &= 0x1F;
			// [Kaori-Chani] The modulus is taken in integer arithmetic, since FRACUNIT is a Fixed
			// on 48.16 and these are unsigned magnitudes in an actor's own range.
			const long long fu = (long long)FRACUNIT;
			g.alpha = (fixed_t)(long long)(FuzzRand(r) % (DWORD)(fu + 1));	// actors' range
			g.scaleX = (fixed_t)(long long)(FuzzRand(r) % (DWORD)(64 * fu));
			g.scaleY = (fixed_t)(long long)(FuzzRand(r) % (DWORD)(64 * fu));
			n = CosWriteGeneric(buf, f, (BYTE)(FuzzRand(r) & 1), g,
				bBig, bSmall);
			s.hot.push_back(at);
			if (buf[0] & SNAP_COSE_EXTRAS)
				s.hot.push_back(at + 4 + f.width[0] + f.width[1] + f.width[2]
					+ ((buf[0] & SNAP_COSE_FINEVEL) ? 6 : 3)
					+ ((buf[0] & SNAP_COSE_STATE) ? 2 : 0));	// extras mask
		}
		else
		{
			SnapCosBlood b;
			FuzzFill(r, &b, sizeof(b));
			n = CosWriteBlood(buf, f,
				(BYTE)(SNAP_COSE_BLOOD + (FuzzRand(r) & 1)), b);
			s.hot.push_back(at);
		}
		buf[0] |= (BYTE)((FuzzRand(r) & 7) << SNAP_COSE_AGE_SHIFT);
		FuzzAppend(s, buf, n);
	}
	return s;
}

// One to four edits, favouring the bytes the readers branch on.
static SNAP_FUZZ_NOINLINE void FuzzMutate(SnapFuzzRng& r, SnapFuzzSeed& s)
{
	std::vector<BYTE>& b = s.bytes;
	const unsigned int rounds = 1 + FuzzRand(r) % 4;
	for (unsigned int k = 0; k < rounds; ++k)
	{
		const unsigned int op = FuzzRand(r) % 8;
		if (b.empty())
			b.push_back((BYTE)FuzzRand(r));
		const unsigned int at = FuzzRand(r) % (unsigned int)b.size();
		if ((op <= 2) && !s.hot.empty())
		{
			// A byte the readers branch on, at a boundary value.
			static const BYTE edge[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF };
			const unsigned int h = s.hot[FuzzRand(r) % s.hot.size()];
			if (h < b.size())
				b[h] = (op == 0) ? (BYTE)(b[h] ^ (1 << (FuzzRand(r) % 8)))
					: (op == 1) ? edge[FuzzRand(r) % 5] : (BYTE)(b[h] + (FuzzRand(r) & 1 ? 1 : -1));
		}
		else if (op == 3)
			b[at] ^= (BYTE)(1 << (FuzzRand(r) % 8));
		else if (op == 4)
			b[at] = (BYTE)FuzzRand(r);
		else if (op == 5)
			b.resize(at);											// truncate
		else if (op == 6)
			b.insert(b.begin() + at, (BYTE)FuzzRand(r));		// insert
		else
			b.erase(b.begin() + at);								// delete
	}
}

}} // namespace zx::netsnapshot

#ifdef _MSC_VER
#pragma optimize("", on)
#endif

#endif // ZX_NETSNAPSHOT_FUZZGEN_H
