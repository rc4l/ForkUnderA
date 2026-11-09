// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The pure decisions of sv_dumppackets, 3.3's capture of packet payloads for training zstd dictionaries: which commands are worth keeping, how the collision counter steps, and what the dump file is called.

#ifndef ZX_DUMPFILTER_COMPUTE_H
#define ZX_DUMPFILTER_COMPUTE_H

#include <string>

namespace zx { namespace netsession {

// [KI] Whether a server command's payload joins the dictionary-training dump: chatter, prints and secrets are left out so the dictionary learns the traffic that matters.
bool ComputeShouldDumpServerCommand(int command, int extcommand);

// [KI] Whether a client command's payload joins the dump: only in-game commands, with the same chatter and secret exclusions.
bool ComputeShouldDumpClientCommand(int command);

// [KI] The collision counter for dump files born in the same second: one more of the same timestamp counts up, a new timestamp starts over at 0.
unsigned int ComputeDumpCounter(bool sameTimestamp, unsigned int counter);

// [KI] The dump file's path, as 3.3 formats it: <dir>/packet_<cl|sv>_<timestamp>_<counter>.bin.
std::string ComputeDumpFileName(const std::string &dir, bool fromClient, const std::string &timestamp, unsigned int counter);

}} // namespace zx::netsession

#endif // ZX_DUMPFILTER_COMPUTE_H
