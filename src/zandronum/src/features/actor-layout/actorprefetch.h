// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACTORPREFETCH_H
#define ZX_ACTORPREFETCH_H

#include <cstddef>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#endif

namespace zx { namespace actorlayout {

// [Doruk-Aksoy] Starts loading the first `bytes` of the object at p a cache line at a time; a prefetch never faults, so any address is safe.
inline void PrefetchLines(const void *p, size_t bytes)
{
	const char *const c = static_cast<const char *>(p);
	for (size_t off = 0; off < bytes; off += 64)
	{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
		_mm_prefetch(c + off, _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
		__builtin_prefetch(c + off);
#else
		(void)c;
#endif
	}
}

// [Doruk-Aksoy] The bytes of an actor its tick reads: the hot groups.
size_t TickBytes();

// [Doruk-Aksoy] The bytes of an actor the render copy reads.
size_t CopyBytes();

}} // namespace zx::actorlayout

#endif
