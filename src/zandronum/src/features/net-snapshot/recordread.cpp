//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: recordread.cpp
//
// Description: The .csnap reader, declared in record.h. Engine-free, so
// snap_tools/formattest's fuzzer can build it.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "features/net-snapshot/record.h"

using namespace zx::netsnapshot;

//*****************************************************************************
//	READING

static bool clrec_ReadExact(CsnapReader& r, void* pDst, size_t n)
{
	return (fread(pDst, 1, n, r.file) == n);
}

// Whether n more bytes remain in the file; true where its size is unknown.
static bool clrec_Fits(CsnapReader& r, unsigned int n)
{
	if (r.fileEnd < 0)
		return true;
	const long here = ftell(r.file);
	return (here < 0) || ((unsigned long)n <= (unsigned long)(r.fileEnd - here));
}

static bool clrec_Read16(CsnapReader& r, WORD& out)
{
	return clrec_ReadExact(r, &out, 2);
}

static bool clrec_Read32(CsnapReader& r, DWORD& out)
{
	return clrec_ReadExact(r, &out, 4);
}

// A FRAME pose coordinate: 64-bit from v17, 32-bit and sign-extended before.
static bool clrec_ReadPose(CsnapReader& r, fixed_t& out)
{
	if (r.version < 17)
	{
		DWORD v = 0;
		if (clrec_Read32(r, v) == false)
			return false;
		out = (fixed_t)(SDWORD)v;
		return true;
	}
	QWORD v = 0;
	if (clrec_ReadExact(r, &v, 8) == false)
		return false;
	out = (fixed_t)(SQWORD)v;
	return true;
}

static bool clrec_ReadCvarBlock(CsnapReader& r)
{
	WORD n = 0;
	if (clrec_ReadExact(r, &n, 2) == false)
		return false;
	// An entry is at least 5 bytes, so a count the file cannot hold is refused
	// before the block is allocated.
	if (clrec_Fits(r, n * 5u) == false)
		return false;

	delete[] r.cvars;
	r.cvars = new CsnapCvar[(n > 0) ? n : 1];
	r.cvarCount = n;

	for (unsigned int i = 0; i < n; ++i)
	{
		BYTE len = 0;
		if (clrec_ReadExact(r, &len, 1) == false)
			return false;
		if (len >= sizeof(r.cvars[i].name))
			return false;
		if ((len > 0) && (clrec_ReadExact(r, r.cvars[i].name, len) == false))
			return false;
		r.cvars[i].name[len] = '\0';
		DWORD v = 0;
		if (clrec_Read32(r, v) == false)
			return false;
		r.cvars[i].value = (int)v;
	}
	return true;
}

bool zx::netsnapshot::CsnapOpen(CsnapReader& reader, const char* pszFilename)
{
	memset(&reader, 0, sizeof(reader));
	reader.mapFrame = FrameNarrow();
	reader.file = fopen(pszFilename, "rb");
	if (reader.file == NULL)
	{
		reader.error = "cannot open file";
		return false;
	}
	// Bounds what a record may claim, the check skipped where ftell fails.
	reader.fileEnd = (fseek(reader.file, 0, SEEK_END) == 0) ? ftell(reader.file) : -1;
	rewind(reader.file);

	char magic[4];
	WORD version = 0, ticrate = 0;
	DWORD flags = 0;
	if ((clrec_ReadExact(reader, magic, 4) == false) || (memcmp(magic, CSNAP_MAGIC, 4) != 0))
	{
		reader.error = "not a .csnap file";
		return false;
	}
	if ((clrec_ReadExact(reader, &version, 2) == false)
		|| (clrec_ReadExact(reader, &ticrate, 2) == false)
		|| (clrec_ReadExact(reader, &flags, 4) == false))
	{
		reader.error = "truncated header";
		return false;
	}
	// Checked BEFORE the recording player's number is read, so a file of
	// another layout is refused before a field is read out of its first record.
	if ((version < CSNAP_VERSION_MIN) || (version > CSNAP_VERSION))
	{
		reader.error = "unsupported version";
		return false;
	}
	DWORD localPlayer = 0;
	if (clrec_ReadExact(reader, &localPlayer, 4) == false)
	{
		reader.error = "truncated header";
		return false;
	}
	reader.version = version;
	reader.ticrate = ticrate;
	reader.flags = flags;
	reader.localPlayer = (int)localPlayer;

	if (clrec_ReadCvarBlock(reader) == false)
	{
		reader.error = "truncated cvar block";
		return false;
	}
	return true;
}

