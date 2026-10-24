// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/bindingkey_compute.h"

namespace zx { namespace hwrender {

namespace {

unsigned long long Mix(unsigned long long h, unsigned long long v)
{
	h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
	return h;
}

}

size_t ComputeBindingKeyHash(const BindingKey &key)
{
	unsigned long long h = (unsigned long long)(size_t)key.a;
	h = Mix(h, (unsigned long long)(size_t)key.b);
	h = Mix(h, (unsigned long long)(size_t)key.c);
	h = Mix(h, (unsigned long long)(unsigned)key.pass);
	return (size_t)h;
}

bool ComputeBindingCacheFull(size_t size, size_t limit)
{
	return size >= limit;
}

}} // namespace zx::hwrender
