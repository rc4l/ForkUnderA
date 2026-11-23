// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: fuzz.cpp
//
// Description: Decoder fuzzer for the snapshot wire format, without the
// engine: a model of the client's payload walk over snap_format.h's length
// functions and readers, the cosmetic event stream and both file readers.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "features/net-snapshot/format.h"
#include "features/net-snapshot/fuzzgen.h"
#include "features/net-snapshot/record.h"
#ifdef SNAP_FUZZ_ZSNAP
#include <zstd.h>
#include "features/net-snapshot/stream.h"
#endif

#ifdef _WIN32
#include <process.h>
#define fuzz_getpid _getpid
#else
#include <unistd.h>
#define fuzz_getpid getpid
#endif

using namespace zx::netsnapshot;

// MSVC runs out of memory optimizing this file, as with snap_fuzzgen.h, so
// its code builds unoptimized there; the readers defined above do not.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif

typedef std::vector<BYTE> Bytes;

//*****************************************************************************
//	FINDINGS

// Findings already queued for a fix: reported once each, not failed. Add an
// entry with its finding and delete it with its fix; none are open.
static const char* const g_known[] =
{
	NULL
};
static unsigned int g_knownHits[sizeof(g_known) / sizeof(g_known[0])];

static const BYTE*	g_input = NULL;		// the input being run, for the dump
static size_t		g_inputSize = 0;

static void fuzz_Finding(const char* tag, const char* what)
{
	for (unsigned int k = 0; g_known[k] != NULL; ++k)
	{
		if (strcmp(g_known[k], tag) == 0)
		{
			if (g_knownHits[k]++ == 0)
				printf("snap_fuzz: known finding %s: %s\n", tag, what);
			return;
		}
	}
	printf("snap_fuzz: FINDING %s: %s\ninput (%u bytes):", tag, what, (unsigned int)g_inputSize);
	for (size_t b = 0; b < g_inputSize; ++b)
		printf("%s%02x", (b % 32) ? " " : "\n  ", g_input[b]);
	printf("\n");
	fflush(stdout);
	abort();
}

//*****************************************************************************
//	REACH

// How many inputs reached each reader, so a run that never gets past the id
// is visible.
enum
{
	R_RECORD, R_LEAVE, R_ENTER, R_TARGET, R_GROUPS, R_APPEARWIDE, R_BRANCH, R_PSPRITE,
	R_LOCAL, R_SKIP, R_MOVERHDR, R_MOVERBODY, R_POLY, R_COSGENERIC, R_COSBLOOD,
	R_ZSNAPOPEN, R_ZSNAPRECORD, R_CSNAPOPEN, R_CSNAPRECORD, R_WIDEFRAME, R_COUNT
};
static const char* const g_reachName[R_COUNT] =
{
	"records", "leaves", "enters", "targets", "groups", "wide appear", "branch",
	"psprite", "local", "skipped", "mover header", "mover body", "poly",
	"cosmetic generic", "cosmetic blood", "zsnap open", "zsnap record", "csnap open",
	"csnap record", "walks in a wide frame"
};
static unsigned long long g_reach[R_COUNT];

//*****************************************************************************
//	READER CHECK

// A reader run on an exact-size copy of the bytes its length function granted
// (an overread is a sanitizer report) and on a padded copy, whose result must
// match (an overread without a sanitizer).
template <typename T, typename F>
static void fuzz_Read(const char* tag, const BYTE* p, unsigned int n, F read)
{
	BYTE* exact = (BYTE*)malloc(n ? n : 1);
	BYTE* padded = (BYTE*)malloc(n + 64);
	memcpy(exact, p, n);
	memcpy(padded, p, n);
	memset(padded + n, 0xA5, 64);
	T a, b;
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	read(exact, a);
	read(padded, b);
	if (memcmp(&a, &b, sizeof(a)) != 0)
		fuzz_Finding(tag, "result depends on bytes past the length granted");
	free(exact);
	free(padded);
}

struct FuzzEvent { WORD tic; SnapEntityState e; };
struct FuzzHeader { BYTE cls, flags, gen; WORD index; };
struct FuzzPoly { bool bOk; SnapMoverPoly s; };

// The readers' frame: from the input's first byte for a payload or cosmetic
// input, from the file for a container.
static SnapFrame g_frame = FrameNarrow();

//*****************************************************************************
//	PAYLOAD WALK

