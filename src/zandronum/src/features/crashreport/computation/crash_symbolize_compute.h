// [rc4l] Pure decision logic for turning a resolved crash backtrace into a human title + a stable
// grouping fingerprint. The engine glue (zx_crashreport.cpp) resolves each frame's address to a
// function/module name via dladdr at crash time -- dyld has every loaded module's symbols, so this
// names BOTH our own frames (the binary ships un-stripped) and system frames (e.g. Apple's OpenGL
// driver, which is where a windowed<->fullscreen GL crash actually dies, on a worker thread with no
// app frames at all). GlitchTip can't do this server-side (it has no Apple symbols), which is why
// unsymbolicated native crashes all collapse into one "<unknown>" group. We name them here and set
// a fingerprint so GlitchTip forms distinct, titled groups.
//
// This file holds only the pure decisions (which frame is the crash site, the fingerprint, name
// cleanup) so they are unit-tested off-engine. No engine/sentry/dl headers here.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_CRASH_SYMBOLIZE_COMPUTE_H
#define ZX_CRASH_SYMBOLIZE_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace crashreport {

// One resolved backtrace frame. `func` is the resolved (demangled) function name, or empty if the
// address didn't resolve. `mainModule` is true when the frame is in our own executable (vs a system
// library). Frames are supplied OUTERMOST-first, matching how sentry-native stores them (index 0 is
// the thread entry point; the crash site is deepest/last).
struct ResolvedFrame
{
	std::string func;
	bool        mainModule = false;
};

// The identity of a crash: a title for the GitHub issue / GlitchTip group, and a fingerprint that
// groups recurrences of the SAME crash together while keeping DIFFERENT crashes apart.
struct CrashIdentity
{
	std::string title;        // e.g. "glgProcessColor" or "AActor::Tick" -- never "<unknown>"/empty
	std::string fingerprint;  // stable key derived from the crash-site frames
};

// Strip a trailing " + <offset>" (dladdr/backtrace_symbols style) and surrounding whitespace, and
// collapse the result. "AActor::Tick() + 3024" -> "AActor::Tick()". Empty stays empty.
std::string NormalizeFrameName(const std::string &raw);

// Decide the crash title + fingerprint from the resolved frames (outermost-first).
//  - Title: the deepest (innermost) frame that resolved -- preferring our own module when the very
//    deepest frames are system noise -- so an app crash reads as the app function and a driver crash
//    reads as the driver function. Never returns empty or the literal "<unknown>": if nothing
//    resolved, falls back to a stable "crash (<n> unresolved frames)".
//  - Fingerprint: the innermost few resolved frame names joined; identical stacks fingerprint the
//    same, different crash sites fingerprint differently. Independent of ASLR (names, not addresses).
CrashIdentity ComputeCrashIdentity(const std::vector<ResolvedFrame> &framesOutermostFirst);

}} // namespace zx::crashreport

#endif // ZX_CRASH_SYMBOLIZE_COMPUTE_H
