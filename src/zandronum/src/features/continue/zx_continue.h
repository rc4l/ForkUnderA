// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Putting the player back where they left off.
//
// One button on the header bar, pinned to the left. Pressing it does not ask anything: the decision
// was made when the button chose to exist, and a confirmation on top of that is a second decision
// about the same thing.
//
// WHAT IT REMEMBERS is one of two shapes -- a server we were connected to, or an offline session
// saved to a snapshot -- and which questions have to be asked before offering it differ enough that
// they are a computation unit of their own (continueshow_compute).
//
// WHEN IT REMEMBERS is the sharper problem, and the reason none of this hangs off a shutdown hook:
// i_main.cpp registers atexit(call_terms) and I_FatalError leaves through exit(), so the atterm
// chain runs on a crash exactly as it does on a clean quit. The record is written from the
// deliberate quit instead, and from the moment a join succeeds.

#ifndef ZX_CONTINUE_H
#define ZX_CONTINUE_H

#include "features/server-hosting/computation/hostargs_compute.h"

namespace zx
{

// [rc4l] Whether the pill is currently the way OUT rather than the way in. The label and the action
// both follow from this, and it is asked rather than stored -- a remembered mode goes stale the
// first time a kick or a dying server moves the player without telling the bar.
bool Continue_IsDisconnect();

// Whether the button should be on the bar right now. Cheap enough to ask every frame: the record is
// read once and held, and the probe is whatever the browser already knows.
bool Continue_IsShown();

// The label, which names what is being continued rather than saying "Continue" twice over -- the
// server's address or the map. Never null; empty when there is nothing.
const char *Continue_Label();

// What the bar should say about it while hovered: where, specifically, it is continuing from.
// Never null; empty when there is nothing.
const char *Continue_Tooltip();

// Act on it: reload the WAD set the session needs and go. Does not return on the path that works,
// because the reload throws. With more than one thing to go back to this opens the list instead.
void Continue_Activate();

// [rc4l] The history, as the list shows it: only the rows worth offering, newest first.
//
// Indexed by POSITION IN THE LIST rather than in the underlying history, so a row that has stopped
// being usable cannot be activated by an index that used to mean something else.
int Continue_HistoryCount();
const char *Continue_EntryLabel( int index );		// the row's headline; never null
const char *Continue_EntryDetail( int index );		// the line under it: kind, mode, mods; never null
const char *Continue_EntrySummary( int index );		// the same without the files; never null

// [rc4l] Whether the row will work: 0 green, 1 yellow, 2 red. See continuestatus_compute for what
// each colour promises the player about their next action.
int Continue_EntryStatus( int index );
const char *Continue_EntryStatusReason( int index );	// empty for green; never null

// [rc4l] The files a row was played with, one at a time, for the panel that has room to list them.
// The row itself only has space for two and a count.
int Continue_EntryFileCount( int index );
const char *Continue_EntryFile( int index, int file );	// never null

// Where a server row points, for the panel. Empty for anything else; never null.
const char *Continue_EntryAddress( int index );
const char *Continue_EntryWhen( int index );		// the last played column; never null
int Continue_EntryKind( int index );				// 0 none, 1 single, 2 server, 3 hosted
int Continue_EntryProbe( int index );				// 0 unknown, 1 alive, 2 gone, 3 wads differ

// Ask about a server row. Lazy on purpose: querying fifty of other people's servers the moment a
// menu opens is a storm sent on behalf of rows nobody may look at.
void Continue_ProbeEntry( int index );

// Go to one row. False when there is no such row, which is all a caller has to check.
bool Continue_ActivateEntry( int index );

// Drop one row and its snapshot. The rest of the history is untouched.
void Continue_ForgetEntry( int index );

// Show the picker. Implemented by the menu (zx_continuemenu.cpp), so the record side of the feature
// does not have to know what a menu is.
void Continue_OpenList();

// [rc4l] Whether the picker is the menu currently on screen.
//
// The bar needs it: "which tab am I on" used to be "the browser, or else the main menu", and with a
// third place to be that answered Main Menu while the list was open -- so clicking Main Menu was a
// click on the tab you were already on, which does nothing by design.
bool Continue_IsListOpen();

// Close it, for the press that means "I have seen this list".
void Continue_CloseList();

// [rc4l] Leave the session and land on the main menu. The first row of the list while in one, so
// that pressing the pill in a game asks where to go rather than deciding for the player.
void Continue_LeaveToMenu();

// Record the session we are in. Called from the deliberate quit, never from a shutdown hook.
void Continue_NoteQuit();

// [rc4l] We are about to abandon whatever is running locally -- joining a server, or hosting one.
//
// Called BEFORE the WAD set is torn down, which is the only moment this can be captured at all: by
// the time the player leaves that server the local game has been gone for however long they played.
// Without this, going from one game straight into another silently loses the first.
void Continue_NoteLeavingLocalGame();

// [rc4l] Record a game we are hosting, so leaving it can start the same one again. There is nothing
// to snapshot -- the world lives in the child process -- so what is kept is the config that made it.
void Continue_NoteHosting( const HostConfig &config );

// Record a join that just landed.
// [rc4l] Bracket a command that leaves a server on its way to a destination it has already chosen,
// so the disconnect is not mistaken for the player simply leaving. See the .cpp.
void Continue_NoteChoosingDestination( bool bChoosing );

void Continue_NoteJoined();

// Forget it, for the case where the record describes something that turned out not to work.
void Continue_Forget();

// [rc4l] A reconnect is in progress, so the teardown it performs is not a departure.
void Continue_NoteReconnecting( bool bReconnecting );

// [rc4l] We have left a server, however that happened -- disconnected, kicked, banned, the server
// died, a version mismatch. Every one of them lands the player in the same place, because somebody
// who ends up somewhere different depending on WHY they left has to understand the difference to
// predict the game.
//
// Records the intention only. Acting here would mean opening a menu or reloading WADs from the
// middle of a teardown, which is what JoinTick already exists to avoid.
void Continue_NoteLeftServer();

// [rc4l] Join the server we just started, once the child reports itself listening. HostStart only
// spawns it; joining is a separate step the menu path owns, and anything else that hosts has to ask
// for it too or the player is left outside a server they meant to be in.
void Continue_JoinHostWhenReady();

// Call once per frame. Finishes a Continue that needed a WAD reload to get here, since the reload
// does not return and the load has to happen on the other side of it.
void Continue_Tick();

// Read the record off disk once, at startup, so the menu never touches the disk while drawing.
void Continue_Load();

// [rc4l] The player has changed how many entries to keep. Applied AT ONCE rather than at the next
// launch: a setting that appears to do nothing is one the player will move again, further, looking
// for the effect -- and then find it has thrown away more than they meant when it finally lands.
void Continue_LimitChanged();

// [rc4l] For the control bridge, so an E2E can assert on the decision rather than on pixels.
// 0 none, 1 single, 2 server. `Target` is the address or the map, never null.
int Continue_RecordKind();
const char *Continue_RecordTarget();

// Why the button is or is not there, so a failure is diagnosable without a debugger.
bool Continue_DebugSaveExists();
int Continue_DebugSaveVersion();
bool Continue_DebugBusy();
int Continue_DebugProbe();   // 0 unknown, 1 alive, 2 gone, 3 wads differ
int Continue_DebugProbeSlot();
int Continue_DebugDepartCalls();
int Continue_DebugDepartReturns();
bool Continue_DebugReturnPending();

} // namespace zx

#endif // ZX_CONTINUE_H
