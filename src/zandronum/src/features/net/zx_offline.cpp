// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net/zx_offline.h"
#include "m_argv.h"

namespace zx { namespace net {

bool OfflineLaunch()
{
	return Args != nullptr && Args->CheckParm("-offline") != 0;
}

}} // namespace zx::net
