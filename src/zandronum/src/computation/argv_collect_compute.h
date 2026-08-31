// [rc4l] Turning a raw (argc, argv) into the argument list the engine should see.
//
// This exists because the macOS entry point used to do it inline, into a fixed 64-entry array, with
// no bounds check -- and a server we start ourselves is handed roughly a hundred and fifty
// arguments, so everything past the sixty-fourth was written past the end of that array and over
// whichever file's statics the linker had placed next. The server then died seconds later somewhere
// with no visible connection to the real fault, differently each run.
//
// Pulled out here so the part that can be got wrong is the part that is tested: the counting, the
// bound, and which arguments are dropped. What is left at the call site is only the copying into
// engine-owned storage.
//
// Header-pure: <string> and <vector> only, no engine types, so the test links without the engine.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#ifndef ZX_ARGV_COLLECT_COMPUTE_H
#define ZX_ARGV_COLLECT_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// What the command line meant, once the entries the engine must not see are taken out of it.
struct CollectedArgv
{
	// Every argument to pass on, in the order given. No cap: a map rotation is as long as the
	// operator wants it, so any constant is one rotation away from being wrong.
	std::vector<std::string> args;

	// -wad_picker_restart is a message to the launcher, not an argument for the engine, so it is
	// reported here and dropped from `args`.
	bool bRestartedFromWadPicker;

	CollectedArgv() : bRestartedFromWadPicker(false) { }
};

// Collect argv[0 .. argc-1]. NULL and empty entries are skipped: the OS is allowed to hand us
// either, and an empty argument reaches the engine's parser as a stray token.
//
// argc is the bound, and argv[argc] -- the standard's NULL terminator -- is NOT read.
CollectedArgv ComputeCollectArgv( int argc, const char *const *argv );

} // namespace zx

#endif
