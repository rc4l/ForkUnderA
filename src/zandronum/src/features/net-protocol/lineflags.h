// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The line flags a client moves and draws by: a server sends them whole whenever any changes (SetSomeLineFlags) and a client replaces them whole.

#ifndef ZX_LINEFLAGS_H
#define ZX_LINEFLAGS_H

#include "doomdata.h"

namespace zx { namespace netprotocol {

// [rc4l] Monsters, floaters, projectiles and hitscans were left out, so a client simulated them through lines its server had closed.
const unsigned kClientLineFlags = ML_BLOCKING | ML_BLOCKMONSTERS | ML_BLOCK_PLAYERS | ML_BLOCK_FLOATERS | ML_BLOCKPROJECTILE |
	ML_BLOCKEVERYTHING | ML_RAILING | ML_BLOCKHITSCAN | ML_ADDTRANS;

}} // namespace zx::netprotocol

#endif // ZX_LINEFLAGS_H
