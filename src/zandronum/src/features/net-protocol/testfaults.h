// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Test-only faults for netgame scenarios: fua_debug_netdrop leaves server commands unsent and fua_debug_unlagged_ticoffset makes a client report an older server tic, each proving a scenario fails.

#ifndef ZX_TESTFAULTS_H
#define ZX_TESTFAULTS_H

namespace zx { namespace netprotocol {

// [rc4l] Whether a command a server is about to write for a client is one fua_debug_netdrop leaves unsent.
bool DroppedForTest(const unsigned char *bytes, int length);

// [rc4l] The server tic a client reports for unlagged after fua_debug_unlagged_ticoffset.
int ReportedServerTic(int held);

}} // namespace zx::netprotocol

#endif // ZX_TESTFAULTS_H
