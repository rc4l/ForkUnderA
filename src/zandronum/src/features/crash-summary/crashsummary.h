// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_CRASHSUMMARY_H
#define ZX_CRASHSUMMARY_H

namespace zx { namespace crashsummary {

// [Doruk-Aksoy] One frame ended; features/framephase's FrameBoundary calls it once a frame in every build.
void NoteFrame();

// [Doruk-Aksoy] Who hears the last minute in a few lines each second, which a crash report keeps to send with a crash; NULL hears nothing.
void SetListener(void (*listen)(const char *summary));

}} // namespace zx::crashsummary

#endif
