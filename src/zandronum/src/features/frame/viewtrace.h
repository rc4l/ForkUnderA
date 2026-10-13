// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_viewtrace: the angle each frame's main view was drawn with, recorded for a number of frames and judged.

#ifndef ZX_VIEWTRACE_H
#define ZX_VIEWTRACE_H

namespace zx { namespace frame {

// [rc4l] The main view as set up for this frame, recorded while a trace is armed; once a frame however many renderers set it up.
void NoteMainView();

// [rc4l] Record the next `frames` frames' main view and report them once they are in, every frame written to `csv` when it is named.
void ArmViewTrace(int frames, const char *csv);

// [rc4l] Report an armed trace now, with however many frames it has.
void EndViewTrace();

}} // namespace zx::frame

#endif // ZX_VIEWTRACE_H
