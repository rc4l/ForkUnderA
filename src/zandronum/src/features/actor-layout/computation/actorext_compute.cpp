// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "actorext_compute.h"

namespace zx { namespace actorlayout {

bool ComputeIsExtMember(intptr_t offset)
{
	return offset >= kExtMemberBase;
}

size_t ComputeExtMemberOffset(intptr_t offset)
{
	return (size_t)(offset - kExtMemberBase);
}

} } // namespace zx::actorlayout
