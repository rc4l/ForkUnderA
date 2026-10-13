// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/materiallayout_compute.h"

namespace zx { namespace materials {

namespace {

void PlaceSprite(MaterialLayout &l, float xScale, float yScale)
{
	l.spriteLeft = -l.leftOffset / xScale;
	l.spriteTop = -l.topOffset / yScale;
	l.spriteWidth = l.width / xScale;
	l.spriteHeight = l.height / yScale;
}

} // namespace

MaterialLayout ComputeMaterialLayout(const MaterialSource &src, bool expanded, bool trimmed, const int trim[4])
{
	MaterialLayout l;
	l.width = src.width;
	l.height = src.height;
	l.leftOffset = src.leftOffset;
	l.topOffset = src.topOffset;
	l.renderWidth = src.renderWidth;
	l.renderHeight = src.renderHeight;
	l.spriteU[0] = l.spriteV[0] = 0.f;
	l.spriteU[1] = l.spriteV[1] = 1.f;
	PlaceSprite(l, src.xScale, src.yScale);
	if (!expanded) return l;

	// [rc4l] A one-texel transparent frame round a sprite, so texture filtering does not cut its edges.
	const int oldwidth = l.width, oldheight = l.height;
	l.width += 2;
	l.height += 2;
	l.leftOffset += 1;
	l.topOffset += 1;
	l.renderWidth = l.renderWidth * l.width / oldwidth;
	l.renderHeight = l.renderHeight * l.height / oldheight;
	PlaceSprite(l, src.xScale, src.yScale);
	if (!trimmed) return l;

	l.spriteLeft += trim[0] / src.xScale;
	l.spriteTop += trim[1] / src.yScale;
	l.spriteWidth -= (oldwidth - trim[2]) / src.xScale;
	l.spriteHeight -= (oldheight - trim[3]) / src.yScale;
	l.spriteU[0] = trim[0] / (float)l.width;
	l.spriteV[0] = trim[1] / (float)l.height;
	l.spriteU[1] -= (oldwidth - trim[0] - trim[2]) / (float)l.width;
	l.spriteV[1] -= (oldheight - trim[1] - trim[3]) / (float)l.height;
	return l;
}

bool ComputeTrimBorders(const unsigned char *rgba, int w, int h, int rect[4])
{
	const int size = w * h;
	if (size == 1) return false;

	int first;
	for (first = 0; first < size; first++)
	{
		if (rgba[first * 4 + 3] != 0) break;
	}
	if (first >= size)
	{
		rect[0] = 0;
		rect[1] = 0;
		rect[2] = 1;
		rect[3] = 1;
		return true;
	}

	int last;
	for (last = size - 1; last > first; last--)
	{
		if (rgba[last * 4 + 3] != 0) break;
	}

	rect[1] = first / w;
	rect[3] = 1 + last / w - rect[1];
	const unsigned char *rows = rgba + rect[1] * w * 4;
	const int rowCount = rect[3];

	// [rc4l] A column is see-through when no row in the opaque band has alpha in it.
	auto columnOpaque = [&](int x) {
		for (int y = 0; y < rowCount; y++)
		{
			if (rows[(x + y * w) * 4 + 3] != 0) return true;
		}
		return false;
	};

	rect[0] = 0;
	while (!columnOpaque(rect[0])) rect[0]++;
	rect[2] = w - rect[0];
	for (int x = w - 1; rect[2] > 1 && !columnOpaque(x); x--) rect[2]--;
	return true;
}

int ComputeStoredDimension(int value, int maxSize)
{
	return (maxSize > 0 && value > maxSize) ? maxSize : value;
}

}} // namespace zx::materials
