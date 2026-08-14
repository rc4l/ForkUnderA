// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Turning "the server wants brutal.wad" into "GET these URLs, in this order, and write to this
// filename" -- the decisions in a download, separated from the transfer that carries it out.
//
// The shape follows Odamex's HTTP downloader (client/src/cl_download.cpp), which is the design worth
// copying: a list of mirror base URLs, each tried with a few case variants of the filename, first hit
// wins. Not their code -- this is our own -- but the same idea, because the idea is right and the
// alternative (a WAD search service like Wadseeker, or serving bytes from the game server) is either
// a whole subsystem or a way to make the server stutter for everyone whenever one player joins.
//
// Two things here exist purely to be attacked. Both take a string the SERVER chose, and the server is
// not ours:
//   - IsSafeDownloadName rejects anything that could write outside the download directory, or write
//     something the engine would then execute-by-loading.
//   - UrlEscapeFileName stops a crafted name from restructuring the URL it is pasted into.
// They are pure functions with tests rather than checks inside the transfer loop, because "did we
// remember to reject ../../" should be answerable without running the game.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_DOWNLOADPLAN_COMPUTE_H
#define ZX_DOWNLOADPLAN_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// Split a whitespace-separated CVAR value ("https://a/ https://b/") into its entries, dropping empty
// runs. Both the mirror list and the IWAD allowlist are stored this way, matching how Odamex stores
// cl_downloadsites and how Zandronum stores every other list-shaped CVAR.
std::vector<std::string> SplitOnWhitespace(const std::string &value);

// Keep only entries that can actually be fetched, and put them in the one form the URL builder
// expects. An entry survives if it starts with "http://" or "https://"; it is then given a trailing
// '/' unless it already ends in '/' or '=' -- '=' because a mirror can be a query endpoint
// ("https://example/getwad.php?search=") where the name is appended as a value, not a path segment.
// Later duplicates are dropped, keeping the first: order is preference order.
std::vector<std::string> NormalizeDownloadSites(const std::vector<std::string> &raw);

// Percent-escape `name` so it survives being appended to a URL. Everything outside the RFC 3986
// unreserved set is escaped, so a name containing '?', '#', '/' or a space cannot restructure the URL
// -- it can only ever be one path segment or one query value.
std::string UrlEscapeFileName(const std::string &name);

// Whether `name` is a bare filename we are willing to create on disk. False for anything with a path
// separator, a drive letter, a "..", a control character, a leading dot, an implausible length, a
// Windows reserved device stem (CON, NUL, COM1 ... -- reserved regardless of extension), or an
// extension outside the set the engine actually loads. The last one matters as much as the traversal
// checks: a download is a file a remote host asked us to create, and there is no reason for it to be
// a .exe, a .dll, a .bat or a .cfg.
bool IsSafeDownloadName(const std::string &name);

// Full URLs to try for `filename`, in order. For each site, three spellings of the name are tried --
// as the server spelled it, all-lowercase, all-uppercase -- because mirrors are case-sensitive web
// servers and the same WAD is filed under all three across the ecosystem. Spellings that coincide are
// emitted once. Returns empty if the name is unsafe or no site survives normalisation.
// [rc4l] The part of a URL worth showing a player: the host, with scheme, path and any credentials
// stripped. Used for the console line that says where a download is coming from.
//
// A whole URL is the wrong thing to print. It wraps in the console, buries the one field that
// answers "who am I downloading from", and a signed mirror URL can carry a token in its query
// string that has no business in a log a player may paste into a bug report.
//
// Returns the input unchanged if there is nothing that looks like a host in it, so an odd entry in
// cl_fua_downloadsites is still named rather than silently becoming an empty string.
std::string DownloadSourceName(const std::string &url);

std::vector<std::string> BuildCandidateUrls(const std::vector<std::string> &sites,
	const std::string &filename);

// The site preference order for one download run, as one place the policy can be read and tested:
// the server's advertised download site(s) first, then the configured public mirrors, then the
// last-resort sources -- today that is the hosting machine's own direct-download endpoint.
//
// [rc4l] The host's endpoint comes AFTER the mirrors deliberately. It is usually a residential
// upload -- the one link that also carries the game's netcode -- while the mirrors are real file
// hosts; and the md5 gate downstream means a mirror's copy either IS the server's build or gets
// deleted and the walk continues. So mirrors first costs at most a few fast 404s on a brand-new
// WAD, and host-first costs the whole download at upstream speed. The host stays in the list as
// the one source certain to have the file.
std::vector<std::string> AssembleSiteOrder(const std::vector<std::string> &serverSites,
	const std::vector<std::string> &configuredMirrors,
	const std::vector<std::string> &lastResortSites);

// "3.2 MB" / "412 KB" -- the form a transfer figure takes in the status line. "?" for a negative.
std::string HumanBytes(long long n);

// The one-line progress summary for a transfer in flight, from the three facts the worker tracks.
// Three shapes, and the boundaries between them are the tested behaviour:
//   - Nothing has answered yet (no Content-Length AND no bytes): "Searching for <file>..." --
//     source selection in progress, which a bare 0% would misreport as a stalled transfer.
//   - Content-Length known: "<file>  42%  (3.2 of 7.4 MB)", FIXED WIDTH -- the percent padded to
//     three columns and the received figure to the width of the total, so the band drawn behind it
//     does not twitch as digits appear.
//   - Bytes flowing but no Content-Length: "<file>  3.2 MB" -- a percentage would be a guess.
std::string FormatDownloadStatus(const std::string &file, long long received, long long total);

} // namespace zx

#endif // ZX_DOWNLOADPLAN_COMPUTE_H
