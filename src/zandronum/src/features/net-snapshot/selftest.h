// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: selftest.h
//
// Description: Fixture validation (fua_snap_verify) and the self-test plumbing
// shared by the engine's fua_snap_selftest and the standalone format runner.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_SELFTEST_H
#define ZX_NETSNAPSHOT_SELFTEST_H

#include "basictypes.h"	// GCCPRINTF

namespace zx { namespace netsnapshot {

// Validates a .zsnap, returning zsnap_info.py's exit codes: 0 PASS, 2 errors,
// 3 aborted at an undecodable record, 4 valid with a truncated tail.
int VerifyFixture(const char* pszFilename, double skew);

// Self-test entry points, run together by the fua_snap_selftest command.
unsigned int RunFormatSelfTests();	// formattest.cpp
unsigned int RunSelfTests();			// entities.cpp
// [Kaori-Chani] The units' self-tests became GoogleTest files in step 1; what only a game
// binary can answer is whether its own build decodes correctly, which is formattest.cpp's.

// Each block names itself as it runs, and the summary prints the names and
// their count, so a block that never ran, or never called this, shows.
void SelfTestBlock(const char* pszName);
void SelfTestBlocksReset();
const char* SelfTestBlocks(unsigned int& count);

// The format blocks' output, Printf in the engine and stdout in the runner.
void SelfTestPrint(const char* pszFormat, ...) GCCPRINTF(1,2);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_SELFTEST_H
