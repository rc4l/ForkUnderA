// [rc4l] Implementation of the pure command-line collection. See argv_collect_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "argv_collect_compute.h"

#include <cstring>

namespace zx
{

CollectedArgv ComputeCollectArgv( int argc, const char *const *argv )
{
	CollectedArgv out;

	if ( argv == NULL )
		return out;

	for ( int i = 0; i < argc; ++i )
	{
		const char *const argument = argv[i];

		if (( argument == NULL ) || ( argument[0] == '\0' ))
			continue;

		if ( std::strcmp( argument, "-wad_picker_restart" ) == 0 )
		{
			out.bRestartedFromWadPicker = true;
			continue;
		}

		out.args.push_back( std::string( argument ));
	}

	return out;
}

} // namespace zx
