// [rc4l] Pure logic that turns a GitHub repo + release tag + host platform into the direct download
// URL for that platform's release asset. Engine-free so it is unit-tested off-engine. The auto-
// updater notice uses this so "Download the update" lands on the file for the user's OS, not a
// generic page. The tag comes from the release check; the platform is chosen by the caller (#ifdef).
//
// Asset naming mirrors .github/workflows/release.yml exactly:
//   macOS   -> ZandroX-<tag>-macos-arm64.zip
//   Windows -> ZandroX-<tag>-windows-x64.zip
//   Linux   -> ZandroX-<tag>-linux-x86_64.tar.gz
// Direct download URL: <repoBase>/releases/download/<tag>/<asset>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_RELEASE_URL_COMPUTE_H
#define ZX_RELEASE_URL_COMPUTE_H

namespace zx
{

enum class ReleasePlatform
{
	MacOS,
	Windows,
	Linux,
	Unknown, // platform we don't ship a single-file asset for -> fall back to the release page
};

// Write the download URL for `tag` on `repoBase` for platform `p` into `out` (never overruns; always
// NUL-terminates when outSize > 0). Returns true when a URL was written, false on bad input
// (null/empty repoBase or tag, null/too-small buffer) with `out` set to an empty string when possible.
//
// Known platform -> the direct asset URL. Unknown platform -> the tag's release *page*
// (<repoBase>/releases/tag/<tag>), which lists every asset, so the user still reaches the download.
bool ComputeReleaseDownloadURL(char *out, int outSize, const char *repoBase, const char *tag,
	ReleasePlatform p);

// Extract the leading release tag from a `git describe` string into `out`. e.g.
// "v0.1.18-37-g6744c8fe4" -> "v0.1.18", "v0.1.19" -> "v0.1.19", "v0.1.18+" -> "v0.1.18" (stops at the
// first '-' or '+'). Used to show a clean "current -> new" version comparison on the update prompt.
// Returns true when a non-empty tag was written; false (with `out` emptied when possible) on null/
// empty input, a bad buffer, or if the tag wouldn't fit.
bool ExtractVersionTag(const char *gitDescribe, char *out, int outSize);

// Compare two release tags like "v0.1.18" and "v0.2.0". Returns true iff `candidate` is a strictly
// newer version than `current`: numeric major.minor.patch comparison, a leading 'v'/'V' is optional,
// and any missing or non-numeric component counts as 0 (so null/garbage -> 0.0.0). This is the gate
// for raising the "update available" notice, so it must never call an equal or older build "newer".
bool IsNewerVersion(const char *current, const char *candidate);

// Extract the "tag_name" string value from a GitHub /releases/latest API JSON response into `out`.
// Returns true on success; false (with `out` emptied) if the key is absent/malformed, the buffer is
// bad, or the value wouldn't fit. Tolerant of surrounding whitespace and key order. Release tags carry
// no JSON escape sequences, so none are interpreted (a value is taken literally up to the next quote).
bool ParseLatestReleaseTag(const char *json, char *out, int outSize);

// Outcome of one update check, folding together every way it can go so the background worker (and its
// tests) treat a timeout, a truncated body, and a valid response uniformly.
enum class UpdateCheckStatus {
	NoNetwork,        // the HTTPS GET failed/timed out/returned non-2xx -> we simply don't know; no notice
	Malformed,        // got a body but no usable tag_name (empty, truncated, garbage) -> no notice
	UpToDate,         // latest release is not newer than this build -> no notice
	UpdateAvailable,  // latest release is strictly newer -> raise the notice for `tag`
};

struct UpdateCheckResult {
	UpdateCheckStatus status;
	char tag[64];     // the latest tag; meaningful only when status == UpdateAvailable, else ""
};

// Decide the outcome of a check. `fetchOk` is whether the HTTP layer got a good 2xx response at all
// (false covers timeouts, no network, DNS failure, HTTP errors -- all "NoNetwork"). `body` is the
// response text; `currentDescribe` is this build's GIT_DESCRIPTION. Never reports UpdateAvailable for
// an equal/older release or an unparseable body, so a flaky/slow/partial response can't false-positive.
UpdateCheckResult ComputeUpdateCheckResult(bool fetchOk, const char *body, const char *currentDescribe);

} // namespace zx

#endif