// Mirrors ApplyDelta's walk and clent_ApplyEvent's event paths
// (cl_entities.cpp), call for call; keep it in step with them. Every reader the
// client may reach is run, whatever ghost state would have skipped it.
static void fuzz_Event(BYTE type, const BYTE* p, unsigned int n, unsigned int version)
{
	switch (type & SNAPEV_TYPEMASK)
	{
	case SNAPEV_MOVER:
	{
		const unsigned int hdrBytes = MoverHeaderBytes(version);
		if (n < hdrBytes)
			return;
		++g_reach[R_MOVERHDR];
		FuzzHeader h;
		fuzz_Read<FuzzHeader>("mover-header", p, hdrBytes, [&](const BYTE* q, FuzzHeader& o)
			{ ReadMoverHeader(q, hdrBytes, o.cls, o.flags, o.index, o.gen); });
		ReadMoverHeader(p, hdrBytes, h.cls, h.flags, h.index, h.gen);
		const unsigned int body = MoverRecordBodyBytes(g_frame, h.cls, h.flags,
			p + hdrBytes,
			n - hdrBytes);
		if ((body == 0) || ((hdrBytes + body) != n))
			return;
		++g_reach[R_MOVERBODY];
		const BYTE* q = p + hdrBytes;
		const bool bStair = (h.flags & SNAPMVF_STAIR) != 0;
		switch (h.cls)
		{
		case SNAPMV_PLAT:
			fuzz_Read<SnapMoverPlat>("plat", q, body, [](const BYTE* r, SnapMoverPlat& o)
				{ ReadMoverPlat(r, o); });
			break;
		case SNAPMV_CEILING:
			fuzz_Read<SnapMoverCeiling>("ceiling", q, body, [](const BYTE* r, SnapMoverCeiling& o)
				{ ReadMoverCeiling(r, o); });
			break;
		case SNAPMV_FLOOR:
			fuzz_Read<SnapMoverFloor>("floor", q, body, [&](const BYTE* r, SnapMoverFloor& o)
				{ ReadMoverFloor(r, o, bStair); });
			break;
		case SNAPMV_DOOR:
			fuzz_Read<SnapMoverDoor>("door", q, body, [](const BYTE* r, SnapMoverDoor& o)
				{ ReadMoverDoor(r, o); });
			break;
		case SNAPMV_ELEVATOR:
			fuzz_Read<SnapMoverElevator>("elevator", q, body, [](const BYTE* r, SnapMoverElevator& o)
				{ ReadMoverElevator(r, o); });
			break;
		case SNAPMV_PILLAR:
			fuzz_Read<SnapMoverPillar>("pillar", q, body, [](const BYTE* r, SnapMoverPillar& o)
				{ ReadMoverPillar(r, o); });
			break;
		case SNAPMV_POLY:
			++g_reach[R_POLY];
			fuzz_Read<FuzzPoly>("poly", q, body, [&](const BYTE* r, FuzzPoly& o)
				{ o.bOk = ReadMoverPoly(r, g_frame, body, o.s); });
			break;
		default:
			fuzz_Finding("mover-class", "a body length for a class with no reader");
		}
		return;
	}
	case SNAPEV_LOCAL:
		++g_reach[R_LOCAL];
		fuzz_Read<SnapLocalState>("local", p, n, [](const BYTE* q, SnapLocalState& o)
			{ ReadLocal(q, g_frame, o); });
		return;
	case SNAPEV_PSPRITE:
		++g_reach[R_PSPRITE];
		fuzz_Read<SnapPSpriteState>("psprite", p, n, [](const BYTE* q, SnapPSpriteState& o)
			{ ReadPSprite(q, o); });
		return;
	case SNAPEV_BRANCH:
		++g_reach[R_BRANCH];
		fuzz_Read<FuzzEvent>("branch", p, n, [&](const BYTE* q, FuzzEvent& o)
			{ o.tic = ReadEvent(q, g_frame, type, o.e); });
		return;
	}
}

