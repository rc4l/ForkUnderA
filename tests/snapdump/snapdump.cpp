//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: snapdump.cpp
//
// Description: Writes every record of a .zsnap or .csnap as text, one line
// per container record and per entity or event record, through format.h's
// readers alone: the reference a port of the format layer must reproduce byte
// for byte. This is snap_dump's body, shared by the reference test and the
// snap_dump tool; neither has a copy of its own.
//
//-----------------------------------------------------------------------------

#include "snapdump.h"

#include <stdio.h>
#include <string.h>

#include "features/net-snapshot/format.h"
#include "features/net-snapshot/stream.h"
#include "features/net-snapshot/record.h"

using namespace zx::netsnapshot;

struct DumpTotals
{
	unsigned int records, entities, enters, updates, leaves, events, errors;
};

static FILE* g_out = NULL;
static DumpTotals g_totals;

// Positions print as exact counts of 1/65536 units, the same under 16.16 and
// 48.16, so a whole-unit position is scaled here.
static long long dump_Units(SDWORD q)
{
	return (long long)q * 65536;
}

static void dump_Raw(const BYTE* p, unsigned int n)
{
	fprintf(g_out, " raw=");
	for (unsigned int i = 0; i < n; ++i)
		fprintf(g_out, (i == 0) ? "%u" : ",%u", (unsigned int)p[i]);
}

static void dump_Frame(const SnapFrame& f)
{
	fprintf(g_out, " origin=%d,%d,%d width=%u,%u,%u", f.origin[0], f.origin[1],
		f.origin[2], f.width[0], f.width[1], f.width[2]);
}

//*****************************************************************************
//	EVENTS

