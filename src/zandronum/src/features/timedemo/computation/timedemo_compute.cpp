// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/timedemo/computation/timedemo_compute.h"
#include <cstdio>

std::string ComputeTimedemoLine(int gametics, int realtics, int ticrate)
{
	char buf[96];
	const double fps = realtics > 0 ? (double)gametics / realtics * ticrate : 0.0;
	std::snprintf(buf, sizeof(buf), "timed %d gametics in %d realtics (%.1f fps)\n", gametics, realtics, fps);
	return buf;
}
