// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/linequery_compute.h"

#include <ctype.h>
#include <string.h>

namespace zx { namespace levelmesh {

const char *ComputeKeyArgValue(const char *arg, const char *key)
{
	const size_t klen = strlen(key);
	return (strncmp(arg, key, klen) == 0 && arg[klen] == '=') ? arg + klen + 1 : nullptr;
}

bool ComputeHasNamePrefix(const char *name, const char *prefix)
{
	for (; *prefix != '\0'; name++, prefix++)
		if (tolower((unsigned char)*name) != tolower((unsigned char)*prefix)) return false;
	return true;
}

}} // namespace zx::levelmesh
