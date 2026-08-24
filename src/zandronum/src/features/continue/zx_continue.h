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
// because the reload throws.
void Continue_Activate();

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
void Continue_NoteJoined();

// Forget it, for the case where the record describes something that turned out not to work.
void Continue_Forget();

// Call once per frame. Finishes a Continue that needed a WAD reload to get here, since the reload
// does not return and the load has to happen on the other side of it.
void Continue_Tick();

// Read the record off disk once, at startup, so the menu never touches the disk while drawing.
void Continue_Load();

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

} // namespace zx

#endif // ZX_CONTINUE_H
