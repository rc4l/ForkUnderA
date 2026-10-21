// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Whether this server will hand over this file, and whether there is room to start now.
//
// The lookup is deliberately not a filesystem operation. A request names a WAD; this matches it
// against the table of files the server ALREADY HAS LOADED and returns an index into that table. No
// path is ever built from a remote string, so there is no root to escape from and no symlink to
// follow -- httpreq_compute refuses to produce anything but a bare segment, and this refuses to turn
// that segment into anything but a table index. Between them, "serve an arbitrary file" is not a bug
// that can be written here.
//
// THE IWAD RULE. The server refuses to send any IWAD that is not on the shipped free-IWAD list --
// the same config/iwadallowlist.txt names the client uses, linked from iwadallow_compute so there is
// one list and not two that can drift. A ZandroX server must not become the thing that distributes
// doom2.wad, even when its operator has configured it carelessly.
//
// Note this side checks the NAME only, while the client checks a SHA-256, and that asymmetry is
// correct rather than an oversight. The client is deciding whether to trust bytes from a stranger, so
// it needs the hash. The server already knows exactly which file it opened, and an operator running
// a locally modified freedoom2.wad is still redistributing something free -- a hash check here would
// refuse a legitimate file to answer a question that was never asked. The security boundary is the
// client's; this is a guard rail on the operator.
//
// ADMISSION is here for the same reason as the rest: it is policy, it is arithmetic, and getting it
// wrong means either an unbounded number of transfer threads or a queue that never drains. Slots are
// capped globally and per address -- per address because one peer opening twenty connections would
// otherwise take every slot, which is a denial of service that costs the attacker nothing.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_SERVEPOLICY_COMPUTE_H
#define ZX_SERVEPOLICY_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

enum class ServeVerdict
{
	Allowed,
	Disabled,			// the operator turned serving off
	NotLoaded,			// not a file this server has open
	ProtectedIwad,		// an IWAD we cannot confirm is free to redistribute -- assume it is sold
	TooLarge,			// past the configured per-file ceiling
};

// A short human sentence for the verdict, for the console log. Never NULL.
const char *ServeVerdictReason(ServeVerdict verdict);

// The HTTP status to answer a verdict with. Disabled and NotLoaded both map to 404 on purpose: a
// server with serving switched off should be indistinguishable from one that simply does not have
// the file, rather than advertising a feature it declines to provide.
int ServeVerdictStatus(ServeVerdict verdict);

// One file the server currently has open and could therefore send. `name` is the bare filename as
// the engine loaded it; `size` is its size in bytes.
struct ServableFile
{
	std::string name;
	bool isIwad;
	long long size;

	ServableFile() : isIwad(false), size(0) {}
	ServableFile(const std::string &n, bool iwad, long long bytes)
		: name(n), isIwad(iwad), size(bytes) {}
};

// Index of the loaded file matching `requested`, or -1. Case-insensitive, because the request came
// off a network where the same WAD is filed under three spellings and the operator did not choose
// which one the client would ask with.
int FindServableFile(const std::vector<ServableFile> &loaded, const std::string &requested);

// The whole serving decision. On Allowed, `outIndex` is the entry in `loaded` to send; on anything
// else it is set to -1. `maxFileBytes` <= 0 means no ceiling.
ServeVerdict ClassifyServeRequest(const std::vector<ServableFile> &loaded,
	const std::string &requested, bool enabled, long long maxFileBytes, int &outIndex);

// Whether a new transfer may start right now. `maxSlots` <= 0 means serving is effectively off;
// `maxPerAddress` <= 0 means no per-address cap.
bool ComputeAdmitTransfer(int activeTotal, int maxSlots, int activeFromAddress, int maxPerAddress);

// What to put in Retry-After when admission is refused. Estimated from how many transfers are ahead
// and how long one typically takes, clamped to something a client will actually wait through rather
// than treat as a failure.
int ComputeRetryAfterSeconds(int waitingAhead, int maxSlots, int typicalTransferSeconds);

} // namespace zx

#endif // ZX_SERVEPOLICY_COMPUTE_H