ESnapReadResult zx::netsnapshot::CsnapReadRecord(CsnapReader& reader)
{
	BYTE kind = 0;
	if (clrec_ReadExact(reader, &kind, 1) == false)
		return SNAPREAD_END;

	++reader.recordIndex;
	reader.cvarsChanged = false;
	reader.payload = NULL;
	reader.payloadSize = 0;

	if (kind == CSNAP_DELTA)
	{
		DWORD tic = 0, gtic = 0, ms = 0, size = 0;
		if ((clrec_Read32(reader, tic) == false) || (clrec_Read32(reader, gtic) == false)
			|| (clrec_Read32(reader, ms) == false)
			|| (clrec_Read32(reader, size) == false))
			return SNAPREAD_TRUNCATED;

		if (size > (1u << 22))
		{
			reader.error = "implausible payload size";
			return SNAPREAD_ERROR;
		}
		if (clrec_Fits(reader, size) == false)
			return SNAPREAD_TRUNCATED;
		if (size > reader.storedCap)
		{
			delete[] reader.stored;
			reader.stored = new BYTE[size];
			reader.storedCap = size;
		}
		if ((size > 0) && (clrec_ReadExact(reader, reader.stored, size) == false))
			return SNAPREAD_TRUNCATED;

		reader.kind = CSNAP_DELTA;
		reader.serverTic = (int)tic;
		reader.gameTic = (int)gtic;
		reader.timeMS = ms;
		reader.payload = reader.stored;
		reader.payloadSize = size;
		return SNAPREAD_OK;
	}

	if (kind == CSNAP_FRAME)
	{
		DWORD ms = 0, ang = 0, pitch = 0;
		fixed_t x = 0, y = 0, z = 0;
		BYTE valid = 0, changed = 0;
		if ((clrec_Read32(reader, ms) == false) || (clrec_ReadPose(reader, x) == false)
			|| (clrec_ReadPose(reader, y) == false) || (clrec_ReadPose(reader, z) == false)
			|| (clrec_Read32(reader, ang) == false) || (clrec_Read32(reader, pitch) == false)
			|| (clrec_ReadExact(reader, &valid, 1) == false)
			|| (clrec_ReadExact(reader, &changed, 1) == false))
			return SNAPREAD_TRUNCATED;

		reader.kind = CSNAP_FRAME;
		reader.timeMS = ms;
		reader.pose.x = x;
		reader.pose.y = y;
		reader.pose.z = z;
		reader.pose.angle = (angle_t)ang;
		reader.pose.pitch = (int)pitch;
		reader.pose.valid = (valid != 0);
		if (changed)
		{
			if (clrec_ReadCvarBlock(reader) == false)
				return SNAPREAD_TRUNCATED;
			reader.cvarsChanged = true;
		}
		return SNAPREAD_OK;
	}

	if (kind == CSNAP_RESET)
	{
		DWORD ms = 0;
		BYTE len = 0;
		if ((clrec_Read32(reader, ms) == false)
			|| (clrec_ReadExact(reader, &len, 1) == false))
			return SNAPREAD_TRUNCATED;
		// The reason is skipped, so its buffer holds any length a byte can say.
		char scratch[255];
		if ((len > 0) && (clrec_ReadExact(reader, scratch, len) == false))
			return SNAPREAD_TRUNCATED;
		reader.kind = CSNAP_RESET;
		reader.timeMS = ms;
		return SNAPREAD_OK;
	}

	if (kind == CSNAP_MIRROR)
	{
		WORD id = 0, qstate = 0, sprite = 0;
		BYTE frame = 0, tics = 0;
		if ((clrec_Read16(reader, id) == false)
			|| (clrec_Read16(reader, qstate) == false)
			|| (clrec_Read16(reader, sprite) == false)
			|| (clrec_ReadExact(reader, &frame, 1) == false)
			|| (clrec_ReadExact(reader, &tics, 1) == false))
			return SNAPREAD_TRUNCATED;
		reader.kind = CSNAP_MIRROR;
		reader.mirrorId = id;
		reader.mirrorQState = qstate;
		reader.mirrorSprite = sprite;
		reader.mirrorFrame = frame;
		reader.mirrorTics = tics;
		return SNAPREAD_OK;
	}

	if (kind == CSNAP_MAP)
	{
		BYTE len = 0;
		if (clrec_ReadExact(reader, &len, 1) == false)
			return SNAPREAD_TRUNCATED;
		if (len >= sizeof(reader.mapName))
		{
			reader.error = "map name too long";
			return SNAPREAD_ERROR;
		}
		if (((len > 0) && (clrec_ReadExact(reader, reader.mapName, len) == false))
			|| (clrec_ReadExact(reader, reader.mapChecksum, 16) == false))
			return SNAPREAD_TRUNCATED;
		reader.mapName[len] = '\0';
		reader.mapFrame = FrameNarrow();
		if (reader.version >= 17)
		{
			DWORD origin[3];
			BYTE width[3];
			if ((clrec_Read32(reader, origin[0]) == false)
				|| (clrec_Read32(reader, origin[1]) == false)
				|| (clrec_Read32(reader, origin[2]) == false)
				|| (clrec_ReadExact(reader, width, 3) == false))
				return SNAPREAD_TRUNCATED;
			// A width with no encoding would misparse every payload after it, and
			// a v17 file was only ever written narrow.
			const unsigned int widest = (reader.version >= 18)
				? SNAP_FRAME_WIDEST : SNAP_FRAME_NARROW;
			if (!FrameWidthKnown(width[0]) || !FrameWidthKnown(width[1])
				|| !FrameWidthKnown(width[2]) || (width[0] > widest)
				|| (width[1] > widest) || (width[2] > widest))
			{
				reader.error = "unsupported frame width";
				return SNAPREAD_ERROR;
			}
			for (unsigned int k = 0; k < 3; ++k)
			{
				reader.mapFrame.origin[k] = (int)(SDWORD)origin[k];
				reader.mapFrame.width[k] = width[k];
			}
		}
		reader.kind = CSNAP_MAP;
		return SNAPREAD_OK;
	}

	reader.error = "unknown record kind";
	return SNAPREAD_ERROR;
}

void zx::netsnapshot::CsnapClose(CsnapReader& reader)
{
	if (reader.file != NULL)
		fclose(reader.file);
	delete[] reader.stored;
	delete[] reader.cvars;
	memset(&reader, 0, sizeof(reader));
}