static void fuzz_Walk(const BYTE* pData, unsigned int size, unsigned int version)
{
	if ((g_frame.width[0] | g_frame.width[1] | g_frame.width[2]) != SNAP_FRAME_NARROW)
		++g_reach[R_WIDEFRAME];
	SnapIdCursor ids;
	IdCursorReset(ids);
	unsigned int off = 0;
	while (off < size)
	{
		WORD id = 0;
		const unsigned int idBytes = ReadRecordId(pData + off, size - off, id, ids);
		if (idBytes == 0)
			return;
		if (idBytes > size - off)
			fuzz_Finding("id-length", "ReadRecordId granted more than remains");
		off += idBytes;
		if (off + 1 > size)
			return;
		const BYTE rawMask = pData[off];
		off += 1;
		++g_reach[R_RECORD];

		if ((rawMask & SNAPGRP_EVENT) != 0)
		{
			const BYTE evType = (BYTE)(rawMask & SNAPEV_TYPEMASK);
			unsigned int evBytes = 0;
			bool bSkip = false;
			if (EventRecordBytes(g_frame, evType, pData + off, size - off, evBytes, bSkip)
				!= SNAPEVLEN_OK)
				return;
			if ((evBytes == 0) || (evBytes > size - off))
				fuzz_Finding("event-length", "EventRecordBytes granted 0 or more than remains");
			if (bSkip)
				++g_reach[R_SKIP];
			else
			{
				const unsigned int lead = EventSelfDescribing(evType) ? 1u : 0u;
				fuzz_Event(evType, pData + off + lead, evBytes - lead, version);
			}
			off += evBytes;
			continue;
		}

		const BYTE mask = (BYTE)(rawMask & SNAPGRP_GROUPMASK);
		if (mask == 0)
		{
			++g_reach[R_LEAVE];
			continue;
		}
		if (rawMask & SNAPGRP_ENTER)
		{
			if (off + 2 > size)
				return;
			const bool bTarget = (Read16(pData + off) & SNAP_CLASS_TARGET) != 0;
			off += 2;
			++g_reach[R_ENTER];
			if (bTarget)
			{
				if (off + 2 > size)
					return;
				off += 2;
				++g_reach[R_TARGET];
			}
		}
		unsigned int need = 0;
		if (!GroupBytesRead(g_frame, mask, pData + off, size - off, need))
			return;
		if (need > size - off)
			fuzz_Finding("group-length", "GroupBytesRead granted more than remains");
		++g_reach[R_GROUPS];
		if (need > GroupBytesBeforeAppear(g_frame, mask) + SNAP_GRPB_APPEAR_NARROW
			+ ((mask & SNAPGRP_RENDER) ? SNAP_GRPB_RENDER : 0))
			++g_reach[R_APPEARWIDE];
		fuzz_Read<SnapEntityState>("groups", pData + off, need, [&](const BYTE* q, SnapEntityState& o)
			{ ReadGroups(q, g_frame, o, mask); });
		off += need;
	}
}

//*****************************************************************************
//	COSMETIC EVENTS

// Mirrors CosmeticEvents's reads (cl_ghostactor.cpp): every event in
// the stream, through the codec's cursor. The decoded events of a whole
// stream must not change with padding past its end.
struct FuzzCos { int flags; SnapCosGeneric g; SnapCosBlood b; };

static void fuzz_CosStream(const BYTE* p, unsigned int n, std::vector<FuzzCos>& out)
{
	SnapCosCursor c = { p, p + n };
	CosRead8(c);								// map stamp
	const unsigned int count = (WORD)CosRead16(c);
	for (unsigned int e = 0; e < count; ++e)
	{
		FuzzCos ev;
		memset(&ev, 0, sizeof(ev));
		ev.flags = CosRead8(c);
		const int kind = ev.flags & SNAP_COSE_KIND_MASK;
		if ((kind == SNAP_COSE_COPY) || (kind == SNAP_COSE_PLAIN))
			CosReadGeneric(c, g_frame, ev.flags, ev.g);
		else
			CosReadBlood(c, g_frame, ev.flags, ev.b);
		if (c.p > c.end)
			break;
		++g_reach[((kind == SNAP_COSE_COPY) || (kind == SNAP_COSE_PLAIN)) ? R_COSGENERIC : R_COSBLOOD];
		out.push_back(ev);
	}
}

static void fuzz_Cosmetic(const BYTE* p, unsigned int n)
{
	BYTE* exact = (BYTE*)malloc(n ? n : 1);
	BYTE* padded = (BYTE*)malloc(n + 64);
	memcpy(exact, p, n);
	memcpy(padded, p, n);
	memset(padded + n, 0xA5, 64);
	std::vector<FuzzCos> a, b;
	fuzz_CosStream(exact, n, a);
	fuzz_CosStream(padded, n, b);
	if ((a.size() != b.size()) || (!a.empty() && (memcmp(&a[0], &b[0], a.size() * sizeof(FuzzCos)) != 0)))
		fuzz_Finding("cosmetic", "decoded events depend on bytes past the stream");
	free(exact);
	free(padded);
}

//*****************************************************************************
//	.ZSNAP

#ifdef SNAP_FUZZ_ZSNAP
static char g_zsnapPath[64];

