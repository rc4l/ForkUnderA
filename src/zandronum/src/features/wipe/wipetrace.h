// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_wipe_trace: what the backend presents around each wipe, recorded so fua_wipe_report can say whether a wipe reached the screen and how it went from one picture to the next.

#ifndef ZX_WIPETRACE_H
#define ZX_WIPETRACE_H

namespace zx { namespace wipe {

// [rc4l] Whether fua_wipe_trace is on, so the backend reads back what it presents only then.
bool TraceWanted();

// [rc4l] The engine began a wipe, which counts whether or not the backend presents any of it.
void NoteWipeStarted();

// [rc4l] A w by h top-down RGB frame the backend is about to present, during a wipe or not.
void TracePresented(const unsigned char *rgb, int w, int h);

}} // namespace zx::wipe

#endif // ZX_WIPETRACE_H
