// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/planemirror_compute.h"

namespace zx { namespace hwrender {

bool ComputePlaneMirrorFaces(bool ceiling, float planeZ, float viewZ)
{
	return ceiling ? viewZ <= planeZ : viewZ >= planeZ;
}

float ComputePlaneMirrorEyeZ(float planeZ, float viewZ)
{
	return 2.f * planeZ - viewZ;
}

void ComputePlaneMirrorClip(bool ceiling, float planeZ, float out[4])
{
	out[0] = 0.f; out[2] = 0.f;
	out[1] = ceiling ? -1.f : 1.f;
	out[3] = ceiling ? planeZ : -planeZ;
	if (out[3] == 0.f) out[3] = ceiling ? 0.0001f : -0.0001f;
}

int ComputePlaneMirrorSlot(MirrorPlane *slots, int &used, int maxSlots, MirrorPlane p)
{
	for (int i = 0; i < used; i++)
		if (slots[i].ceiling == p.ceiling && slots[i].z == p.z) return i;
	if (used >= maxSlots) return -1;
	slots[used] = p;
	return used++;
}

bool ComputeBoxMayBeVisible(const float *mvp, const float mn[3], const float mx[3])
{
	// [rc4l] Bit k set when a corner is outside plane k: x<-w, x>w, y<-w, y>w, z<0, z>w.
	int allOutside = 63;
	for (int c = 0; c < 8; c++)
	{
		const float p[3] = { (c & 1) ? mx[0] : mn[0], (c & 2) ? mx[1] : mn[1], (c & 4) ? mx[2] : mn[2] };
		float clip[4];
		for (int r = 0; r < 4; r++)
			clip[r] = mvp[r] * p[0] + mvp[4 + r] * p[1] + mvp[8 + r] * p[2] + mvp[12 + r];
		int out = 0;
		if (clip[0] < -clip[3]) out |= 1;
		if (clip[0] >  clip[3]) out |= 2;
		if (clip[1] < -clip[3]) out |= 4;
		if (clip[1] >  clip[3]) out |= 8;
		if (clip[2] < 0.f)      out |= 16;
		if (clip[2] >  clip[3]) out |= 32;
		allOutside &= out;
	}
	return allOutside == 0;
}

}} // namespace zx::hwrender