// The container through ZsnapOpen and ZsnapReadRecord, by file, and each
// payload through the walk. The reader must never allocate more stored bytes
// than the file holds, or more raw bytes than ZSNAP_MAX_RAWSIZE.
static void fuzz_Zsnap(const BYTE* p, unsigned int n)
{
	FILE* f = fopen(g_zsnapPath, "wb");
	if (f == NULL)
		return;
	fwrite(p, 1, n, f);
	fclose(f);

	SnapStreamReader reader;
	if (ZsnapOpen(reader, g_zsnapPath) == false)
		return;
	++g_reach[R_ZSNAPOPEN];
	for (unsigned int r = 0; r < 64; ++r)
	{
		const ESnapReadResult res = ZsnapReadRecord(reader);
		if ((reader.storedCap > n) || (reader.rawCap > ZSNAP_MAX_RAWSIZE))
			fuzz_Finding("zsnap-alloc", "the reader allocated past the file or the raw limit");
		if (res != SNAPREAD_OK)
			break;
		++g_reach[R_ZSNAPRECORD];
		if (reader.payloadSize > 0)
		{
			g_frame = reader.frame;
			BYTE* exact = (BYTE*)malloc(reader.payloadSize);
			memcpy(exact, reader.payload, reader.payloadSize);
			fuzz_Walk(exact, reader.payloadSize, reader.version);
			free(exact);
		}
	}
	ZsnapClose(reader);
}
#endif

//*****************************************************************************
//	.CSNAP

static char g_csnapPath[64];

// The reader must never allocate a payload, or a cvar block of 5-byte
// entries, larger than the file of n bytes.
static void fuzz_CsnapAlloc(const CsnapReader& reader, unsigned int n)
{
	if ((reader.storedCap > n) || (reader.cvarCount * 5u > n))
		fuzz_Finding("csnap-alloc", "the reader allocated past the file");
}

// The container through CsnapOpen and CsnapReadRecord, by file, and each
// DELTA payload through the walk at the wire version its container carries.
static void fuzz_Csnap(const BYTE* p, unsigned int n)
{
	FILE* f = fopen(g_csnapPath, "wb");
	if (f == NULL)
		return;
	fwrite(p, 1, n, f);
	fclose(f);

	CsnapReader reader;
	const bool bOpen = CsnapOpen(reader, g_csnapPath);
	fuzz_CsnapAlloc(reader, n);
	if (bOpen)
	{
		++g_reach[R_CSNAPOPEN];
		const unsigned int version = (reader.version >= 15) ? (unsigned int)ZSNAP_VERSION : 28u;
		for (unsigned int r = 0; r < 64; ++r)
		{
			const ESnapReadResult res = CsnapReadRecord(reader);
			fuzz_CsnapAlloc(reader, n);
			if (res != SNAPREAD_OK)
				break;
			++g_reach[R_CSNAPRECORD];
			if ((reader.kind == CSNAP_DELTA) && (reader.payloadSize > 0))
			{
				g_frame = reader.mapFrame;
				BYTE* exact = (BYTE*)malloc(reader.payloadSize);
				memcpy(exact, reader.payload, reader.payloadSize);
				fuzz_Walk(exact, reader.payloadSize, version);
				free(exact);
			}
		}
	}
	CsnapClose(reader);
}

//*****************************************************************************
//	ENTRY

// The first byte picks the target (0-3 payload, 4-5 cosmetic, 6 .zsnap, 7
// .csnap), the payload's version, and in its next three bits the axes a
// payload or cosmetic input is wide on (FuzzFrame); the rest is the
// target's input.
static void fuzz_one(const BYTE* data, size_t size)
{
	if (size < 1)
		return;
	g_input = data;
	g_inputSize = size;
	const unsigned int sel = data[0] & 7;
	const unsigned int version = (data[0] & 8) ? 27u : (unsigned int)ZSNAP_VERSION;
	g_frame = FuzzFrame((data[0] >> 4) & 7);
	const BYTE* p = data + 1;
	const unsigned int n = (unsigned int)(size - 1);
	if (sel <= 3)
	{
		BYTE* exact = (BYTE*)malloc(n ? n : 1);
		memcpy(exact, p, n);
		fuzz_Walk(exact, n, version);
		free(exact);
	}
	else if (sel <= 5)
		fuzz_Cosmetic(p, n);
	else if (sel == 7)
		fuzz_Csnap(p, n);
#ifdef SNAP_FUZZ_ZSNAP
	else
		fuzz_Zsnap(p, n);
#endif
}

//*****************************************************************************
//	SEEDS

// One generator for the shared seeds and mutation (snap_fuzzgen.h) and the
// container seeds below.
static SnapFuzzRng g_rng = { 0x9E3779B9u };
static DWORD fuzz_Rand()
{
	return FuzzRand(g_rng);
}

// Mover records may name any index, as a corrupt file or stream can.
static const SnapFuzzLevel g_anyIndex = { 65536, 65536 };

