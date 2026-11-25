//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: i_tictime_web.cpp
//
// Description: The browser build's half of the time base's wait, compiled in
// place of i_tictime_wait.cpp. A page drives the loop one frame at a time and
// TryRunTics never waits for a tic where a main-loop driver is installed, so
// nothing calls this; blocking inside an animation frame would freeze the tab,
// which is why it stays a no-op rather than a sleep that happens not to be
// called. Its own file so that no browser condition sits in the time base
// (tools/platform-ifdef-tripwire.sh).
//
//-----------------------------------------------------------------------------


#include "i_tictime.h"

bool TIC_SleepWaits()
{
	return false;
}

void TIC_SleepUntilRaw(tictime_raw_t)
{
}
