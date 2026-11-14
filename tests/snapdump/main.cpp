//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: main.cpp
//
// Description: snap_dump, the command line over the shared dumper: writes any
// .zsnap or .csnap as text, not only the three references the test holds.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "snapdump.h"

// [Kaori-Chani] The reference test decodes the three recordings it knows the digests of. This is
// the same decode over any recording, which is what makes a new reference dump after a deliberate
// format change, and what reads a recording when a later layer goes wrong. Both call
// SnapDumpWrite, so a dump taken by hand is the dump the test would have taken.
int main(int argc, char** argv)
{
	if ((argc < 2) || (argc > 3) || (strcmp(argv[1], "--help") == 0))
	{
		fprintf(stderr,
			"snap_dump: writes a .zsnap or .csnap recording as text.\n"
			"\n"
			"  snap_dump <recording> [out.txt]\n"
			"\n"
			"The container is told from the file's own magic. Without out.txt the dump goes to\n"
			"standard output.\n"
			"\n"
			"Exit status: 0 the whole recording read with no error; 1 the bytes could not be got\n"
			"at all, or these arguments were wrong; 2 the bytes were read and are wrong, which\n"
			"means no known container, a header that would not open, or a record that did not\n"
			"decode. At 2 the dump written so far names the failure.\n");
		// [Kaori-Chani] Wrong arguments share the status of a file that could not be read: in both
		// there is nothing to dump, and nothing was written.
		return (argc < 2) ? (int)SNAPDUMP_UNREADABLE : 0;
	}

	FILE* pOut = stdout;
	if (argc == 3)
	{
		pOut = fopen(argv[2], "wb");
		if (pOut == NULL)
		{
			fprintf(stderr, "snap_dump: %s: cannot be written\n", argv[2]);
			return (int)SNAPDUMP_UNREADABLE;
		}
	}

	const ESnapDumpResult res = SnapDumpWrite(pOut, argv[1]);
	if (pOut != stdout)
		fclose(pOut);

	if (res == SNAPDUMP_UNREADABLE)
		fprintf(stderr, "snap_dump: %s: cannot be read\n", argv[1]);
	else if (res == SNAPDUMP_BAD)
		fprintf(stderr, "snap_dump: %s: read, but it is not a recording this build can"
			" decode; see the dump for where it stopped\n", argv[1]);

	// [Kaori-Chani] The result is the exit status, rather than a mapping beside it that could be
	// changed on one side only.
	return (int)res;
}