static void seed_Put32(Bytes& b, DWORD v)
{
	for (unsigned int k = 0; k < 4; ++k)
		b.push_back((BYTE)(v >> (8 * k)));
}

static void seed_Cvars(SnapFuzzSeed& s)
{
	static const char* const names[] = { "fua_csnap_localanim", "fua_csnap_rendercull", "fua_csnap_cull_far" };
	const unsigned int count = fuzz_Rand() % 4;
	s.hot.push_back((unsigned int)s.bytes.size());					// count
	s.bytes.push_back((BYTE)count);
	s.bytes.push_back(0);
	for (unsigned int c = 0; c < count; ++c)
	{
		const char* name = names[fuzz_Rand() % 3];
		s.hot.push_back((unsigned int)s.bytes.size());	// name length
		s.bytes.push_back((BYTE)strlen(name));
		s.bytes.insert(s.bytes.end(), (const BYTE*)name, (const BYTE*)name + strlen(name));
		seed_Put32(s.bytes, fuzz_Rand() % 8000);
	}
}

// A map's frame as both containers store it, the one FuzzFrame(bits)
// names: three origins, then three widths, which are hot.
static void seed_Frame(SnapFuzzSeed& s, unsigned int bits)
{
	const SnapFrame f = FuzzFrame(bits);
	for (unsigned int k = 0; k < 3; ++k)
		seed_Put32(s.bytes, (DWORD)f.origin[k]);
	s.hot.push_back((unsigned int)s.bytes.size());
	for (unsigned int k = 0; k < 3; ++k)
		s.bytes.push_back(f.width[k]);
}

// A .csnap: the header, its cvar block, then records of every kind.
static SnapFuzzSeed seed_Csnap()
{
	SnapFuzzSeed s;
	s.bytes.insert(s.bytes.end(), (const BYTE*)CSNAP_MAGIC, (const BYTE*)CSNAP_MAGIC + 4);
	s.hot.push_back(4);
	const BYTE version = (BYTE)(CSNAP_VERSION_MIN
		+ fuzz_Rand() % (CSNAP_VERSION - CSNAP_VERSION_MIN + 1));
	s.bytes.push_back(version);
	s.bytes.push_back(0);
	s.bytes.push_back(35);
	s.bytes.push_back(0);
	seed_Put32(s.bytes, 0);
	seed_Put32(s.bytes, fuzz_Rand() % 8);
	seed_Cvars(s);
	// From v18 the map record may carry wide axes, and its payloads are written
	// in that frame; the map comes first, so they are read in it.
	const unsigned int bits = (version >= 18) ? fuzz_Rand() % 8 : 0;
	const unsigned int records = 2 + fuzz_Rand() % 6;
	for (unsigned int r = 0; r < records; ++r)
	{
		const BYTE kind = (r == 0) ? (BYTE)CSNAP_MAP : (BYTE)(CSNAP_DELTA + fuzz_Rand() % 5);
		s.hot.push_back((unsigned int)s.bytes.size());
		s.bytes.push_back(kind);
		if (kind == CSNAP_DELTA)
		{
			const SnapFuzzSeed pay = FuzzSeedPayload(g_rng, bits, g_anyIndex);
			for (unsigned int k = 0; k < 3; ++k)
				seed_Put32(s.bytes, fuzz_Rand());
			s.hot.push_back((unsigned int)s.bytes.size() + 3);	// size, high
			seed_Put32(s.bytes, (DWORD)pay.bytes.size());
			s.bytes.insert(s.bytes.end(), pay.bytes.begin(), pay.bytes.end());
		}
		else if (kind == CSNAP_FRAME)
		{
			// The time, the pose's x/y/z (64-bit from v17), angle and pitch.
			const unsigned int words = (version >= 17) ? 9 : 6;
			for (unsigned int k = 0; k < words; ++k)
				seed_Put32(s.bytes, fuzz_Rand());
			s.bytes.push_back(1);
			const bool bChanged = (fuzz_Rand() & 1) != 0;
			s.hot.push_back((unsigned int)s.bytes.size());	// changed
			s.bytes.push_back(bChanged ? 1 : 0);
			if (bChanged)
				seed_Cvars(s);
		}
		else if (kind == CSNAP_RESET)
		{
			seed_Put32(s.bytes, fuzz_Rand());
			const BYTE len = (BYTE)(fuzz_Rand() % 64);
			s.hot.push_back((unsigned int)s.bytes.size());	// reason length
			s.bytes.push_back(len);
			for (unsigned int k = 0; k < len; ++k)
				s.bytes.push_back((BYTE)('a' + fuzz_Rand() % 26));
		}
		else if (kind == CSNAP_MIRROR)
		{
			for (unsigned int k = 0; k < 8; ++k)
				s.bytes.push_back((BYTE)fuzz_Rand());
		}
		else
		{
			const BYTE len = (BYTE)(1 + fuzz_Rand() % 8);
			s.hot.push_back((unsigned int)s.bytes.size());	// name length
			s.bytes.push_back(len);
			for (unsigned int k = 0; k < len + 16u; ++k)
				s.bytes.push_back((BYTE)fuzz_Rand());
			if (version >= 17)
				seed_Frame(s, bits);
		}
	}
	return s;
}

