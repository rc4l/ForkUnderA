// [rc4l] Auto-updater notice state (Phase 0). Holds whether a newer ZandroX release exists and which
// tag it is, so the main menu can show a bottom-right "update available" notice that opens the
// OS-correct download. The background release check (a later phase) is the real producer; until it
// lands, the `update_notify` CCMD drives this so the notice can be exercised.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_UPDATER_H
#define ZX_UPDATER_H

namespace zx { namespace updater {

// Record that release `tag` (e.g. "v0.1.19") is the latest available. The notice is only raised if
// `tag` is strictly newer than the running build (zx::IsNewerVersion vs GIT_DESCRIPTION); an equal or
// older tag clears it. Safe to call repeatedly (the check may re-run).
void SetLatestTag(const char *tag);

// Drop any pending notice.
void Clear();

// Whether a newer release is available (i.e. the main menu should show the notice).
bool IsAvailable();

// The newer release's tag, or "" when none. Valid only while IsAvailable() is true.
const char *Tag();

// Kick off the background GitHub-releases check on a detached worker thread (non-blocking). Called
// once at startup; a no-op when the cl_fua_update_notify cvar is off. On finding a newer release it
// calls SetLatestTag; any failure/timeout leaves the notice hidden. Safe to call again (e.g. a CCMD).
void StartCheck();

} } // namespace zx::updater

#endif
