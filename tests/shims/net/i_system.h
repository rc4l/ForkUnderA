// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Minimal i_system.h shim for the standalone wire-format test target
// (zandrox_tests_net). It shadows the engine's heavy i_system.h so the vendored
// serialization TUs (networkshared.cpp, platform.cpp, huffman/*) compile without
// dragging the console/video/system layers -- the exact trick the masterserver
// subproject uses (masterserver/i_system.h). Placed FIRST on the target's include
// path; the engine tree (for doomtype.h etc.) is second.
//
// The primitives under test (BYTESTREAM_s read/write) only reach the engine
// through Printf (warnings on overflow) and atterm (huffman's atexit hook), so
// those are all that need standing in.
#pragma once
#include <cstdio>
#include <cstdlib>

#define Printf printf
#define atterm atexit

// Declared, not defined: the ByteStream paths the tests exercise never call it,
// so leaving it undefined would only fail the link if a test strayed into a
// genuinely fatal engine path -- which is a signal, not noise.
void I_Error(const char *error, ...);