#ifdef SNAP_FUZZ_ZSNAP
static SnapFuzzSeed seed_Zsnap()
{
	SnapFuzzSeed s;
	s.bytes.insert(s.bytes.end(), (const BYTE*)"ZSNP", (const BYTE*)"ZSNP" + 4);
	const BYTE version = (BYTE)(ZSNAP_VERSION - fuzz_Rand() % 3);
	s.bytes.push_back(version);
	s.bytes.push_back(0);
	s.bytes.push_back(35);
	s.bytes.push_back(0);
	seed_Put32(s.bytes, 1);
	seed_Put32(s.bytes, 0);
	s.hot.push_back(4);
	// A v34 header may carry wide axes; v33 was only ever narrow.
	const unsigned int bits = (version >= 34) ? fuzz_Rand() % 8 : 0;
	if (ZsnapHeaderBytes(version) > ZSNAP_HEADER_SIZE)
		seed_Frame(s, bits);
	const unsigned int records = 1 + fuzz_Rand() % 3;
	for (unsigned int r = 0; r < records; ++r)
	{
		const SnapFuzzSeed pay = FuzzSeedPayload(g_rng, bits, g_anyIndex);
		Bytes stored = pay.bytes;
		if (fuzz_Rand() & 1)
		{
			stored.resize(ZSTD_compressBound(pay.bytes.size()));
			const size_t z = ZSTD_compress(&stored[0], stored.size(), &pay.bytes[0],
				pay.bytes.size(), 1);
			stored.resize(ZSTD_isError(z) ? 0 : z);
			if (stored.empty())
				stored = pay.bytes;
		}
		const unsigned int at = (unsigned int)s.bytes.size();
		seed_Put32(s.bytes, fuzz_Rand());
		seed_Put32(s.bytes, fuzz_Rand());
		s.bytes.push_back((BYTE)fuzz_Rand());
		s.bytes.push_back(0);
		seed_Put32(s.bytes, (DWORD)pay.bytes.size());
		seed_Put32(s.bytes, (DWORD)stored.size());
		s.hot.push_back(at + 10);	// rawSize
		s.hot.push_back(at + 14);	// storedSize
		s.hot.push_back(at + 17);	// its top byte
		s.bytes.insert(s.bytes.end(), stored.begin(), stored.end());
	}
	return s;
}
#endif

#ifdef SNAP_FUZZ_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, size_t size)
{
	if (g_csnapPath[0] == '\0')
		snprintf(g_csnapPath, sizeof(g_csnapPath), "snap_fuzz_%d.csnap", (int)fuzz_getpid());
#ifdef SNAP_FUZZ_ZSNAP
	if (g_zsnapPath[0] == '\0')
		snprintf(g_zsnapPath, sizeof(g_zsnapPath), "snap_fuzz_%d.zsnap", (int)fuzz_getpid());
#endif
	fuzz_one(data, size);
	return 0;
}
#else

//*****************************************************************************
//	WIDE REFERENCE

// The seed the wide reference is drawn from, fixed so the file is the same
// on every run of one build.
#define WIDE_REFERENCE_SEED	0x5EED0285u