static void dump_Mover(const SnapFrame& f, const BYTE* p, unsigned int n,
	unsigned int version)
{
	const unsigned int hdrBytes = MoverHeaderBytes(version);
	if (n < hdrBytes)
	{
		fprintf(g_out, " bad=1");
		dump_Raw(p, n);
		return;
	}
	BYTE cls = 0, flags = 0, gen = 0;
	WORD index = 0;
	ReadMoverHeader(p, hdrBytes, cls, flags, index, gen);
	fprintf(g_out, " class=%u flags=%u index=%u gen=%u", cls, flags, index, gen);

	// An unknown class or a length that disagrees loses this record only, as
	// in the client's event path.
	const BYTE* q = p + hdrBytes;
	const unsigned int body = MoverRecordBodyBytes(f, cls, flags, q, n - hdrBytes);
	if ((body == 0) || ((hdrBytes + body) != n))
	{
		fprintf(g_out, " bad=1");
		dump_Raw(q, n - hdrBytes);
		return;
	}
	switch (cls)
	{
	case SNAPMV_PLAT:
	{
		SnapMoverPlat s;
		ReadMoverPlat(q, s);
		fprintf(g_out, " speed=%d low=%d high=%d planed=%d wait=%d count=%d"
			" tag=%d platid=%d crush=%d status=%u oldstatus=%u movertype=%u",
			(int)s.speed, (int)s.low, (int)s.high, (int)s.planeD, s.wait, s.count,
			s.tag, s.platID, s.crush, s.status, s.oldStatus, s.type);
		break;
	}
	case SNAPMV_CEILING:
	{
		SnapMoverCeiling s;
		ReadMoverCeiling(q, s);
		fprintf(g_out, " speed=%d speed1=%d speed2=%d bottom=%d top=%d planed=%d"
			" tag=%d ceilingid=%d crush=%d movertype=%u silent=%u hexencrush=%u"
			" direction=%d olddirection=%d", (int)s.speed, (int)s.speed1,
			(int)s.speed2, (int)s.bottom, (int)s.top, (int)s.planeD, s.tag,
			s.ceilingID, s.crush, s.type, s.silent, s.hexencrush, s.direction,
			s.oldDirection);
		break;
	}
	case SNAPMV_FLOOR:
	{
		const bool bStair = (flags & SNAPMVF_STAIR) != 0;
		SnapMoverFloor s;
		ReadMoverFloor(q, s, bStair);
		fprintf(g_out, " speed=%d destdist=%d planed=%d floorid=%d crush=%d"
			" movertype=%u hexencrush=%u direction=%d", (int)s.speed, (int)s.destDist,
			(int)s.planeD, s.floorID, s.crush, s.type, s.hexencrush, s.direction);
		if (bStair)
			fprintf(g_out, " orgdist=%d resetcount=%d delay=%d pausetime=%d"
				" steptime=%d persteptime=%d", (int)s.orgDist, s.resetCount, s.delay,
				s.pauseTime, s.stepTime, s.perStepTime);
		break;
	}
	case SNAPMV_DOOR:
	{
		SnapMoverDoor s;
		ReadMoverDoor(q, s);
		fprintf(g_out, " speed=%d topdist=%d botdist=%d oldfloordist=%d planed=%d"
			" topwait=%d topcountdown=%d botspot=%d lighttag=%d doorid=%d"
			" movertype=%u direction=%d", (int)s.speed, (int)s.topDist,
			(int)s.botDist, (int)s.oldFloorDist, (int)s.planeD, s.topWait,
			s.topCountdown, s.botSpot,
			s.lightTag, s.doorID, s.type, s.direction);
		break;
	}
	case SNAPMV_ELEVATOR:
	{
		SnapMoverElevator s;
		ReadMoverElevator(q, s);
		fprintf(g_out, " speed=%d floordestdist=%d ceilingdestdist=%d"
			" floorplaned=%d ceilingplaned=%d elevatorid=%d movertype=%u direction=%d",
			(int)s.speed, (int)s.floorDestDist, (int)s.ceilingDestDist,
			(int)s.floorPlaneD, (int)s.ceilingPlaneD, s.elevatorID, s.type,
			s.direction);
		break;
	}
	case SNAPMV_PILLAR:
	{
		SnapMoverPillar s;
		ReadMoverPillar(q, s);
		fprintf(g_out, " floorspeed=%d ceilingspeed=%d floortarget=%d"
			" ceilingtarget=%d floorplaned=%d ceilingplaned=%d pillarid=%d"
			" crush=%d movertype=%u hexencrush=%u blocked=%u", (int)s.floorSpeed,
			(int)s.ceilingSpeed, (int)s.floorTarget, (int)s.ceilingTarget,
			(int)s.floorPlaneD, (int)s.ceilingPlaneD, s.pillarID, s.crush, s.type,
			s.hexencrush, s.blocked);
		break;
	}
	case SNAPMV_POLY:
	{
		SnapMoverPoly s;
		if (ReadMoverPoly(q, f, body, s) == false)
		{
			fprintf(g_out, " bad=1");
			dump_Raw(q, body);
			break;
		}
		fprintf(g_out, " x=%lld y=%lld angle=%u count=%u", (long long)s.x,
			(long long)s.y, (unsigned int)s.angle, (unsigned int)s.count);

		// Each action is numbered and prints every field in one order, those
		// its kind does not carry reading as 0.
		for (unsigned int i = 0; i < s.count; ++i)
		{
			const SnapPolyEntry& e = s.e[i];
			fprintf(g_out, " entry%u=%u,%u,%d,%d,%d,%d,%d,%d,%lld,%lld,%d,%d,%d,%u",
				i, e.kind, e.gen, e.speed, e.dist, e.angle, e.direction, (int)e.xSpeed,
				(int)e.ySpeed, (long long)e.xTarget, (long long)e.yTarget,
				e.totalDist, e.tics, e.waitTics, e.close);
		}
		break;
	}
	default:
		fprintf(g_out, " bad=1");
		dump_Raw(q, body);
		break;
	}
}

// Set per run by SnapDumpWrite: in tag mode every self-describing type this build has
// learned since the reference tag is skipped rather than decoded.
static bool g_tagTypesOnly = false;

// A float's bits, so a dump compares the same on every platform.
static unsigned int dump_FloatBits(float v)
{
	unsigned int bits = 0;
	memcpy(&bits, &v, 4);
	return bits;
}

