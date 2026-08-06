// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/dialog_compute.h"

namespace zx
{

namespace
{
char Fold( int ch )
{
	if (( ch >= 'A' ) && ( ch <= 'Z' ))
		return static_cast<char>( ch - 'A' + 'a' );
	return static_cast<char>( ch );
}
} // namespace

int ComputeDialogFocus( int focus, int count, DialogKey key )
{
	if ( count <= 0 )
		return 0;

	// A focus that arrived out of range is normalised before it is moved, so one bad value cannot
	// keep producing bad ones.
	int at = focus;
	if (( at < 0 ) || ( at >= count ))
		at = 0;

	const int step = (( key == DialogKey::Left ) || ( key == DialogKey::Up )) ? -1 : 1;

	at += step;
	if ( at < 0 )
		at = count - 1;
	if ( at >= count )
		at = 0;

	return at;
}

int ComputeDialogShortcut( const std::vector<char> &shortcuts, int ch )
{
	const char want = Fold( ch );

	for ( size_t i = 0; i < shortcuts.size( ); ++i )
	{
		if ( shortcuts[i] == 0 )
			continue;					// a choice with no shortcut is reachable other ways
		if ( Fold( shortcuts[i] ) == want )
			return static_cast<int>( i );
	}

	return -1;
}

int ComputeDialogEscape( int cancelIndex, int count )
{
	if (( count <= 0 ) || ( cancelIndex < 0 ) || ( cancelIndex >= count ))
		return -1;

	return cancelIndex;
}

} // namespace zx
