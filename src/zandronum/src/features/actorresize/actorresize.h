// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#pragma once

// [rc4l] Pure, engine-free helpers for runtime actor resizing (A_SetSize / ACS
// APROP_Radius / APROP_Height / the SVC2_SETTHINGSIZE netcode). The resize itself
// (unlink -> set radius/height -> relink) lives in AActor::SetSize (p_map.cpp) and is
// exercised end-to-end via the MCP; only these dependency-free bits are pulled out so
// they can be unit-tested (actorresize_test.cpp) without linking the engine.
//
// Templated so the same code serves the engine's fixed_t (the zx::Fixed strong type, or
// the raw 64-bit integer) and the plain integers the tests use. The only operations used
// are `< 0` and `!=`, both of which fixed_t already supports.

namespace ActorResize
{
	// [rc4l] A_SetSize treats a negative requested dimension as "keep the current value"
	// (that is the meaning of its -1 default). Returns the dimension to actually apply.
	template <typename T>
	inline T ComputeResolvedDimension( T requested, T current )
	{
		return requested < 0 ? current : requested;
	}

	// [rc4l] Which dimensions differ between an old and new size. The engine maps these to
	// ACTORSIZE_RADIUS / ACTORSIZE_HEIGHT flags for the SetThingSize broadcast; kept as
	// plain bools here so the helper stays engine-free.
	struct SizeDelta
	{
		bool radiusChanged;
		bool heightChanged;

		// [rc4l] True when at least one dimension changed -- there is nothing to broadcast
		// otherwise, so this gates the network send.
		bool Any() const { return radiusChanged || heightChanged; }
	};

	template <typename T>
	inline SizeDelta ComputeSizeDelta( T oldRadius, T newRadius, T oldHeight, T newHeight )
	{
		return SizeDelta{ oldRadius != newRadius, oldHeight != newHeight };
	}
}