static void dump_Event(const SnapFrame& f, BYTE type, const BYTE* p,
	unsigned int n, unsigned int version)
{
	switch (type)
	{
	case SNAPEV_BRANCH:
	{
		SnapEntityState e;
		memset(&e, 0, sizeof(e));
		const WORD tic = ReadEvent(p, f, type, e);
		fprintf(g_out, " etic=%u state=%u tics=%u x=%lld y=%lld z=%lld",
			(unsigned int)tic, (unsigned int)e.qstate, (unsigned int)e.qtics,
			dump_Units(e.qx), dump_Units(e.qy), dump_Units(e.qz));
		break;
	}
	case SNAPEV_PROPS:
	{
		SnapProps s;
		if (ReadProps(p, n, s) == false)
		{
			fprintf(g_out, " props=bad");
			dump_Raw(p, n);
			break;
		}
		fprintf(g_out, " propflags=%u", (unsigned int)s.flags);
		if (s.flags & SNAP_PROPS_SCALE)
			fprintf(g_out, " scalex=%u scaley=%u", (unsigned int)s.scaleX,
				(unsigned int)s.scaleY);
		if (s.flags & SNAP_PROPS_ROLL)
			fprintf(g_out, " roll=%u", (unsigned int)s.roll);
		if (s.flags & SNAP_PROPS_MATERIAL)
		{
			// Printable bytes only: a body from the fuzzer or the wide reference
			// carries anything, and a newline in it would break a line in two.
			char name[SNAP_PROPS_MATNAME_MAX + 1];
			unsigned int k = 0;
			for (; k < s.matLen; ++k)
				name[k] = ((s.material[k] >= 32) && (s.material[k] < 127))
					? s.material[k] : '?';
			name[k] = '\0';
			fprintf(g_out, " matlen=%u material=%s", (unsigned int)s.matLen, name);
		}
		if (s.flags & SNAP_PROPS_UNIFORMS)
		{
			fprintf(g_out, " uniset=%u", (unsigned int)s.uniSet);
			for (unsigned int u = 0; u < SNAP_PROPS_UNIFORM_SLOTS; ++u)
			{
				if ((s.uniSet & (1u << u)) == 0)
					continue;
				// As the bits, not as text: a float printed by a C library is not
				// the same string on every platform, and these dumps are compared.
				fprintf(g_out, " u%u=%08x,%08x,%08x,%08x", u,
					dump_FloatBits(s.uni[u][0]), dump_FloatBits(s.uni[u][1]),
					dump_FloatBits(s.uni[u][2]), dump_FloatBits(s.uni[u][3]));
			}
		}
		break;
	}
	case SNAPEV_PSPRITE:
	{
		SnapPSpriteState s;
		ReadPSprite(p, s);
		fprintf(g_out, " weapon=%u state0=%u tics0=%u state1=%u tics1=%u",
			(unsigned int)s.weaponClass, (unsigned int)s.qstate[0],
			(unsigned int)s.qtics[0], (unsigned int)s.qstate[1],
			(unsigned int)s.qtics[1]);
		break;
	}
	case SNAPEV_LOCAL:
	{
		SnapLocalState s;
		ReadLocal(p, f, s);
		fprintf(g_out, " flags=%u clienttic=%u x=%lld y=%lld z=%lld vx=%d vy=%d"
			" vz=%d jumptics=%d", (unsigned int)s.flags, (unsigned int)s.clientTic,
			(long long)s.x, (long long)s.y, (long long)s.z, (int)s.velx,
			(int)s.vely, (int)s.velz, (int)s.jumpTics);
		break;
	}
	case SNAPEV_MOVER:
		dump_Mover(f, p, n, version);
		break;
	default:
		dump_Raw(p, n);
		break;
	}
}

//*****************************************************************************
//	PAYLOAD WALK

static void dump_Error(const char* pszWhat, unsigned int off)
{
	fprintf(g_out, "error what=%s offset=%u\n", pszWhat, off);
	++g_totals.errors;
}

