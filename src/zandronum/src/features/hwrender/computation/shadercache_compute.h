// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Compiled shaders kept by their stage and final text, so a source the backend has compiled once, a failed one included, is never compiled again.

#ifndef ZX_HWRENDER_SHADERCACHE_COMPUTE_H
#define ZX_HWRENDER_SHADERCACHE_COMPUTE_H

#include <string>
#include <unordered_map>

namespace zx { namespace hwrender {

// [Doruk-Aksoy] The kept shaders, with how many lookups found one and how many compiled.
template <class Shader> struct ShaderCache
{
	std::unordered_map<std::string, Shader> kept;
	unsigned hits = 0, compiles = 0;
};

// [Doruk-Aksoy] The shader for `stage` and `text`, compiled by `compile` the first time they are asked for and kept, whatever it returned.
template <class Shader, class Compile>
Shader ComputeCachedShader(ShaderCache<Shader> &cache, int stage, const std::string &text, Compile compile)
{
	const std::string key = std::to_string(stage) + ":" + text;
	const auto found = cache.kept.find(key);
	if (found != cache.kept.end()) { cache.hits++; return found->second; }
	cache.compiles++;
	return cache.kept.emplace(key, compile()).first->second;
}

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_SHADERCACHE_COMPUTE_H
