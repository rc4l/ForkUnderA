// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/reconnect_compute.h"

namespace zx
{

ReconnectAction DecideReconnect( bool haveStoredAddress )
{
	return haveStoredAddress ? ReconnectAction::AskThenConnect : ReconnectAction::Refuse;
}

bool ReconnectConnects( ReconnectAction action )
{
	return ( action != ReconnectAction::Refuse );
}

} // namespace zx