// The client's walk (ApplyDelta in cl_entities.cpp) without the ghost table,
// so every record prints what its own bytes carry and nothing carried forward.
static void dump_Walk(const SnapFrame& f, const BYTE* pData, unsigned int size,
	unsigned int rec, int tic, unsigned int version)
{
	SnapIdCursor ids;
	IdCursorReset(ids);
	unsigned int off = 0;
	while (off < size)
	{
		WORD id = 0;
		const unsigned int idBytes = ReadRecordId(pData + off, size - off, id, ids);
		if (idBytes == 0)
			return dump_Error("id", off);
		off += idBytes;
		if (off + 1 > size)
			return dump_Error("header", off);
		const BYTE rawMask = pData[off];
		off += 1;
		++g_totals.entities;
		fprintf(g_out, "ent rec=%u tic=%d id=%u", rec, tic, (unsigned int)id);

		if ((rawMask & SNAPGRP_EVENT) != 0)
		{
			const BYTE type = (BYTE)(rawMask & SNAPEV_TYPEMASK);
			unsigned int evBytes = 0;
			bool bSkip = false;
			const int evLen = EventRecordBytes(f, type, pData + off, size - off,
				evBytes, bSkip);
			fprintf(g_out, " kind=event mask=%u type=%u", (unsigned int)rawMask,
				(unsigned int)type);
			if (evLen != SNAPEVLEN_OK)
			{
				fprintf(g_out, "\n");
				return dump_Error((evLen == SNAPEVLEN_UNKNOWN) ? "eventtype"
					: "eventlength", off);
			}
			++g_totals.events;

			// A self-describing payload starts past its length byte, which is
			// counted in evBytes.
			const unsigned int lead = EventSelfDescribing(type) ? 1u : 0u;
			if (g_tagTypesOnly && EventSelfDescribing(type)
				&& ((type & SNAPEV_TYPEMASK) != SNAPEV_MOVER))
				bSkip = true;
			if (bSkip)
			{
				fprintf(g_out, " skip=1");
				dump_Raw(pData + off + lead, evBytes - lead);
			}
			else
			{
				dump_Event(f, type, pData + off + lead, evBytes - lead, version);
			}
			fprintf(g_out, "\n");
			off += evBytes;
			continue;
		}

		const BYTE mask = (BYTE)(rawMask & SNAPGRP_GROUPMASK);
		if (mask == 0)
		{
			fprintf(g_out, " kind=leave mask=%u\n", (unsigned int)rawMask);
			++g_totals.leaves;
			continue;
		}
		const bool bEnter = (rawMask & SNAPGRP_ENTER) != 0;
		fprintf(g_out, " kind=%s mask=%u", bEnter ? "enter" : "update",
			(unsigned int)rawMask);
		if (bEnter)
		{
			if (off + 2 > size)
			{
				fprintf(g_out, "\n");
				return dump_Error("class", off);
			}
			const WORD classWord = Read16(pData + off);
			off += 2;
			fprintf(g_out, " class=%u", (unsigned int)(classWord & SNAP_CLASS_MASK));
			if ((classWord & SNAP_CLASS_TARGET) != 0)
			{
				if (off + 2 > size)
				{
					fprintf(g_out, "\n");
					return dump_Error("target", off);
				}
				fprintf(g_out, " target=%u", (unsigned int)Read16(pData + off));
				off += 2;
			}
			++g_totals.enters;
		}
		else
		{
			++g_totals.updates;
		}

		unsigned int need = 0;
		if (!GroupBytesRead(f, mask, pData + off, size - off, need))
		{
			fprintf(g_out, "\n");
			return dump_Error("groups", off);
		}
		SnapEntityState st;
		memset(&st, 0, sizeof(st));
		ReadGroups(pData + off, f, st, mask);

		// Groups print in write order, each only where the mask carries it.
		if (mask & SNAPGRP_POS)
			fprintf(g_out, " x=%lld y=%lld", dump_Units(st.qx), dump_Units(st.qy));
		if (mask & SNAPGRP_POSZ)
			fprintf(g_out, " z=%lld", dump_Units(st.qz));
		if (mask & SNAPGRP_ANG)
			fprintf(g_out, " angle=%u pitch=%u", (unsigned int)st.qangle,
				(unsigned int)(st.qpitch >> 2));
		if (mask & SNAPGRP_VELMISC)
			fprintf(g_out, " vx=%d vy=%d vz=%d misc=%u effects=%u", (int)st.qvx,
				(int)st.qvy, (int)st.qvz, (unsigned int)st.misc,
				(unsigned int)st.effects);
		if (mask & SNAPGRP_APPEAR)
		{
			const unsigned int wide =
				((pData[off + GroupBytesBeforeAppear(f, mask)]
				& SNAP_APPEAR_WIDE_FLAG) != 0) ? 1u : 0u;
			fprintf(g_out, " appear=%u tics=%u state=%u", wide,
				(unsigned int)st.qtics, (unsigned int)st.qstate);
		}
		if (mask & SNAPGRP_RENDER)
			fprintf(g_out, " render=%u", (unsigned int)st.renderstate);
		fprintf(g_out, "\n");
		off += need;
	}
}

