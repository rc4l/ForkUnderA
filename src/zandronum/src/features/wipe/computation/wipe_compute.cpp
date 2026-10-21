// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/computation/wipe_compute.h"

namespace zx { namespace wipe {

void ComputeMeltStart(const unsigned char *randoms, int *y)
{
	y[0] = -(randoms[0] & 15);
	for (int i = 1; i < kMeltColumns; i++)
	{
		const int next = y[i - 1] + (randoms[i] % 3) - 1;
		y[i] = next < -15 ? -15 : next > 0 ? 0 : next;
	}
}

bool ComputeMeltTic(int *y)
{
	bool done = true;
	for (int i = 0; i < kMeltColumns; i++)
	{
		if (y[i] < 0)
		{
			y[i]++;
			done = false;
		}
		else if (y[i] < kMeltHeight)
		{
			const int dy = y[i] < 16 ? y[i] + 1 : 8;
			y[i] = y[i] + dy < kMeltHeight ? y[i] + dy : kMeltHeight;
			done = false;
		}
	}
	return done;
}

bool ComputeMeltStrip(int column, int fall, int w, int h, MeltStrip &out)
{
	const int left = column * w / kMeltColumns, right = (column + 1) * w / kMeltColumns;
	int drop = fall * h / kMeltHeight;
	if (drop < 0) drop = 0;
	if (h - drop <= 0) return false;
	out.x = float(left);
	out.y = float(drop);
	out.w = float(right - left);
	out.h = float(h - drop);
	out.u1 = float(left) / w;
	out.u2 = float(right) / w;
	out.v2 = float(h - drop) / h;
	return true;
}

float ComputeCrossfadeAlpha(int clock)
{
	const float a = clock / 32.f;
	return a < 0.f ? 0.f : a > 1.f ? 1.f : a;
}

void ComputeBurnMask(const unsigned char *fire, int count, unsigned char *alpha)
{
	for (int i = 0; i < count; i++)
	{
		const int s = fire[i] * 2;
		alpha[i] = (unsigned char)(s > 255 ? 255 : s);
	}
}

}} // namespace zx::wipe
