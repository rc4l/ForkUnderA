// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Every fatal error written to stderr, the +logfile log and the bridge's log before any dialog shows it.

#ifndef ZX_FATALLOG_H
#define ZX_FATALLOG_H

// [Doruk-Aksoy] Logs the run's first fatal error and ignores the rest; safe before the console, the video or the bridge are up.
void ZX_FatalLog(const char *message);

#endif // ZX_FATALLOG_H