//*****************************************************************************
//	CONTAINERS

static bool dump_Zsnap(const char* pszPath)
{
	SnapStreamReader reader;
	if (ZsnapOpen(reader, pszPath) == false)
	{
		fprintf(stderr, "snap_dump: %s: %s\n", pszPath, reader.error);
		return false;
	}
	fprintf(g_out, "zsnap version=%u ticrate=%u flags=%u", reader.version,
		reader.ticrate, reader.flags);
	dump_Frame(reader.frame);
	fprintf(g_out, "\n");

	ESnapReadResult res;
	while ((res = ZsnapReadRecord(reader)) == SNAPREAD_OK)
	{
		++g_totals.records;
		fprintf(g_out, "rec index=%u tic=%d wall=%u span=%u bytes=%u\n",
			reader.recordIndex, reader.tic, reader.wallMS, reader.spanTics,
			reader.payloadSize);
		dump_Walk(reader.frame, reader.payload, reader.payloadSize,
			reader.recordIndex, reader.tic, reader.version);
	}
	if (res != SNAPREAD_END)
	{
		fprintf(g_out, "error what=%s record=%u\n",
			(res == SNAPREAD_TRUNCATED) ? "truncated" : "container",
			reader.recordIndex);
		++g_totals.errors;
	}
	ZsnapClose(reader);
	return true;
}

static void dump_Cvars(const CsnapReader& reader)
{
	for (unsigned int i = 0; i < reader.cvarCount; ++i)
		fprintf(g_out, "cvar name=%s value=%d\n", reader.cvars[i].name,
			reader.cvars[i].value);
}

