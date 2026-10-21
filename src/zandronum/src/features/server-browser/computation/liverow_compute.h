// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How a list row is painted when one of the rows is LIVE.
//
// Two lists in this browser have a row that is not merely listed but currently true: the hosting
// catalogue has the experience it is serving, and the server list has the server you are connected
// to. Both marked it green, and both invented their own rule for what happens when that row is also
// the selected one, so the same state was drawn two different ways on two panels of the same menu.
// This unit is the one answer, and the two call sites map it onto their own colours and geometry.
//
// THE BAND CARRIES LIVE, THE LABEL CARRIES SELECTION. That is the whole rule, and it falls out of
// what the two states are. Selection moves every time an arrow key is pressed; being connected, or
// being the experience that is running, does not. So the slow fact gets the band, which is still
// there when the selection walks away, and the fast one gets the label, which is where the eye
// already is. A row that recoloured its label to say "live" would lose that label to the next thing
// that wanted it -- the catalogue learned this when a hovered running row went gold and the one
// state worth marking vanished exactly when you pointed at it.
//
// HOVER IS THE WEAKEST CLAIM and loses to both, because it is not a state the row is in at all. It
// says what clicking would do, so it may not overwrite what IS.
//
// What is NOT here: colours, alphas, rectangles. The two lists sit on different panels at different
// weights, and forcing one set of numbers on both would be sharing the wrong thing.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_LIVEROW_COMPUTE_H
#define ZX_LIVEROW_COMPUTE_H

namespace zx
{

// The band drawn behind the row, if any.
enum class RowBand
{
	None,

	// The faint hint under the pointer.
	Hover,

	// This is the row the caller would act on.
	Selection,

	// This row is the live one. Beats Selection, because the caller draws it at a stronger alpha
	// when it is also selected, so nothing is lost by the band saying live instead.
	Live,
};

// What the row's label says about itself.
enum class RowLabel
{
	// Neither selected nor live. Each list decides how loud that is -- the catalogue dims these,
	// the server list does not.
	Plain,

	Selected,

	// Live and not selected. The band is already green; this is the label agreeing with it.
	Live,
};

struct RowPaint
{
	RowBand band;
	RowLabel label;

	RowPaint() : band(RowBand::None), label(RowLabel::Plain) {}
	RowPaint(RowBand b, RowLabel l) : band(b), label(l) {}
};

// The three flags are independent on purpose: a row can be live, selected and hovered at once, and
// the caller should not have to know which of those it is allowed to pass together.
RowPaint PaintListRow(bool selected, bool live, bool hovered);

} // namespace zx

#endif // ZX_LIVEROW_COMPUTE_H
