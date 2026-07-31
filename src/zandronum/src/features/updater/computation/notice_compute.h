// [rc4l] Pure state machine for the main-menu "update available" notice (the bottom-right chip). The
// engine's DUpdateMainMenu::MenuEvent/MouseEvent are thin wrappers: they map the input, call these,
// apply the returned state, and perform the returned action. Keeping the focus/selection logic here
// (engine-free) means the interaction rules -- which were fiddly to get right (keyboard vs mouse,
// restoring the previous list item on exit, mutual exclusion with the list) -- are unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_NOTICE_COMPUTE_H
#define ZX_NOTICE_COMPUTE_H

namespace zx { namespace updater {

// The menu keys the notice cares about (mapped from the engine's MKEY_* in the wrapper). Anything
// else is Other.
enum class NoticeKey { Left, Right, Up, Down, Enter, Back, Other };

// What the engine should do after a key is processed.
enum class NoticeAction {
	Handled,   // event fully consumed by the notice; do nothing else
	Activate,  // open the download confirmation (chip was chosen)
	Delegate,  // notice did not consume it -> let the base list menu handle this key
};

// Chip focus + the list's selection index it shadows. `selected` is the list's current item
// (-1 = none). `prevSelected` is the item to restore to when leaving the chip.
struct NoticeState { bool focused; int selected; int prevSelected; };

struct NoticeStep { NoticeState state; NoticeAction action; };

// Advance the notice state for one key press. `available` is zx::updater::IsAvailable(): when false
// the chip can never hold focus and every key delegates to the list. Rules (when available):
//   * Not focused + Right -> focus the chip (remember the list item, clear the list selection).
//   * Not focused + anything else -> Delegate (the list handles it).
//   * Focused + Enter -> Activate.
//   * Focused + Right -> Handled (already the rightmost element; no-op).
//   * Focused + Left/Back -> unfocus and restore the remembered list item, Handled.
//   * Focused + Up/Down -> unfocus and Delegate, so the list moves from the cleared selection.
//   * Focused + anything else -> Handled (swallowed while the chip holds focus).
NoticeStep ComputeNoticeKey(NoticeState s, bool available, NoticeKey key);

// Whether a mouse event should act on selection. True only when the pointer actually moved since the
// last event, or the event is a click/release. A resting pointer (a move event with unchanged coords)
// returns false, so it can't keep re-selecting and fighting the keyboard.
bool ComputeMouseActs(int lastX, int lastY, int x, int y, bool isClickOrRelease);

} } // namespace zx::updater

#endif
