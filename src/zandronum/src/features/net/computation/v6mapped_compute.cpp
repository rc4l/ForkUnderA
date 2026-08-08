// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net/computation/v6mapped_compute.h"

namespace zx
{

bool IsV4MappedV6( const unsigned char *sixteen )
{
	if ( sixteen == 0 )
		return false;

	// The first ten bytes must be zero. Checked before the 0xff pair rather than after, so a real v6
	// address that happens to carry 0xff 0xff in those two positions is rejected on the prefix
	// instead of being accepted on a coincidence.
	for ( int i = 0; i < 10; ++i )
	{
		if ( sixteen[i] != 0 )
			return false;
	}

	return ( sixteen[10] == 0xff ) && ( sixteen[11] == 0xff );
}

void ExtractMappedV4( const unsigned char *sixteen, unsigned char *four )
{
	if (( sixteen == 0 ) || ( four == 0 ))
		return;

	for ( int i = 0; i < 4; ++i )
		four[i] = sixteen[12 + i];
}

} // namespace zx
