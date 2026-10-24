// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] One server command as fua_netcap writes it: the tic, the client it went to, reliable or not, its command id (and extended id), its size and its bytes in hex, one line each so two captures diff line by line.

#ifndef ZX_COMMANDCAPTURE_COMPUTE_H
#define ZX_COMMANDCAPTURE_COMPUTE_H

#include <string>

namespace zx { namespace netprotocol {

// [rc4l] A command's id and, after extendedId, its extended id; -1 for the extended id of an ordinary command, and both -1 for an empty one.
void ComputeCommandIds(const unsigned char *bytes, int length, int extendedId, int &id, int &extended);

// [rc4l] The capture line: "tic client r|u id ext size hex".
std::string ComputeCaptureLine(int tic, int client, bool reliable, const unsigned char *bytes, int length, int extendedId);

}} // namespace zx::netprotocol

#endif // ZX_COMMANDCAPTURE_COMPUTE_H
