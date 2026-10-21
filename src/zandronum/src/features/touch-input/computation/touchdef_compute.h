// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The on-screen controls a FUATOUCH lump defines, engine-free: the model the parser fills, how lumps layer, where each control lies on a view, which pointer drives what each tic, and when the overlay shows.

#ifndef ZX_TOUCHDEF_COMPUTE_H
#define ZX_TOUCHDEF_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace touch {

// [rc4l] A region of the view as fractions of its width and height.
struct TouchZone { float x0, y0, x1, y1; };

// [rc4l] The stick: where a thumb may land to take it, its radius in virtual pixels, and the radial dead zone as a fraction of the radius.
struct TouchStickDef { TouchZone zone; float radius; float deadZone; };

// [rc4l] The look drag: where a finger may land to aim, and how many mouse units a pixel of drag is.
struct TouchLookDef { TouchZone zone; float sensitivity; };

// [rc4l] A button: its id, the key it presses (a DEFBINDS name resolved by the parser) or the command a spare joystick key is bound to, its centre as fractions of the view, its size in virtual pixels, and what it shows.
struct TouchButtonDef { std::string id; int key; std::string command; float ax, ay; float size; std::string label; std::string icon; };

// [rc4l] What a portrait or landscape block changes about a button: its place, its size, either or both.
struct TouchButtonOverride { std::string id; bool hasAt; float ax, ay; bool hasSize; float size; };

struct TouchLayout
{
	TouchStickDef stick;
	TouchLookDef look;
	std::vector<TouchButtonDef> buttons;
	std::vector<TouchButtonOverride> portrait, landscape;
};

// [rc4l] The layout before any lump: a stick on the left third, a look drag on the right, no buttons.
TouchLayout DefaultTouchLayout();

// [rc4l] `clear`: everything a lump said before is dropped, the stick and look back to their defaults.
void ClearTouchLayout(TouchLayout &layout);

// [rc4l] A `button` line: the one with its id replaced, else appended, so a later lump retunes or adds.
void MergeTouchButton(TouchLayout &layout, const TouchButtonDef &button);

// [rc4l] A button line inside a portrait or landscape block, merged the same way into that block's overrides.
void MergeTouchOverride(std::vector<TouchButtonOverride> &overrides, const TouchButtonOverride &override);

// [rc4l] The view the controls are laid on: its size in pixels, the safe-area insets inside it, and whether it is taller than wide.
struct TouchViewIn { int width, height; float insetLeft, insetTop, insetRight, insetBottom; bool portrait; };

// [rc4l] A rectangle in view pixels.
struct TouchRect { float x, y, w, h; };

// [rc4l] A button's place on the view and which layout entry it is.
struct TouchButtonRect { int index; TouchRect rect; };

// [rc4l] The controls laid on a view: the zones and radius in pixels, each button's rectangle, and the virtual-to-view scale used.
struct TouchRects { TouchRect stickZone, lookZone; float stickRadius; std::vector<TouchButtonRect> buttons; float scale; };

// [rc4l] Where the controls lie on a view: virtual pixels scaled as the freeform menu's 480x270 canvas is (the smaller of width over 480 and height over 270) and by `sizeScale`, anchors inside the safe area, the orientation's overrides applied, and everything mirrored for a left-handed player.
TouchRects ComputeTouchRects(const TouchLayout &layout, const TouchViewIn &view, float sizeScale, bool leftHanded);

// [rc4l] Whether a point lies in a rectangle.
bool TouchRectHas(const TouchRect &rect, float x, float y);

// [rc4l] A pointer this tic: its id, where it is, where it landed, and whether it lifted since the last tic.
struct TouchPointerIn { int id; float x, y, ox, oy; bool ended; };

// [rc4l] What a pointer is doing: nothing yet, driving the stick, dragging the look, or on buttons; a role is taken on arrival and kept until the pointer lifts.
enum { kTouchNone = 0, kTouchStick = 1, kTouchLook = 2, kTouchButton = 3 };

// [rc4l] The pointers the overlay follows between tics: each one's role and, for the stick its origin, for the look its last place.
struct TouchSlot { int id; int role; float ox, oy; };
const int kTouchSlots = 8;
struct TouchFrameState { TouchSlot slots[kTouchSlots]; };

// [rc4l] A tic's input from the pointers: the stick's offset from its origin in pixels (and whether a thumb holds it), the look's drag in pixels, the buttons held (bit = layout index), and the state to carry.
struct TouchFrameOut { bool stickHeld; float stickDx, stickDy; float lookDx, lookDy; unsigned buttons; TouchFrameState next; };

// [rc4l] The state with no pointers.
TouchFrameState EmptyTouchFrameState();

// [rc4l] The tic's input: each pointer kept to the role it arrived with (a button's finger may slide between buttons, never into the stick), a new one taking the first of stick, look or button the place it landed offers, measured from there, and one that lifted releasing what it held after this tic.
TouchFrameOut ComputeTouchFrame(const TouchFrameState &prev, const TouchPointerIn *pointers, int count, const TouchRects &rects);

// [rc4l] fua_touch: off, on, or auto (shown once a touch screen has been seen, hidden by a keyboard).
enum { kTouchOff = 0, kTouchOn = 1, kTouchAuto = 2 };

// [rc4l] What decides whether the overlay is on the screen: the game's state and the player's choice.
struct TouchShownIn { bool inLevel, menuActive, consoleOpen, automapActive, paused, demoPlayback; int mode; bool touchSeen, keyboardSinceTouch; };

// [rc4l] The overlay shows in play alone: a level with no menu, console or automap, not paused, not a demo; and only when wanted.
bool ComputeTouchShown(const TouchShownIn &in);

}} // namespace zx::touch

#endif // ZX_TOUCHDEF_COMPUTE_H
