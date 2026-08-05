// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See zx_joinserver.cpp. Joining is normally reached through the fua_join_selected_server
// console command, so this header exists only for the parts the browser menu has to drive directly.

#ifndef ZX_JOINSERVER_H
#define ZX_JOINSERVER_H

namespace zx
{

// Hold the resume that follows a finished download, because the player is being asked something they
// have to answer first.
//
// A download completing one frame into a "cancel this download?" prompt would otherwise tear the
// engine down for the reload underneath the prompt: the question resolves itself, the answer never
// lands, and a restart appears out of a dialog you were mid-way through -- which reads as a crash
// rather than as a feature. Every path that puts up such a prompt must pair this with a release.
void HoldJoinResume();

// Let it go again. `proceed` false discards a download that finished while held, so answering "yes,
// cancel" still means no join even when the transfer beat the player to it. The downloaded file is
// kept either way -- it is complete and verified, and discarding it would only mean fetching it
// again.
void ReleaseJoinResume(bool proceed);

bool IsJoinResumeHeld();

// [rc4l] A join we started is now in flight -- the WAD set has been reloaded and the connect is
// being attempted. `serverName` is only for the message if it goes wrong.
//
// These three exist because a refused connect used to leave the player at a bare console with a
// stranger's WAD set loaded and one line of explanation scrolled off. Every port that has a server
// browser puts you back in it with the reason instead, and that is what this is for. It covers far
// more than downloads: full, wrong password, banned, version mismatch and just-shut-down all fail
// the same way and all landed in the same place.
void NoteJoinStarted( const char *serverName );

// The connect worked. Nothing after this counts as a failed join, so quitting normally later does
// not reopen the browser.
void NoteJoinSucceeded();

// The connect failed, with the engine's own reason. Safe to call from the middle of the disconnect --
// it only records, and Tick acts on it once the teardown has finished.
void NoteJoinFailed( const char *reason );

// Call once per frame from the main loop. Reopens the browser after a failed join.
void JoinTick();

} // namespace zx

#endif // ZX_JOINSERVER_H
