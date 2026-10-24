// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What a cached resource binding is made for, so a pass keeps one per set of views it reads rather than re-pointing one, which on WebGPU makes a new bind group a browser frees only when it collects it.

#ifndef ZX_BINDINGKEY_COMPUTE_H
#define ZX_BINDINGKEY_COMPUTE_H

#include <cstddef>

namespace zx { namespace hwrender {

// [rc4l] Three objects a binding reads, by identity, and the pass or pipeline it is for.
struct BindingKey
{
	const void *a, *b, *c;
	int pass;
	bool operator==(const BindingKey &o) const { return a == o.a && b == o.b && c == o.c && pass == o.pass; }
};

// [rc4l] A key's hash, every member mixed in.
size_t ComputeBindingKeyHash(const BindingKey &key);

struct BindingKeyHash
{
	size_t operator()(const BindingKey &key) const { return ComputeBindingKeyHash(key); }
};

// [rc4l] Whether a cache of size bindings is emptied before it makes another, at limit, since a key holding views the frame no longer has (a resize, a level change) is never asked for again.
bool ComputeBindingCacheFull(size_t size, size_t limit);

}} // namespace zx::hwrender

#endif // ZX_BINDINGKEY_COMPUTE_H
