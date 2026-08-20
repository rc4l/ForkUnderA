// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where the arrow keys go on the hosting form.
//
// The server browser has had one of these since it grew a second region: computation/
// browserfocus_compute, one enum, one pure function, a test per rule. The hosting form never got
// one. It tracked the same idea -- which single thing owns the keyboard -- across THREE independent
// variables: an int for which field, a bool for the visibility row, a bool for the button.
//
// Three variables for one position means an invariant nobody enforces: at most one may be set. Every
// place that moved focus had to remember to clear the other two, and four of them did not. Clicking
// INTERNET and then a field left the visibility bool set, and that bool gates every keystroke, so the
// caret appeared in a box that would not accept a letter. Clicking the button left it set too, and
// then two things glowed while DOWN did nothing at all, because the button was tested first and
// returned early.
//
// One position, one value, and the invariant stops being something to remember.
//
//     LIST            up/down move the selected experience; right crosses to the right column.
//                     Up off the first row leaves the form, back to the tabs.
//     FIELDS          up/down walk the column; up off the first one returns to the LIST.
//                     LEFT AND RIGHT ARE NOT NAVIGATION -- they belong to the caret, the same rule
//                     the browser's search box follows, which is also why LEFT cannot be the way
//                     back to the list from here.
//     GAMEPLAY        the settings drawn beside the experience while the form is SHUT: the sliders
//                     and the rows of pills. Up and down walk them, left and right change the row
//                     they are on rather than moving off it -- the same split the visibility row
//                     makes, and for the same reason.
//     VISIBILITY      up returns to the last field, down goes to the COPY button when it is being
//                     offered and the foot when it is not. Left and right pick the choice rather
//                     than moving.
//     COPY            one button, so up and down are the only keys it has: back to the visibility
//                     row, on to the foot.
//     ACTION/TOGGLE   the foot is a ROW: left and right move along it, up returns to the column.
//                     Left off the action returns to the list, the way the browser's ACTION returns
//                     to its rows. Down goes nowhere, because there is nothing below it.
//
// What exists depends on the panel. With the settings closed there are no fields and no visibility
// row, so up from the foot leaves the form; while a server is running there is no toggle either, so
// the foot is one button wide; and the SERVER box CUSTOM and NEW open has the fields and the
// visibility row with NO experience list above them. All of it is told to this unit rather than
// assumed, because a focus that can land on something not drawn is the invisible-but-reachable bug
// in its keyboard form.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_HOSTFOCUS_COMPUTE_H
#define ZX_HOSTFOCUS_COMPUTE_H

namespace zx
{

enum class HostSlot
{
	// [rc4l] The experience rows. Added because they had NO keyboard at all: the arrows walked the
	// form and never reached the list, so the primary choice on a screen headed SELECT AN EXPERIENCE
	// TO HOST could only be made with a mouse. The server list beside it has been navigable since
	// browserfocus_compute existed.
	List,

	Field,		// one of the text boxes; `field` says which

	// [rc4l] One row of the gameplay panel; `field` is which, counted in the order they are drawn.
	//
	// It is a slot rather than part of Field because the two are never on screen together: the form
	// replaces the panel. What the row IS -- a slider, an axis of pills -- is the caller's business,
	// and deliberately not this unit's: it would have to be told the kind of every row to answer
	// questions it does not ask, and the two would then have to agree about the order twice.
	Gameplay,

	Visibility,	// the INTERNET / HOME row

	// [rc4l] COPY THIS TO NEW, under the visibility row. Offered only when every file the chosen
	// experience asks for is already on the machine -- a copy of something you cannot load is a
	// prefilled screen that refuses to start, and finding that out one screen later is worse than
	// not being offered it.
	Copy,

	Action,		// PLAY NOW! and its other faces
	Toggle,		// SETTINGS / BACK, beside the action

	// Nothing on the form has it. The caller reads this as "hand the arrows back to the tabs".
	Away,
};

struct HostFocusPos
{
	HostSlot slot;
	int field;			// meaningful only when slot is Field

	HostFocusPos() : slot(HostSlot::Action), field(0) {}
	HostFocusPos(HostSlot s, int f) : slot(s), field(f) {}
};

enum class HostNavKey
{
	Up,
	Down,
	Left,
	Right,
};

struct HostNavResult
{
	HostFocusPos pos;	// where focus ends up, unchanged when the key did something else

	// [rc4l] Movement and traversal are separate answers, the same split browserfocus_compute makes.
	// A key that changes the visibility choice does not move focus, and a key that moves focus does
	// not change the choice. Returning both from one call is what stops the caller inventing its own
	// rule for the overlap.
	// -1 or +1 on a row that HAS choices -- the visibility row, or whichever gameplay row is
	// focused. The caller knows which of those it is from the slot; this only says which way.
	int choiceStep;
	int rowStep;		// -1 or +1 on the experience list, 0 otherwise
	bool caret;			// the key belongs to the text caret; the caller passes it to the field

	HostNavResult() : choiceStep(0), rowStep(0), caret(false) {}
};

// `rowStep` is how far to move the experience selection, which the caller applies to its own list --
// the same movement/traversal split as above.
//
// `fieldCount` is how many text boxes there are. `hasFields` covers the fields AND the visibility
// row, which appear and disappear together with the settings. `hasToggle` is whether the SETTINGS
// button is beside the action. `gameplayRows` is how many rows the gameplay panel is drawing, which
// is zero whenever the form is open, while a server is running, or for an experience that offers
// nothing to decide. `hasCopy` is whether the COPY button is under the visibility row, which needs
// the settings open AND every file already downloaded.
//
// A position that no longer exists -- a field while the settings are shut, a gameplay row on an
// experience that has none -- is corrected rather than honoured, because the panel can change
// underneath a focus that was legitimate when it was set.
//
// `hasList` is whether the experience list is on screen at all. False on the SERVER box that CUSTOM
// and NEW open, which is this same form with nothing above it -- so every crossing that would reach
// the list is answered here instead of by a caller inventing its own rule for a screen the diagram
// above does not describe.
HostNavResult ComputeHostNav(HostFocusPos pos, HostNavKey key, int fieldCount,
                             bool hasFields, bool hasToggle, int gameplayRows, bool hasCopy,
                             bool hasList);

// The position to correct to when `pos` names something not currently on screen. Returns `pos`
// unchanged when it is already valid.
HostFocusPos ClampHostFocus(HostFocusPos pos, int fieldCount, bool hasFields, bool hasToggle,
                            int gameplayRows, bool hasCopy, bool hasList);

// [rc4l] Where LEFT lands when a text field finally gives it back.
//
// The field owns WHEN -- only it knows whether the caret still has anywhere to go, and that rule is
// textinput_compute's ArrowLeavesField. This owns WHERE, so the destination is written once and the
// answer cannot drift from the one the action button gives for the same key.
HostFocusPos HostLeftOfTheForm(bool hasList);

} // namespace zx

#endif // ZX_HOSTFOCUS_COMPUTE_H
