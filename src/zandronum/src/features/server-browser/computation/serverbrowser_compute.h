// [rc4l] Pure presentation logic for the server browser, engine-free so it is unit-tested off-engine.
//
// The old browser drew a list and nothing else, so "no servers" looked identical whether it was still
// querying, had queried and heard nothing back, or genuinely had nothing to show. That silence was
// the single worst thing about it -- a player could not tell a broken network from an empty one.
// Everything here exists to keep those cases apart.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_SERVERBROWSER_COMPUTE_H
#define ZX_SERVERBROWSER_COMPUTE_H

namespace zx
{

// How many servers are in each state right now. Filled by walking the browser's list.
struct BrowserCounts
{
	int waiting;      // asked, still inside the reply window
	int active;       // answered, drawable
	int timedOut;     // asked, gave up -- listed somewhere but unreachable from here
	int badResponse;  // answered with something we could not parse
};

// What the screen should be saying.
enum class BrowserPhase
{
	Loading,   // still finding or querying; a count of nothing means nothing yet
	Empty,     // everything that was going to answer has answered, and there is nothing to show
	Ready,     // at least one server is drawable
};

// Ready wins over Loading: a list that is still filling in should show what it already has rather
// than a spinner, because the first server usually arrives long before the last one gives up.
BrowserPhase ComputeBrowserPhase( bool waitingForRegistry, const BrowserCounts &counts );

// True when a "still working" hint belongs on screen ALONGSIDE results -- the phase alone cannot say
// this, since Ready and "three servers yet to answer" are both true at once.
bool ComputeShowsProgress( bool waitingForRegistry, const BrowserCounts &counts );

// Spinner frame for a given tic. frameCount and ticsPerFrame must be positive; nonsense input pins
// to frame 0 rather than dividing by zero, because a spinner is never worth a crash.
int ComputeSpinnerFrame( int tic, int frameCount, int ticsPerFrame );

// Ping quality, for colouring. Thresholds are the usual ones players already read as
// green/yellow/red elsewhere.
enum class PingBucket
{
	Good,      // < 80
	Fair,      // < 160
	Poor,      // >= 160
	Unknown,   // negative: not measured yet
};

PingBucket ComputePingBucket( int ping );

// The slice of rows to draw.
struct RowWindow
{
	int first;   // index of the first visible row
	int count;   // how many are visible
};

// Scroll only as far as needed to keep `selected` on screen, preserving `currentFirst` otherwise.
// Anchoring to the selection instead of recentring every frame is what stops the list jumping around
// while results stream in underneath it.
RowWindow ComputeRowWindow( int total, int perPage, int selected, int currentFirst );

// A scroll position, made safe against a list that is not the size it was when the position was
// taken. Two things produce a stale position: a remembered per-tab scroll being restored, and
// servers dying underneath a list that is already open. Both land here.
//
// The dangerous direction is SHRINKING. A browser scrolled to row 40 of 60 whose servers time out
// down to 6 must not keep drawing from row 40 -- there is nothing there, and the row indices are fed
// straight back into hit-testing, so a stale first row aims clicks at rows that do not exist.
// Anything past the end pins to the last full page; a list that fits entirely on screen pins to 0,
// which is also what makes the scrollbar's disappearance harmless.
int ComputeRestoredScroll( int remembered, int total, int perPage );

// Keep a selection valid as the list grows and shrinks under it. Returns -1 for an empty list.
int ComputeClampedSelection( int selected, int total );

} // namespace zx

#endif
