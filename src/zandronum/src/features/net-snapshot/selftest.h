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
// [Kaori-Chani] The twelve units' own self-tests are not declared here any more: step 1
// turned each into its *_compute_test.cpp GoogleTest file, so there is nothing left in
// this tree to call. What a game binary can still answer, and a test binary cannot, is
// whether its own build decodes the format correctly -- which is what formattest.cpp is
// reduced to.

// Each block names itself as it runs, and the summary prints the names and
// their count, so a block that never ran, or never called this, shows.
void SelfTestBlock(const char* pszName);
void SelfTestBlocksReset();
const char* SelfTestBlocks(unsigned int& count);

// The format blocks' output, Printf in the engine and stdout in the runner.
void SelfTestPrint(const char* pszFormat, ...) GCCPRINTF(1,2);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_SELFTEST_H
