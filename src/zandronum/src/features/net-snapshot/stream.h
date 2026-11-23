// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: stream.h
//
// Description: The .zsnap reader: file framing, decompression and a
// truncated tail, used by fua_snap_verify (design doc appendix B).
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_STREAM_H
#define ZX_NETSNAPSHOT_STREAM_H

#include <stdio.h>
#include "features/net-snapshot/format.h"

enum ESnapReadResult
{
	SNAPREAD_OK,			// a record was read; payload/payloadSize are valid
	SNAPREAD_END,			// clean end of file
	SNAPREAD_TRUNCATED,		// file ends mid-record (expected of a crash tail)
	SNAPREAD_ERROR,			// corrupt: bad size, or decompression failed
};

struct SnapStreamReader
{
	FILE* file;

	// From the file header.
	unsigned int	version;
	unsigned int	ticrate;
	unsigned int	flags;
	SnapFrame		frame;			// the narrow frame at origin 0 before v33

	// Valid after SNAPREAD_OK.
	int				tic;
	unsigned int	wallMS;
	unsigned int	spanTics;
	const BYTE* payload;		// points into the reader's own buffer
	unsigned int	payloadSize;	// 0 for timing-only records
	unsigned int	recordIndex;	// 1-based, for diagnostics

	// Internals.
	BYTE* stored;
	BYTE* raw;
	unsigned int	storedCap;
	unsigned int	rawCap;
	const char* error;			// set when SNAPREAD_ERROR is returned
	long			fileEnd;		// the file's size, -1 where ftell can't say
};

namespace zx { namespace netsnapshot {

// Opens the file and validates its header, or returns false with the reason in
// reader.error.
bool ZsnapOpen(SnapStreamReader& reader, const char* pszFilename);

// Reads the next record, decompressing if needed.
ESnapReadResult ZsnapReadRecord(SnapStreamReader& reader);

void ZsnapClose(SnapStreamReader& reader);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_STREAM_H