// A .csnap for the reference set: a map in each of FuzzFrame's eight frames,
// each with payloads written in it and the other record kinds between.
static bool wide_Reference(const char* pszPath)
{
	g_rng.s = WIDE_REFERENCE_SEED;
	Bytes b;
	b.insert(b.end(), (const BYTE*)CSNAP_MAGIC, (const BYTE*)CSNAP_MAGIC + 4);
	b.push_back((BYTE)CSNAP_VERSION);
	b.push_back((BYTE)(CSNAP_VERSION >> 8));
	b.push_back(35);
	b.push_back(0);
	seed_Put32(b, 0);
	seed_Put32(b, 0);
	SnapFuzzSeed cvars;
	seed_Cvars(cvars);
	b.insert(b.end(), cvars.bytes.begin(), cvars.bytes.end());

	DWORD serverTic = 100, gameTic = 100, ms = 1000;
	for (unsigned int bits = 0; bits < 8; ++bits)
	{
		if (bits > 0)
		{
			static const char reason[] = "map change";
			b.push_back((BYTE)CSNAP_RESET);
			seed_Put32(b, ms);
			b.push_back((BYTE)(sizeof(reason) - 1));
			b.insert(b.end(), (const BYTE*)reason, (const BYTE*)reason + sizeof(reason) - 1);
		}
		char name[8];
		snprintf(name, sizeof(name), "WIDE%u", bits);
		b.push_back((BYTE)CSNAP_MAP);
		b.push_back((BYTE)strlen(name));
		b.insert(b.end(), (const BYTE*)name, (const BYTE*)name + strlen(name));
		for (unsigned int k = 0; k < 16; ++k)
			b.push_back((BYTE)fuzz_Rand());
		SnapFuzzSeed frame;
		seed_Frame(frame, bits);
		b.insert(b.end(), frame.bytes.begin(), frame.bytes.end());

		for (unsigned int d = 0; d < 48; ++d)
		{
			const SnapFuzzSeed pay = FuzzSeedPayload(g_rng, bits, g_anyIndex);
			b.push_back((BYTE)CSNAP_DELTA);
			seed_Put32(b, ++serverTic);
			seed_Put32(b, ++gameTic);
			seed_Put32(b, (ms += 28));
			seed_Put32(b, (DWORD)pay.bytes.size());
			b.insert(b.end(), pay.bytes.begin(), pay.bytes.end());

			// A frame's pose stays within 32 bits, which the reader's fixed_t
			// holds on the tag and the port's holds alike.
			if ((d % 3) == 2)
			{
				b.push_back((BYTE)CSNAP_FRAME);
				seed_Put32(b, ms + 5);
				for (unsigned int k = 0; k < 3; ++k)
				{
					const long long v = (long long)(SDWORD)fuzz_Rand();
					for (unsigned int i = 0; i < 8; ++i)
						b.push_back((BYTE)((unsigned long long)v >> (8 * i)));
				}
				seed_Put32(b, fuzz_Rand());
				seed_Put32(b, fuzz_Rand());
				b.push_back(1);
				const bool bChanged = (d == 2);
				b.push_back(bChanged ? 1 : 0);
				if (bChanged)
				{
					SnapFuzzSeed changed;
					seed_Cvars(changed);
					b.insert(b.end(), changed.bytes.begin(), changed.bytes.end());
				}
			}
			if ((d % 4) == 3)
			{
				b.push_back((BYTE)CSNAP_MIRROR);
				for (unsigned int k = 0; k < 8; ++k)
					b.push_back((BYTE)fuzz_Rand());
			}
		}
	}

	FILE* f = fopen(pszPath, "wb");
	if (f == NULL)
		return false;
	const bool bOk = fwrite(&b[0], 1, b.size(), f) == b.size();
	fclose(f);
	printf("snap_fuzz: wrote the wide reference, %u bytes, to %s\n",
		(unsigned int)b.size(), pszPath);
	if (bOk == false)
		return false;

	// [Kaori-Chani] Read back what was just written, so this mode is checked the whole way
	// through: the generator, the 48.16 writers and the readers. It is deliberately not
	// compared with the committed ref-wide.csnap, which no port can reproduce byte for byte:
	// the generator fills its structs a byte at a time from this same stream, and a fixed_t
	// field is eight bytes here where it was four in the build that wrote that file, so every
	// draw from the first mover record on differs. The committed copy stays the reference, and
	// the decode test is what holds it to its dump.
	CsnapReader reader;
	if (CsnapOpen(reader, pszPath) == false)
	{
		printf("snap_fuzz: the wide reference did not open: %s\n", reader.error);
		CsnapClose(reader);
		return false;
	}
	ESnapReadResult res;
	unsigned int records = 0;
	while ((res = CsnapReadRecord(reader)) == SNAPREAD_OK)
		++records;
	CsnapClose(reader);
	if (res != SNAPREAD_END)
	{
		printf("snap_fuzz: the wide reference failed to decode at record %u (%s)\n",
			records, (res == SNAPREAD_TRUNCATED) ? "truncated" : "container");
		return false;
	}
	printf("snap_fuzz: the wide reference decodes, %u records\n", records);
	return true;
}

