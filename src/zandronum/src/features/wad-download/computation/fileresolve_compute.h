// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which file on this disk is the one with THIS digest, and in what order to ask.
//
// There were two answers to that question and they disagreed. Joining resolved a PWAD by name AND
// md5, so a same-named file with different bytes was rejected and the right one fetched. Hosting
// resolved by name alone, took the first hit, and started a server on it. Since the server is handed
// the path the client resolved, both sides load the same wrong file, protected-lump authentication
// compares them and passes, and the experience is quietly not the one it advertises. The host is the
// last person who could notice, and they are the one we tell nothing.
//
// So resolution belongs in one place. The plan is the part worth testing on its own: the ORDER, and
// what each step costs.
//
//   by-hash/<md5>/<name>   a stat. The path asserts the content, so a hit needs no read at all.
//   <download dir>/<name>  a hash. Ours, and the likeliest right answer after the store.
//   <search hits>          a hash each, in the engine's own search order.
//
// Cheapest first is the whole point. Anything we downloaded is a store hit and costs nothing to
// confirm, whatever its size. That is what makes verifying affordable at all for a 240MB entry.
// Only a file the player put there by hand is ever read, and that is exactly the file whose bytes we
// have no other reason to trust.
//
// DEDUPED, because the download folder is registered in FileSearch.Directories: without it the flat
// copy is found twice and hashed twice. Comparison is case-insensitive and separator-blind, since
// the same file reaches us as both C:\x\a.wad and c:/x/a.wad depending on who resolved it.
//
// NO DIGEST, NO PLAN. An entry without an md5 returns nothing to try rather than a list of steps
// none of which could ever confirm anything. The caller falls back to a name search and knows it
// is doing so, instead of being handed a plan that silently always fails.
//
// Header-pure by the features/ rules: no engine types, no filesystem.

#ifndef ZX_FILERESOLVE_COMPUTE_H
#define ZX_FILERESOLVE_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// What it costs to confirm a candidate.
enum class ResolveCheck
{
	Stat,	// exists is enough: the path itself names the digest
	Hash,	// read the file and compare
};

struct ResolveStep
{
	std::string path;
	ResolveCheck check;

	ResolveStep() : check(ResolveCheck::Hash) {}
	ResolveStep(const std::string &p, ResolveCheck c) : path(p), check(c) {}
};

// Candidates for "the file called `name` whose md5 is `md5Hex`", cheapest first, deduped.
// Empty when there is nothing to try: no name, no usable digest, or nowhere to look.
//
// `downloadDir` is our own folder and may be empty. `searchHits` is every copy the engine's file
// search found, in its order, and may be empty.
std::vector<ResolveStep> PlanFileResolve(const std::string &name, const std::string &md5Hex,
	const std::string &downloadDir, const std::vector<std::string> &searchHits);

} // namespace zx

#endif // ZX_FILERESOLVE_COMPUTE_H
