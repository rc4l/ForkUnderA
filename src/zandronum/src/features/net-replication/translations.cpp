// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/net-replication/translations.h"
#include "features/net-session/netrole.h"

#include "sv_commands.h"
#include "sv_main.h"
#include "features/trnslate/computation/trnslate_compute.h"

namespace zx { namespace netreplication {

void SendTranslationRange(int index, const trnslate::TranslationRange &range)
{
	if (!zx::net::IsServer())
		return;
	// [Doruk-Aksoy] Sent before it is kept, so the first range after StartTranslation tells the clients to start from the identity.
	SERVERCOMMANDS_CreateTranslation(index, range);
	SERVER_AddEditedTranslation(index, range);
}

void SendTranslationEnd(int index)
{
	if (!zx::net::IsServer() || SERVER_IsTranslationEdited(index))
		return;
	trnslate::TranslationRange identity;
	identity.kind = trnslate::kRangePalette;
	identity.start = identity.end = identity.pal1 = identity.pal2 = 1;
	SendTranslationRange(index, identity);
}

}} // namespace zx::netreplication
