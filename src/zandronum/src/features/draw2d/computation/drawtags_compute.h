// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] What a 2D draw's tag list carries: the width a 16.16 tag value travels at, and which argument types may ride in it.

#ifndef ZX_DRAWTAGS_COMPUTE_H
#define ZX_DRAWTAGS_COMPUTE_H

#include <cstdint>
#include <type_traits>

#include "features/fixed64/computation/fixed_strong.h"

// [Doruk-Aksoy] What a tag list holding a fixed_t or a 64-bit integer is refused with.
#define ZX_DRAWTAGS_REFUSED "a 2D draw's tag value is a fixed_t or a 64-bit integer, which the tag's 32-bit read takes half of: pass int(...)"

namespace zx { namespace draw2d {

// [Doruk-Aksoy] The type a 16.16 tag value (DTA_Alpha, DTA_ShadowAlpha) is passed and read as, so a caller's int and the reader agree on its width.
using TagFixed = int32_t;

// [Doruk-Aksoy] A 16.16 alpha tag's value as the draw keeps it, raw fixed point: at most opaque.
int64_t ComputeTagAlpha(TagFixed raw);

// [Doruk-Aksoy] Whether a tag-list argument of type T travels at the width its reader takes: never a fixed_t or a 64-bit integer, whose upper half a 32-bit read leaves behind.
template <typename T> constexpr bool ComputeTagArgIsSafe()
{
	using U = typename std::decay<T>::type;
	return !std::is_same<U, zx::Fixed>::value && !std::is_same<U, long long>::value && !std::is_same<U, unsigned long long>::value;
}

template <typename... T> struct TagArgsSafe;
template <> struct TagArgsSafe<> : std::true_type {};
template <typename H, typename... T> struct TagArgsSafe<H, T...> : std::integral_constant<bool, ComputeTagArgIsSafe<H>() && TagArgsSafe<T...>::value> {};

// [Doruk-Aksoy] Whether every argument of a tag list is one ComputeTagArgIsSafe allows.
template <typename... T> constexpr bool ComputeTagArgsAreSafe() { return TagArgsSafe<T...>::value; }

}} // namespace zx::draw2d

#endif // ZX_DRAWTAGS_COMPUTE_H
