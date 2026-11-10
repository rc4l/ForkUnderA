// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Server cvars a client's own simulation reads, which a server sends when they change and to each joiner.

#ifndef ZX_NET_SYNCEDCVARS_H
#define ZX_NET_SYNCEDCVARS_H

class FBaseCVar;

namespace zx { namespace net {

// [Doruk-Aksoy] Constructed beside a CVAR_SERVERINFO cvar's definition, after it in the same file, to register it.
struct SyncedCVar
{
	explicit SyncedCVar(FBaseCVar &cvar);
};

// [Doruk-Aksoy] A registered cvar's new value to every client, called from its CUSTOM_CVAR body; a client or a startup sends nothing.
void SendSyncedCVar(FBaseCVar &cvar);

// [Doruk-Aksoy] Every registered cvar to one joining client.
void SendSyncedCVarsTo(int client);

}} // namespace zx::net

#endif // ZX_NET_SYNCEDCVARS_H
