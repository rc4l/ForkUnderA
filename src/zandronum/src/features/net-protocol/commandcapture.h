// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_netcap: every command a server writes for a client, one line each, to the file fua_netcap_start names, so two runs of one fixture can be diffed command by command (fuactl netcap).

#ifndef ZX_COMMANDCAPTURE_H
#define ZX_COMMANDCAPTURE_H

namespace zx { namespace netprotocol {

// [rc4l] One command written for client, recorded while a capture is open.
void CaptureCommand(int client, bool reliable, const unsigned char *bytes, int length);

}} // namespace zx::netprotocol

#endif // ZX_COMMANDCAPTURE_H
