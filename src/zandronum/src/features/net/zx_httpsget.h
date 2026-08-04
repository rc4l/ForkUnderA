// [rc4l] One HTTPS GET, three platform backends behind one call.
//
// This lives in its own module rather than inside a feature because two unrelated features now need
// it -- the update check and the server registry list fetch -- and the alternative was a second copy
// of the macOS/Windows/libcurl backends, or the server registry #including the updater. Both are
// worse than a shared seam.
//
// Fail-safe by contract: false means "we do not know", never "the answer is empty". Callers must
// treat a false return as a reason to keep whatever they already had.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_HTTPSGET_H
#define ZX_HTTPSGET_H

namespace zx
{

// Fill `out` with the body of GET https://<host><path> on a 2xx; return false (with out emptied) on
// any error, timeout, or non-2xx. Blocking: call it from a worker thread, never the game thread.
bool HttpsGet( const char *host, const char *path, char *out, int outSize );

} // namespace zx

#endif
