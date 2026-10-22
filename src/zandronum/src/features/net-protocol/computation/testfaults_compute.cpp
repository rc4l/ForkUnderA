// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-protocol/computation/testfaults_compute.h"

#include <stdlib.h>

namespace zx { namespace netprotocol {

bool ComputeNetDropMatches(const char *spec, int id, int ext)
{
	if (spec == nullptr) return false;
	const char *p = spec;
	while (*p != '\0')
	{
		char *end = nullptr;
		const long wantId = strtol(p, &end, 10);
		if (end == p) return false;
		long wantExt = -1;
		p = end;
		if (*p == '/')
		{
			wantExt = strtol(p + 1, &end, 10);
			if (end == p + 1) return false;
			p = end;
		}
		if (wantId == id && wantExt == ext) return true;
		if (*p == ',') p++;
		else if (*p != '\0') return false;
	}
	return false;
}

int ComputeReportedServerTic(int held, int testOffset)
{
	const int t = held - (testOffset > 0 ? testOffset : 0);
	return t > 0 ? t : 0;
}

}} // namespace zx::netprotocol
