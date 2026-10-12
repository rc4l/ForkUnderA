// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's Clipper as a value: the angle ranges the view is blocked in, the silhouette a removal keeps, and the box test that prunes the BSP.

#ifndef ZX_CLIPRANGES_COMPUTE_H
#define ZX_CLIPRANGES_COMPUTE_H

#include <vector>

namespace zx { namespace visibility {

// [rc4l] Pseudo-angles throughout: they order points about the eye as true angles do, and wrap at 2^32 as BAM does.
class ClipRanges
{
public:
	void Clear();
	// [rc4l] Keep what is blocked now through every later removal, as a portal's outline is kept.
	void SetSilhouette();
	bool SafeCheckRange(unsigned start, unsigned end) const;
	void SafeAddClipRange(unsigned start, unsigned end);
	void SafeRemoveClipRange(unsigned start, unsigned end);

	int Count() const { return (int)m_ranges.size(); }
	unsigned Start(int i) const { return m_ranges[i].start; }
	unsigned End(int i) const { return m_ranges[i].end; }

private:
	struct Range { unsigned start, end; };
	std::vector<Range> m_ranges, m_silhouette;

	bool IsRangeVisible(unsigned start, unsigned end) const;
	void AddClipRange(unsigned start, unsigned end);
	void RemoveClipRange(unsigned start, unsigned end);
	void DoRemoveClipRange(unsigned start, unsigned end);
};

// [rc4l] R_PointToPseudoAngle for the offset (dx, dy) from the eye; 0 at the eye itself.
unsigned ComputePseudoAngle(double dx, double dy);

// [rc4l] Clipper::AngleToPseudo: a true angle (BAM) as the pseudo-angle the ranges are kept in.
unsigned ComputeAngleToPseudo(unsigned angle);

// [rc4l] Block everything outside the view's frustum, as GL seeds its clipper before a walk: a half-angle of 2^32-1 or more blocks nothing.
void ComputeSeedFrustum(ClipRanges &clip, unsigned viewAngle, unsigned halfAngle);

// [rc4l] Clipper::CheckBox: whether any of a BSP node's box (top, bottom, left, right, raw 16.16) may be visible from the eye.
bool ComputeBoxVisible(const ClipRanges &clip, long long viewx, long long viewy, const long long box[4]);

}} // namespace zx::visibility

#endif // ZX_CLIPRANGES_COMPUTE_H
