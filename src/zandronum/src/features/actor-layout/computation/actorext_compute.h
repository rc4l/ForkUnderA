// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] How a DECORATE native member names a field of the actor's rare-field block rather than of the actor: its offset carries a base no actor reaches.
#ifndef ZX_ACTOREXT_COMPUTE_H
#define ZX_ACTOREXT_COMPUTE_H

#include <cstddef>
#include <cstdint>

namespace zx { namespace actorlayout {

// [Doruk-Aksoy] Far past any actor's size, so an offset at or above it can only be one of the block's.
constexpr intptr_t kExtMemberBase = 0x40000000;

// [Doruk-Aksoy] The member offset that names the block field offsetInBlock bytes in.
constexpr intptr_t ComputeExtMemberCode(size_t offsetInBlock)
{
	return kExtMemberBase + (intptr_t)offsetInBlock;
}

// [Doruk-Aksoy] Whether a member offset names a field of the block.
bool ComputeIsExtMember(intptr_t offset);

// [Doruk-Aksoy] The block field's offset in the block, for an offset ComputeIsExtMember accepts.
size_t ComputeExtMemberOffset(intptr_t offset);

} } // namespace zx::actorlayout

#endif
