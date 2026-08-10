// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "liverow_compute.h"

namespace zx
{

RowPaint PaintListRow(bool selected, bool live, bool hovered)
{
	if (live)
		return RowPaint(RowBand::Live, selected ? RowLabel::Selected : RowLabel::Live);

	if (selected)
		return RowPaint(RowBand::Selection, RowLabel::Selected);

	// Hover never reaches the label. It is a hint about a click that has not happened.
	return RowPaint(hovered ? RowBand::Hover : RowBand::None, RowLabel::Plain);
}

} // namespace zx