static bool dump_Csnap(const char* pszPath)
{
	CsnapReader reader;
	if (CsnapOpen(reader, pszPath) == false)
	{
		fprintf(stderr, "snap_dump: %s: %s\n", pszPath, reader.error);
		CsnapClose(reader);
		return false;
	}
	fprintf(g_out, "csnap version=%u ticrate=%u flags=%u player=%d\n",
		reader.version, reader.ticrate, reader.flags, reader.localPlayer);
	dump_Cvars(reader);

	// The payload's wire version, as the replay and the fuzzer take it: the
	// mover envelope gained its generation byte in v28.
	const unsigned int version = (reader.version >= 15)
		? (unsigned int)ZSNAP_VERSION : 28u;

	ESnapReadResult res;
	while ((res = CsnapReadRecord(reader)) == SNAPREAD_OK)
	{
		++g_totals.records;
		fprintf(g_out, "rec index=%u", reader.recordIndex);
		switch (reader.kind)
		{
		case CSNAP_DELTA:
			fprintf(g_out, " kind=delta servertic=%d gametic=%d ms=%u bytes=%u\n",
				reader.serverTic, reader.gameTic, reader.timeMS,
				reader.payloadSize);
			break;
		case CSNAP_FRAME:
			fprintf(g_out, " kind=frame ms=%u x=%lld y=%lld z=%lld angle=%u"
				" pitch=%d valid=%u\n", reader.timeMS, (long long)reader.pose.x,
				(long long)reader.pose.y, (long long)reader.pose.z,
				(unsigned int)reader.pose.angle, reader.pose.pitch,
				reader.pose.valid ? 1u : 0u);
			break;
		case CSNAP_RESET:
			fprintf(g_out, " kind=reset ms=%u\n", reader.timeMS);
			break;
		case CSNAP_MIRROR:
			fprintf(g_out, " kind=mirror id=%u state=%u sprite=%u frame=%u"
				" tics=%u\n", reader.mirrorId, (unsigned int)reader.mirrorQState,
				(unsigned int)reader.mirrorSprite,
				(unsigned int)reader.mirrorFrame, (unsigned int)reader.mirrorTics);
			break;
		case CSNAP_MAP:
			fprintf(g_out, " kind=map name=%s checksum=", reader.mapName);
			for (unsigned int i = 0; i < 16; ++i)
				fprintf(g_out, "%02x", (unsigned int)reader.mapChecksum[i]);
			dump_Frame(reader.mapFrame);
			fprintf(g_out, "\n");
			break;
		default:
			fprintf(g_out, " kind=%u\n", (unsigned int)reader.kind);
			break;
		}
		if (reader.cvarsChanged)
			dump_Cvars(reader);
		if ((reader.kind == CSNAP_DELTA) && (reader.payloadSize > 0))
			dump_Walk(reader.mapFrame, reader.payload, reader.payloadSize,
				reader.recordIndex, reader.serverTic, version);
	}
	if (res != SNAPREAD_END)
	{
		fprintf(g_out, "error what=%s record=%u\n",
			(res == SNAPREAD_TRUNCATED) ? "truncated" : "container",
			reader.recordIndex);
		++g_totals.errors;
	}
	CsnapClose(reader);
	return true;
}

//*****************************************************************************
//	THE SHARED ENTRY
//
// Everything above is snap_dump's body. This is the one function the reference
// test and the snap_dump tool both call, so the container sniff, the totals and
// the closing line cannot drift between them.

ESnapDumpResult SnapDumpWrite(FILE* pOut, const char* pszIn, bool bTagTypesOnly)
{
	g_tagTypesOnly = bTagTypesOnly;
	// [Kaori-Chani] Not being able to get four bytes is the one failure about the file rather than
	// its contents, and so the only one that reads as unreadable.
	char magic[4] = { 0, 0, 0, 0 };
	FILE* f = fopen(pszIn, "rb");
	if ((f == NULL) || (fread(magic, 1, 4, f) != 4))
	{
		if (f != NULL)
			fclose(f);
		return SNAPDUMP_UNREADABLE;
	}
	fclose(f);

	g_out = pOut;
	memset(&g_totals, 0, sizeof(g_totals));

	// [Kaori-Chani] Past here the bytes were readable, so every remaining failure is the
	// recording's own: a magic neither container claims, a header that would not open, or a record
	// the walk refused. All of those are one answer, and the dump written so far names which.
	bool bWalked;
	if (memcmp(magic, "ZSNP", 4) == 0)
		bWalked = dump_Zsnap(pszIn);
	else if (memcmp(magic, CSNAP_MAGIC, 4) == 0)
		bWalked = dump_Csnap(pszIn);
	else
		bWalked = false;
	if (bWalked)
		fprintf(g_out, "end records=%u entities=%u enters=%u updates=%u leaves=%u"
			" events=%u errors=%u\n", g_totals.records, g_totals.entities,
			g_totals.enters, g_totals.updates, g_totals.leaves, g_totals.events,
			g_totals.errors);
	g_out = NULL;
	return (bWalked && (g_totals.errors == 0)) ? SNAPDUMP_OK : SNAPDUMP_BAD;
}
