// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Bindings a refill replaced, each kept with the last frame that could have drawn with it, and handed back to be filled again once the GPU has finished that frame, so a refill makes no new descriptor sets.

#ifndef ZX_HWRENDER_RETIREDBINDINGS_COMPUTE_H
#define ZX_HWRENDER_RETIREDBINDINGS_COMPUTE_H

#include <cstdint>
#include <deque>
#include <utility>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] The bindings waiting, oldest first, each with the frame number it was last drawn in.
template <class Bindings> struct RetiredBindings
{
	std::deque<std::pair<uint64_t, Bindings>> waiting;
};

// [Doruk-Aksoy] Retires `bindings`, last drawn in frame `frame`; past `keep` waiting, the oldest comes back in `dropped` (and true) for the caller to let go.
template <class Bindings>
bool ComputeRetireBindings(RetiredBindings<Bindings> &retired, uint64_t frame, Bindings bindings, size_t keep, Bindings &dropped)
{
	retired.waiting.emplace_back(frame, std::move(bindings));
	if (retired.waiting.size() <= keep) return false;
	dropped = std::move(retired.waiting.front().second);
	retired.waiting.pop_front();
	return true;
}

// [Doruk-Aksoy] The oldest bindings whose last frame the GPU has finished (`completed`), in `out` (and true), or false when every one may still be read.
template <class Bindings>
bool ComputeTakeRetiredBindings(RetiredBindings<Bindings> &retired, uint64_t completed, Bindings &out)
{
	if (retired.waiting.empty() || retired.waiting.front().first > completed) return false;
	out = std::move(retired.waiting.front().second);
	retired.waiting.pop_front();
	return true;
}

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_RETIREDBINDINGS_COMPUTE_H
