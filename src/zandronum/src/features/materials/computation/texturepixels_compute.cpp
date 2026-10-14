// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/materials/computation/texturepixels_compute.h"

#include <string.h>

namespace zx { namespace materials {

unsigned int ComputeAverageColor(const unsigned char *rgba, int count, bool scale, int maxOut)
{
	if (count == 0) return 0xFFFFFFFFu;
	unsigned int r = 0, g = 0, b = 0;
	for (int i = 0; i < count; i++)
	{
		r += rgba[i * 4];
		g += rgba[i * 4 + 1];
		b += rgba[i * 4 + 2];
	}
	r /= count;
	g /= count;
	b /= count;
	unsigned int maxv = r > g ? r : g;
	if (b > maxv) maxv = b;
	if (maxv && scale)
	{
		r = (unsigned int)((long long)r * maxOut / maxv);
		g = (unsigned int)((long long)g * maxOut / maxv);
		b = (unsigned int)((long long)b * maxOut / maxv);
	}
	return 0xFF000000u | (r << 16) | (g << 8) | b;
}

int ComputeTransparency(const unsigned char *rgba, int count, int trans)
{
	if (trans != -1) return trans;
	for (int i = 0; i < count; i++)
	{
		const unsigned char alpha = rgba[i * 4 + 3];
		if (alpha != 0xff && alpha != 0) return 1;
	}
	return 0;
}

// [rc4l] The alpha byte and the colour bytes of one RGBA texel as a 32-bit word, as GZDoom's macros read them.
#ifdef WORDS_BIGENDIAN
#define MSB 0
#define SOME_MASK 0xffffff00
#else
#define MSB 3
#define SOME_MASK 0x00ffffff
#endif

#define CHKPIX(ofs) (l1[(ofs)*4+MSB]==255 ? (( ((unsigned int*)l1)[0] = ((unsigned int*)l1)[ofs]&SOME_MASK), trans=true ) : false)

bool ComputeSmoothEdges(unsigned char *rgba, int w, int h)
{
	if (h <= 1 || w <= 1) return false;
	// [rc4l] A picture with only clear texels still counts as having them.
	bool trans = rgba[MSB] == 0;
	unsigned char *l1 = rgba;
	int x, y;

	if (l1[MSB]==0 && !CHKPIX(1)) CHKPIX(w);
	l1+=4;
	for(x=1;x<w-1;x++, l1+=4)
	{
		if (l1[MSB]==0 &&  !CHKPIX(-1) && !CHKPIX(1)) CHKPIX(w);
	}
	if (l1[MSB]==0 && !CHKPIX(-1)) CHKPIX(w);
	l1+=4;

	for(y=1;y<h-1;y++)
	{
		if (l1[MSB]==0 && !CHKPIX(-w) && !CHKPIX(1)) CHKPIX(w);
		l1+=4;
		for(x=1;x<w-1;x++, l1+=4)
		{
			// [rc4l] uzdoom@e29fce695: the diagonal neighbours count too, or an upscaled texture keeps a clear texel whose only opaque neighbours are diagonal.
			if (l1[MSB]==0 &&  !CHKPIX(-w) && !CHKPIX(-1) && !CHKPIX(1) && !CHKPIX(-w-1) && !CHKPIX(-w+1) && !CHKPIX(w-1) && !CHKPIX(w+1)) CHKPIX(w);
		}
		if (l1[MSB]==0 && !CHKPIX(-w) && !CHKPIX(-1)) CHKPIX(w);
		l1+=4;
	}

	if (l1[MSB]==0 && !CHKPIX(-w)) CHKPIX(1);
	l1+=4;
	for(x=1;x<w-1;x++, l1+=4)
	{
		if (l1[MSB]==0 &&  !CHKPIX(-w) && !CHKPIX(-1)) CHKPIX(1);
	}
	if (l1[MSB]==0 && !CHKPIX(-w)) CHKPIX(-1);

	return trans;
}

#undef CHKPIX
#undef MSB
#undef SOME_MASK

int ComputeTextureBands(const unsigned char *rgba, int w, int h, int bands[5][2])
{
	int count = 0;
	int start = -1, len = 0;
	for (int y = 0; y < h; y++)
	{
		const unsigned char *alpha = rgba + w * y * 4 + 3;
		int x;
		for (x = 0; x < w; x++, alpha += 4)
		{
			if (*alpha != 0) break;
		}
		if (x != w)
		{
			if (start == -1)
			{
				start = y;
				// [rc4l] A gap of under 16 rows joins the band above it.
				if (count && y <= bands[count - 1][0] + bands[count - 1][1] + 16)
				{
					count--;
					start = bands[count][0];
					len = y - start;
				}
				if (count == 4) return 0;
			}
			len++;
		}
		else if (start != -1)
		{
			if (len == 1) len = 2;
			bands[count][0] = start;
			bands[count][1] = len;
			count++;
			start = -1;
			len = 0;
		}
	}
	if (start != -1)
	{
		bands[count][0] = start;
		bands[count][1] = len;
		count++;
	}
	// [rc4l] One band over the whole picture saves nothing.
	if (start == 0 && len == h) return 0;
	return count;
}

int ComputeFullbrightEntries(const unsigned char *colormap, const unsigned char *playpal, bool lit[256])
{
	int count = 0;
	for (int i = 0; i < 256; i++)
	{
		// [rc4l] The palette test should be for 0, but that gives false positives with Heretic and Hexen.
		bool on = !(playpal[3 * i] < 10 && playpal[3 * i + 1] < 10 && playpal[3 * i + 2] < 10);
		for (int j = 0; j < 32 && on; j++) on = colormap[i + j * 256] == i;
		lit[i] = on;
		if (on) count++;
	}
	return count;
}

}} // namespace zx::materials
