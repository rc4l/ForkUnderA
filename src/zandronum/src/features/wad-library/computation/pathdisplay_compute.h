// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where a file on a row actually came from, said in a tooltip.
//
// Two rows of one name differ only by their size and their folder, and until now the lists said the
// size and swallowed the folder -- so a collection with av.wad in three places offered three
// identical-looking rows and an "x3" marker that told the player duplicates existed without telling
// them where either copy was. The load order has the same problem one panel over: once a file is on
// the list its name is all that is left of it.
//
// THE WHOLE PATH IS SHOWN, WRAPPED, rather than shortened. A path elided in the middle
// (/Users/me/.../wads/doom) drops the very part that distinguishes two copies -- which is the one
// question the tooltip exists to answer -- so it wraps onto as many lines as it needs and nothing is
// lost. Breaking happens at the separators, because a path broken mid-component reads as two
// different folders.
//
// The tooltip renderer wraps on spaces, and a path has none: left to it, a long path is one
// unbreakable line that blows straight through the width cap. So the breaking is done here, and the
// result is handed over as lines the renderer will leave alone.
//
// Header-pure by the features/ rules -- no engine types. The caller supplies the measuring, because
// only it knows the font; the tests supply a monospace one.

#ifndef ZX_PATHDISPLAY_COMPUTE_H
#define ZX_PATHDISPLAY_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// [rc4l] Three states, not two. A file nobody has looked at yet is not a missing one, and saying
// MISSING about it is the lie the CUSTOM rows used to tell one column over -- the resolve runs on a
// worker, so "no answer yet" is a normal state that lasts a visible moment on a cold list.
enum class PathTipState
{
	Found,		// resolved, and `path` says where
	Pending,	// the resolve job has not answered yet
	Missing,	// looked for, and not on this machine
};

// What the tooltip says when there is no path to show. Named rather than written at the call sites,
// because the same two answers are given by the CUSTOM tab and the load order and they must not
// drift into two different wordings for one state.
extern const char *const kPathPendingText;
extern const char *const kPathMissingText;

// Measures a NUL-terminated string in whatever unit `maxWidth` is given in.
typedef int (*PathMeasureFn)(const char *text, void *ctx);

// [rc4l] The path, broken into lines that each fit `maxWidth`.
//
// Breaks fall BEFORE a separator, so every line after the first begins with one and reads as a
// continuation rather than as a fresh absolute path. A single component too wide to fit on a line of
// its own is split by character -- rare, but a filename can be arbitrarily long and silently
// overflowing the box is what this unit exists to stop.
//
// An empty path gives no lines. A `maxWidth` that nothing could fit gives the path back on one line,
// because the alternative is splitting forever.
std::vector<std::string> ComputeWrappedPath(const std::string &path, int maxWidth,
                                            PathMeasureFn measure, void *ctx);

// [rc4l] The whole tooltip body for one row: what the file is called, then where it came from.
//
// The name is repeated even though the row already draws it, because the row draws it TRUNCATED to
// whatever space was left after the size -- and on the lists this is for, the part that gets cut is
// the end, which is the part that distinguishes doom2-v1.9.wad from doom2-v1.666.wad.
std::string ComputePathTip(const std::string &name, PathTipState state, const std::string &path,
                           int maxWidth, PathMeasureFn measure, void *ctx);

} // namespace zx

#endif // ZX_PATHDISPLAY_COMPUTE_H
