// [rc4l] See serverbrowser_compute.h. Pure logic only — unit-tested at 100% line coverage off-engine.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/server-browser/computation/serverbrowser_compute.h"

namespace zx
{

BrowserPhase ComputeBrowserPhase( bool waitingForRegistry, const BrowserCounts &counts )
{
	// Anything drawable wins. Waiting on stragglers is worth a hint, not a blank screen.
	if ( counts.active > 0 )
		return BrowserPhase::Ready;

	if ( waitingForRegistry || ( counts.waiting > 0 ))
		return BrowserPhase::Loading;

	// Nothing active and nothing outstanding. Servers that timed out or answered badly still count as
	// "we are done here" -- they are why the list is empty, not a reason to keep spinning.
	return BrowserPhase::Empty;
}

int ComputeSpinnerFrame( int tic, int frameCount, int ticsPerFrame )
{
	if (( frameCount <= 0 ) || ( ticsPerFrame <= 0 ))
		return 0;

	// Negative tics should not run the animation backwards into a negative index.
	if ( tic < 0 )
		tic = 0;

	return ( tic / ticsPerFrame ) % frameCount;
}

PingBucket ComputePingBucket( int ping )
{
	if ( ping < 0 )
		return PingBucket::Unknown;
	if ( ping < 80 )
		return PingBucket::Good;
	if ( ping < 160 )
		return PingBucket::Fair;
	return PingBucket::Poor;
}

RowWindow ComputeRowWindow( int total, int perPage, int selected, int currentFirst )
{
	RowWindow out;
	out.first = 0;
	out.count = 0;

	if (( total <= 0 ) || ( perPage <= 0 ))
		return out;

	int first = currentFirst;

	// A list that shrank can leave us scrolled past the end.
	const int maxFirst = ( total > perPage ) ? ( total - perPage ) : 0;
	if ( first > maxFirst )
		first = maxFirst;
	if ( first < 0 )
		first = 0;

	// Only move far enough to bring the selection back into view, and only when there is one.
	if (( selected >= 0 ) && ( selected < total ))
	{
		if ( selected < first )
			first = selected;
		else if ( selected >= ( first + perPage ))
			first = selected - perPage + 1;
	}

	out.first = first;
	out.count = ( total - first < perPage ) ? ( total - first ) : perPage;
	return out;
}

int ComputeRestoredScroll( int remembered, int total, int perPage )
{
	if (( total <= 0 ) || ( perPage <= 0 ))
		return 0;

	const int maxFirst = ( total > perPage ) ? ( total - perPage ) : 0;
	if ( remembered > maxFirst )
		return maxFirst;
	if ( remembered < 0 )
		return 0;
	return remembered;
}

int ComputeClampedSelection( int selected, int total )
{
	if ( total <= 0 )
		return -1;
	if ( selected < 0 )
		return 0;
	if ( selected >= total )
		return total - 1;
	return selected;
}

} // namespace zx
