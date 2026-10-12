// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/clipranges_compute.h"

#include <math.h>
#include <string.h>

namespace zx { namespace visibility {

static const unsigned kAngleMax = 0xffffffffu;

void ClipRanges::Clear()
{
	m_ranges.clear();
	m_silhouette.clear();
}

void ClipRanges::SetSilhouette()
{
	// [rc4l] GL keeps the first silhouette and drops the nodes a second call makes, so a second call changes nothing.
	if (m_silhouette.empty()) m_silhouette = m_ranges;
}

bool ClipRanges::IsRangeVisible(unsigned start, unsigned end) const
{
	if (end == 0 && !m_ranges.empty() && m_ranges[0].start == 0) return false;
	for (size_t i = 0; i < m_ranges.size() && m_ranges[i].start < end; i++)
		if (start >= m_ranges[i].start && end <= m_ranges[i].end) return false;
	return true;
}

bool ClipRanges::SafeCheckRange(unsigned start, unsigned end) const
{
	if (start > end) return IsRangeVisible(start, kAngleMax) || IsRangeVisible(0, end);
	return IsRangeVisible(start, end);
}

void ClipRanges::AddClipRange(unsigned start, unsigned end)
{
	if (m_ranges.empty())
	{
		m_ranges.push_back({ start, end });
		return;
	}
	// [rc4l] Drop the ranges this one covers; stop if one already covers it.
	for (size_t i = 0; i < m_ranges.size() && m_ranges[i].start < end; )
	{
		if (m_ranges[i].start >= start && m_ranges[i].end <= end) m_ranges.erase(m_ranges.begin() + i);
		else if (m_ranges[i].start <= start && m_ranges[i].end >= end) return;
		else i++;
	}
	// [rc4l] Grow the first range it touches, and swallow the ones that growth reaches.
	for (size_t i = 0; i < m_ranges.size() && m_ranges[i].start <= end; i++)
	{
		if (m_ranges[i].end < start) continue;
		if (m_ranges[i].start > start) m_ranges[i].start = start;
		if (m_ranges[i].end < end) m_ranges[i].end = end;
		while (i + 1 < m_ranges.size() && m_ranges[i + 1].start <= m_ranges[i].end)
		{
			if (m_ranges[i + 1].end > m_ranges[i].end) m_ranges[i].end = m_ranges[i + 1].end;
			m_ranges.erase(m_ranges.begin() + i + 1);
		}
		return;
	}
	size_t pos = 0;
	while (pos < m_ranges.size() && m_ranges[pos].start < end) pos++;
	m_ranges.insert(m_ranges.begin() + pos, { start, end });
}

void ClipRanges::SafeAddClipRange(unsigned start, unsigned end)
{
	if (start > end)
	{
		AddClipRange(start, kAngleMax);
		AddClipRange(0, end);
	}
	else AddClipRange(start, end);
}

void ClipRanges::DoRemoveClipRange(unsigned start, unsigned end)
{
	if (m_ranges.empty()) return;
	for (size_t i = 0; i < m_ranges.size() && m_ranges[i].start < end; )
	{
		if (m_ranges[i].start >= start && m_ranges[i].end <= end) m_ranges.erase(m_ranges.begin() + i);
		else i++;
	}
	for (size_t i = 0; i < m_ranges.size(); i++)
	{
		Range &r = m_ranges[i];
		if (r.start >= start && r.start <= end)
		{
			r.start = end;
			break;
		}
		else if (r.end >= start && r.end <= end) r.end = start;
		else if (r.start < start && r.end > end)
		{
			const Range tail = { end, r.end };
			r.end = start;
			m_ranges.insert(m_ranges.begin() + i + 1, tail);
			break;
		}
	}
}

void ClipRanges::RemoveClipRange(unsigned start, unsigned end)
{
	if (!m_silhouette.empty())
	{
		size_t k = 0;
		while (k < m_silhouette.size() && m_silhouette[k].end <= start) k++;
		if (k < m_silhouette.size() && m_silhouette[k].start <= start)
		{
			if (m_silhouette[k].end >= end) return;
			start = m_silhouette[k].end;
			k++;
		}
		while (k < m_silhouette.size() && m_silhouette[k].start < end)
		{
			DoRemoveClipRange(start, m_silhouette[k].start);
			start = m_silhouette[k].end;
			k++;
		}
		if (start >= end) return;
	}
	DoRemoveClipRange(start, end);
}

void ClipRanges::SafeRemoveClipRange(unsigned start, unsigned end)
{
	if (start > end)
	{
		RemoveClipRange(start, kAngleMax);
		RemoveClipRange(0, end);
	}
	else RemoveClipRange(start, end);
}

unsigned ComputePseudoAngle(double dx, double dy)
{
	if (dx == 0 && dy == 0) return 0;
	double result = dy / (fabs(dx) + fabs(dy));
	if (dx < 0) result = 2.f - result;
	// [rc4l] xs_Fix<30>::ToFix, as GL does it: the magic add leaves the value rounded half to even in the low word.
	const double magic = result + 6755399441055744.0 / 1073741824.0;
	unsigned long long bits;
	memcpy(&bits, &magic, sizeof(bits));
	return (unsigned)bits;
}

unsigned ComputeAngleToPseudo(unsigned angle)
{
	const double a = angle * 3.14159265358979323846 / 2147483648.0;
	return ComputePseudoAngle(cos(a), sin(a));
}

void ComputeSeedFrustum(ClipRanges &clip, unsigned viewAngle, unsigned halfAngle)
{
	clip.SafeAddClipRange(ComputeAngleToPseudo(viewAngle + halfAngle), ComputeAngleToPseudo(viewAngle - halfAngle));
}

bool ComputeBoxVisible(const ClipRanges &clip, long long viewx, long long viewy, const long long box[4])
{
	enum { kTop = 0, kBottom = 1, kLeft = 2, kRight = 3 };
	static const int checkcoord[12][4] =
	{
		{ 3, 0, 2, 1 }, { 3, 0, 2, 0 }, { 3, 1, 2, 0 }, { 0 },
		{ 2, 0, 2, 1 }, { 0, 0, 0, 0 }, { 3, 1, 3, 0 }, { 0 },
		{ 2, 0, 3, 1 }, { 2, 1, 3, 1 }, { 2, 1, 3, 0 }
	};
	const int boxpos = (viewx <= box[kLeft] ? 0 : viewx < box[kRight] ? 1 : 2) +
		(viewy >= box[kTop] ? 0 : viewy > box[kBottom] ? 4 : 8);
	if (boxpos == 5) return true;
	const int *check = checkcoord[boxpos];
	// [rc4l] The offset taken in 64-bit fixed point first, then as a double, as R_PointToPseudoAngle takes it.
	const unsigned angle1 = ComputePseudoAngle((double)(box[check[0]] - viewx), (double)(box[check[1]] - viewy));
	const unsigned angle2 = ComputePseudoAngle((double)(box[check[2]] - viewx), (double)(box[check[3]] - viewy));
	return clip.SafeCheckRange(angle2, angle1);
}

}} // namespace zx::visibility
