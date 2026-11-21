//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: snapdump.h
//
// Description: The snapshot decode dumper, shared by the reference test and
// the snap_dump tool so the two cannot disagree about what a recording says.
//
//-----------------------------------------------------------------------------

#ifndef ZX_SNAPDUMP_H
#define ZX_SNAPDUMP_H

#include <stdio.h>

// [Kaori-Chani] How a dump ended. The values are the snap_dump tool's exit codes, which is why they
// are spelled out: main() returns this, so the exit status cannot drift from the result, and a
// script telling a mistyped path from a damaged recording reads the difference here.
enum ESnapDumpResult
{
	// Every record read, and no record reported an error.
	SNAPDUMP_OK = 0,
	// The bytes could not be got at all: no such file, or shorter than the four magic bytes.
	SNAPDUMP_UNREADABLE = 1,
	// The bytes were read and are wrong: no container this build knows, a header that would not
	// open, or a record that did not decode. A dump may have been written up to that point, with
	// the failure named in it.
	SNAPDUMP_BAD = 2,
};

// [Kaori-Chani] Writes every record of a .zsnap or .csnap to `pOut` as text, one line per container
// record and per entity or event record, through format.h's readers alone. The container is told
// from the file's own magic, so the caller does not choose a decoder.
// `bTagTypesOnly` decodes only the types the reference tag knew -- SNAPEV_MOVER the one
// self-describing one -- so a reference dump stays what the shipped one is.
ESnapDumpResult SnapDumpWrite(FILE* pOut, const char* pszIn, bool bTagTypesOnly = false);

#endif // ZX_SNAPDUMP_H
