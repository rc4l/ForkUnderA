// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Faults a netgame scenario switches on to prove it fails: server commands left unsent (fua_debug_netdrop) and a client reporting an older server tic than it holds (fua_debug_unlagged_ticoffset).

#ifndef ZX_TESTFAULTS_COMPUTE_H
#define ZX_TESTFAULTS_COMPUTE_H

namespace zx { namespace netprotocol {

// [rc4l] Whether a command of id (and extended id, -1 for none) is in spec, a comma list of "id" or "id/ext"; an empty spec drops nothing.
bool ComputeNetDropMatches(const char *spec, int id, int ext);

// [rc4l] The server tic a client reports for unlagged: what it holds less a test offset in tics, never below 0.
int ComputeReportedServerTic(int held, int testOffset);

}} // namespace zx::netprotocol

#endif // ZX_TESTFAULTS_COMPUTE_H