// snap_fuzz [inputs] [seed] [corpus-dir]: with a directory, writes the seeds
// there for a libFuzzer build to start from and exits.
int main(int argc, char** argv)
{
	// --wide-reference <out.csnap> writes the wide reference and exits.
	if ((argc == 3) && (strcmp(argv[1], "--wide-reference") == 0))
		return wide_Reference(argv[2]) ? 0 : 2;
	const unsigned long inputs = (argc > 1) ? strtoul(argv[1], NULL, 0) : 200000ul;
	g_rng.s = (argc > 2) ? (DWORD)strtoul(argv[2], NULL, 0) : 0x9E3779B9u;
	const DWORD seed = g_rng.s;
	if (seed == 0)
	{
		printf("snap_fuzz: the seed must not be 0, which the generator never leaves\n");
		return 2;
	}
	snprintf(g_csnapPath, sizeof(g_csnapPath), "snap_fuzz_%d.csnap", (int)fuzz_getpid());
#ifdef SNAP_FUZZ_ZSNAP
	snprintf(g_zsnapPath, sizeof(g_zsnapPath), "snap_fuzz_%d.zsnap", (int)fuzz_getpid());
	const unsigned int targets = 4;
#else
	const unsigned int targets = 3;
#endif

	std::vector<SnapFuzzSeed> seeds;
	for (unsigned int k = 0; k < 64; ++k)
	{
		const unsigned int t = k % targets;
		// Every other round of targets in a frame with wide axes, which cycle.
		const unsigned int round = k / targets;
		const unsigned int bits = (round & 1) ? (1 + (round >> 1) % 7) : 0;
		SnapFuzzSeed s = (t == 0) ? FuzzSeedPayload(g_rng, bits, g_anyIndex)
			: (t == 1) ? FuzzSeedCosmetic(g_rng, bits) : (t == 2) ? seed_Csnap()
#ifdef SNAP_FUZZ_ZSNAP
			: seed_Zsnap();
#else
			: FuzzSeedPayload(g_rng, bits, g_anyIndex);
#endif
		const BYTE sel = (BYTE)(((t == 0) ? (fuzz_Rand() % 4) : (t == 1) ? 4 : (t == 2) ? 7 : 6)
			| (s.frameBits << 4));
		s.bytes.insert(s.bytes.begin(), sel);
		for (unsigned int h = 0; h < s.hot.size(); ++h)
			++s.hot[h];
		seeds.push_back(s);
	}

	if (argc > 3)
	{
		for (unsigned int k = 0; k < seeds.size(); ++k)
		{
			char path[512];
			snprintf(path, sizeof(path), "%s/seed%02u", argv[3], k);
			FILE* f = fopen(path, "wb");
			if (f != NULL)
			{
				fwrite(&seeds[k].bytes[0], 1, seeds[k].bytes.size(), f);
				fclose(f);
			}
			// A container seed again without its selector byte, as a file the
			// offline readers open, narrow and wide.
			const unsigned int t = seeds[k].bytes[0] & 7;
			if ((t == 6) || (t == 7))
			{
				snprintf(path, sizeof(path), "%s/seed%02u.%s", argv[3], k,
					(t == 7) ? "csnap" : "zsnap");
				f = fopen(path, "wb");
				if (f != NULL)
				{
					fwrite(&seeds[k].bytes[1], 1, seeds[k].bytes.size() - 1, f);
					fclose(f);
				}
			}
		}
		printf("snap_fuzz: wrote %u seeds to %s\n", (unsigned int)seeds.size(), argv[3]);
		return 0;
	}

	for (unsigned long i = 0; i < inputs; ++i)
	{
		if ((inputs >= 10) && (i > 0) && ((i % (inputs / 10)) == 0))
		{
			printf("snap_fuzz: %lu of %lu\n", i, inputs);
			fflush(stdout);
		}
		SnapFuzzSeed s;
		if ((i % 16) == 15)
		{
			s.bytes.resize(1 + fuzz_Rand() % 200);	// raw random bytes
			FuzzFill(g_rng, &s.bytes[0], s.bytes.size());
		}
		else
		{
			s = seeds[fuzz_Rand() % seeds.size()];
			FuzzMutate(g_rng, s);
			if (s.bytes.empty())
				continue;
		}
		fuzz_one(&s.bytes[0], s.bytes.size());
	}
	remove(g_csnapPath);
#ifdef SNAP_FUZZ_ZSNAP
	remove(g_zsnapPath);
#endif

	printf("snap_fuzz: PASS (%lu inputs, seed 0x%08x)\nreached:", inputs, (unsigned int)seed);
	for (unsigned int r = 0; r < R_COUNT; ++r)
		printf("%s %s %llu", (r == 0) ? "" : ",", g_reachName[r], g_reach[r]);
	printf("\n");
	for (unsigned int k = 0; g_known[k] != NULL; ++k)
		if (g_knownHits[k] > 0)
			printf("known: %s x%u\n", g_known[k], g_knownHits[k]);
	return 0;
}
#endif
