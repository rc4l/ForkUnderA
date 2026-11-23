// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: stream.cpp
//
// Description: The .zsnap reader. See stream.h.
//
//-----------------------------------------------------------------------------


#include <string.h>
#include <stdlib.h>

#include <zstd.h>

#include "features/net-snapshot/stream.h"

using namespace zx::netsnapshot;

bool zx::netsnapshot::ZsnapOpen(SnapStreamReader& reader, const char* pszFilename)
{
	memset(&reader, 0, sizeof(reader));

	reader.file = fopen(pszFilename, "rb");
	if (reader.file == NULL)
	{
		reader.error = "couldn't open file";
		return false;
	}

	// Bounds what a record may claim before it is allocated, unchecked where
	// ftell fails (past 2 GB on some platforms).
	reader.fileEnd = (fseek(reader.file, 0, SEEK_END) == 0) ? ftell(reader.file) : -1;
	rewind(reader.file);

	BYTE hdr[ZSNAP_HEADER_SIZE];
	if ((fread(hdr, 1, ZSNAP_HEADER_SIZE, reader.file) != ZSNAP_HEADER_SIZE) ||
		(memcmp(hdr, "ZSNP", 4) != 0))
	{
		ZsnapClose(reader);
		reader.error = "not a zsnap file";
		return false;
	}

	reader.version = Read16(hdr + 4);
	reader.ticrate = Read16(hdr + 6);
	reader.flags = Read32(hdr + 8);

	if ((reader.version < ZSNAP_VERSION_MIN) || (reader.version > ZSNAP_VERSION))
	{
		ZsnapClose(reader);
		reader.error = "unsupported zsnap version";
		return false;
	}

	reader.frame = FrameNarrow();
	if (ZsnapHeaderBytes(reader.version) > ZSNAP_HEADER_SIZE)
	{
		BYTE fr[ZSNAP_HEADER_FRAME];
		if (fread(fr, 1, ZSNAP_HEADER_FRAME, reader.file) != ZSNAP_HEADER_FRAME)
		{
			ZsnapClose(reader);
			reader.error = "truncated zsnap header";
			return false;
		}
		for (unsigned int k = 0; k < 3; ++k)
		{
			reader.frame.origin[k] = (int)(SDWORD)Read32(fr + 4 * k);
			reader.frame.width[k] = fr[12 + k];
		}
		// A width with no encoding would misparse every payload, and a v33
		// file was only ever written narrow.
		const unsigned int widest = (reader.version >= 34)
			? SNAP_FRAME_WIDEST : SNAP_FRAME_NARROW;
		if (!FrameWidthKnown(fr[12]) || !FrameWidthKnown(fr[13])
			|| !FrameWidthKnown(fr[14]) || (fr[12] > widest) || (fr[13] > widest)
			|| (fr[14] > widest))
		{
			ZsnapClose(reader);
			reader.error = "unsupported zsnap frame width";
			return false;
		}
	}
	return true;
}

// Whether n more bytes remain in the file; true where its size is unknown.
static bool snapstream_Fits(SnapStreamReader& reader, unsigned int n)
{
	if (reader.fileEnd < 0)
		return true;
	const long here = ftell(reader.file);
	return (here < 0) || ((unsigned long)n <= (unsigned long)(reader.fileEnd - here));
}

ESnapReadResult zx::netsnapshot::ZsnapReadRecord(SnapStreamReader& reader)
{
	reader.payload = NULL;
	reader.payloadSize = 0;

	if (reader.file == NULL)
		return SNAPREAD_END;

	BYTE rh[ZSNAP_RECHDR_SIZE];
	const size_t got = fread(rh, 1, ZSNAP_RECHDR_SIZE, reader.file);
	if (got == 0)
		return SNAPREAD_END;
	if (got < ZSNAP_RECHDR_SIZE)
		return SNAPREAD_TRUNCATED;

	reader.tic = (int)Read32(rh);
	reader.wallMS = Read32(rh + 4);
	reader.spanTics = Read16(rh + 8);
	const unsigned int rawSize = Read32(rh + 10);
	const unsigned int storedSize = Read32(rh + 14);

	if (rawSize > ZSNAP_MAX_RAWSIZE)
	{
		reader.error = "record claims an implausible uncompressed size";
		return SNAPREAD_ERROR;
	}

	// The writer stores the smaller form, so no more than the raw limit.
	if (storedSize > ZSNAP_MAX_RAWSIZE)
	{
		reader.error = "record claims an implausible stored size";
		return SNAPREAD_ERROR;
	}
	if (snapstream_Fits(reader, storedSize) == false)
		return SNAPREAD_TRUNCATED;

	if (storedSize > reader.storedCap)
	{
		delete[] reader.stored;
		reader.storedCap = storedSize;
		reader.stored = new BYTE[reader.storedCap ? reader.storedCap : 1];
	}
	if (fread(reader.stored, 1, storedSize, reader.file) != storedSize)
		return SNAPREAD_TRUNCATED;

	++reader.recordIndex;

	if (rawSize == 0)
		return SNAPREAD_OK;			// timing-only record

	if (storedSize == rawSize)
	{
		reader.payload = reader.stored;
		reader.payloadSize = rawSize;
		return SNAPREAD_OK;
	}

	// The engine's frames record their size, so one naming another is refused
	// before its buffer is allocated; a frame with no size still decompresses.
	const unsigned long long frameSize = ZSTD_getFrameContentSize(reader.stored, storedSize);
	if ((frameSize == ZSTD_CONTENTSIZE_ERROR)
		|| ((frameSize != ZSTD_CONTENTSIZE_UNKNOWN) && (frameSize != rawSize)))
	{
		reader.error = "zstd frame unreadable, or of another size than the record";
		return SNAPREAD_ERROR;
	}

	if (rawSize > reader.rawCap)
	{
		delete[] reader.raw;
		reader.rawCap = rawSize;
		reader.raw = new BYTE[reader.rawCap];
	}
	const size_t r = ZSTD_decompress(reader.raw, rawSize, reader.stored, storedSize);
	if (ZSTD_isError(r) || (r != rawSize))
	{
		reader.error = "zstd decompression failed";
		return SNAPREAD_ERROR;
	}

	reader.payload = reader.raw;
	reader.payloadSize = rawSize;
	return SNAPREAD_OK;
}

void zx::netsnapshot::ZsnapClose(SnapStreamReader& reader)
{
	if (reader.file != NULL)
	{
		fclose(reader.file);
		reader.file = NULL;
	}
	delete[] reader.stored;
	delete[] reader.raw;
	reader.stored = NULL;
	reader.raw = NULL;
	reader.storedCap = 0;
	reader.rawCap = 0;
	reader.payload = NULL;
	reader.payloadSize = 0;
}
