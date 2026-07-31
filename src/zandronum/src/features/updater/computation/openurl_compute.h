// [rc4l] Pure, engine-free URL-safety logic for the "open a link in the browser" primitive used by
// the auto-updater notice (Phase 0). No engine headers — only the standard library — so it is
// unit-tested off-engine and the coverage gate can enforce 100% on the matching *_compute.cpp.
//
// Why this is its own tested unit: the open-URL primitive is reachable from menudef/CCMD, so a mod
// could feed it a string. The allowlist here is the security boundary — only http/https, no control
// characters, no whitespace, bounded length — and both the confirmation dialog AND the platform
// I_OpenURL re-check through it, so no path can shell out to a file://, javascript:, or smuggled URL.
// See features/updater/zx_openurl.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_OPENURL_COMPUTE_H
#define ZX_OPENURL_COMPUTE_H

namespace zx {

// Longest URL we will ever hand to the OS "open" facility. Well past any real GitHub release URL,
// short enough that a pathological string can't blow up the confirmation text or a platform buffer.
static const int kMaxOpenableUrlLen = 2048;

// Whether `url` is safe to pass to the platform browser-open call. True only when ALL hold:
//   * non-null and non-empty, length < kMaxOpenableUrlLen;
//   * the scheme is exactly "http://" or "https://" (case-insensitive), with at least one more
//     character after it (a host must follow);
//   * every byte is printable 7-bit ASCII in (0x20, 0x7f) — no control chars, no spaces, no DEL,
//     no high bytes. This is what rejects embedded newlines/quotes (log/command smuggling), raw
//     spaces, and non-ASCII homoglyph tricks.
// Any other scheme (file, javascript, data, mailto, steam, an implicit relative path, …) is refused.
bool IsOpenableURL(const char *url);

} // namespace zx

#endif
