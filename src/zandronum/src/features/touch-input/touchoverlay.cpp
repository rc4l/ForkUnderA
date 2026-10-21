// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/touchoverlay.h"

#include "features/touch-input/computation/touchdef_compute.h"
#include "features/touch-input/computation/touchinput_compute.h"
#include "features/touch-input/touchdef.h"
#include "features/touch-input/touchjoystick.h"
#include "posix/sdl/sdlhost.h"

#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "d_event.h"
#include "doomstat.h"
#include "m_joy.h"
#include "menu/menu.h"
#include "textures/textures.h"
#include "v_font.h"
#include "v_text.h"
#include "v_video.h"
#include "zstring.h"

#include <math.h>

// [rc4l] fua_touch: 0 off, 1 on, 2 auto (shown once a touch screen has been seen, hidden by a keyboard until the next touch).
CVAR(Int, fua_touch, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, fua_touch_size, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, fua_touch_opacity, 0.35f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Float, fua_touch_look, 1.f, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, fua_touch_invert, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, fua_touch_lefthanded, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace zx { namespace touch {

namespace {

const int kMaxPointers = 16;
const int kMaxButtons = 32;   // [rc4l] the button mask's width
// [rc4l] A pixel of drag as the mouse's prescale reads a count: three across and two up (MouseRead).
const float kLookPrescaleX = 3.f, kLookPrescaleY = 2.f;

TouchFrameState g_state = EmptyTouchFrameState();
TouchRects g_rects;
TouchFrameOut g_frame;
bool g_shown = false;
bool g_keyboardSinceTouch = false;
unsigned g_buttons = 0;

TouchShownIn ShownIn(bool touchSeen)
{
	const TouchShownIn in = {
		gamestate == GS_LEVEL, menuactive != MENU_Off, ConsoleState != c_up, automapactive, paused != 0, demoplayback,
		fua_touch, touchSeen, g_keyboardSinceTouch };
	return in;
}

// [rc4l] The buttons' keys in layout order, for Joy_GenerateButtonEvents.
int ButtonKeys(int keys[kMaxButtons])
{
	const TouchLayout &layout = Layout();
	const int n = (int)layout.buttons.size() < kMaxButtons ? (int)layout.buttons.size() : kMaxButtons;
	for (int i = 0; i < n; i++) keys[i] = layout.buttons[i].key;
	return n;
}

void PostButtons(unsigned buttons)
{
	int keys[kMaxButtons];
	const int n = ButtonKeys(keys);
	Joy_GenerateButtonEvents(g_buttons, buttons, n, keys);
	g_buttons = buttons;
}

// [rc4l] Nothing held: the stick centred, every button up, the pointers forgotten.
void Release()
{
	SetStick(0.f, 0.f);
	PostButtons(0);
	g_state = EmptyTouchFrameState();
	g_frame.stickHeld = false;
}

// [rc4l] A label centred at (cx, cy), each glyph at the overlay's own scale through explicit sizes, since DrawText's scale tags read globals the web's canvas resize does not refresh (ScaleFac).
void DrawLabel(const char *text, float cx, float cy, float scale, EColorRange color)
{
	FFont *const font = SmallFont;
	const int th = (int)(font->GetHeight() * scale);
	int tw = 0;
	for (const char *c = text; *c != 0; c++) { int w = 0; font->GetChar(*c, &w); tw += (int)(w * scale); }
	int x = (int)(cx - tw * 0.5f);
	const int y = (int)(cy - th * 0.5f);
	FRemapTable *const range = font->GetColorTranslation(color);
	for (const char *c = text; *c != 0; c++)
	{
		int w = 0;
		FTexture *const pic = font->GetChar(*c, &w);
		if (pic != NULL) screen->DrawTexture(pic, x, y, DTA_DestWidth, (int)(w * scale), DTA_DestHeight, th, DTA_Translation, range, TAG_DONE);
		x += (int)(w * scale);
	}
}

void DrawPill(const TouchRect &r, float alpha, bool lit)
{
	// [rc4l] A rounded look from a lighter rim under a darker fill; the fill brightens while held.
	screen->Dim(PalEntry(230, 232, 236), alpha * 0.5f, (int)r.x, (int)r.y, (int)r.w, (int)r.h);
	const int in = (int)(r.w * 0.08f) + 1;
	screen->Dim(lit ? PalEntry(255, 255, 255) : PalEntry(24, 26, 32), lit ? alpha : alpha * 1.4f, (int)r.x + in, (int)r.y + in, (int)r.w - 2 * in, (int)r.h - 2 * in);
}

} // namespace

void NoteKeyboard() { g_keyboardSinceTouch = true; }

bool Shown() { return g_shown; }

// [rc4l] What the overlay holds this tic, for fua_touch_status: the buttons, whether the stick is held and its offset, how many buttons are laid out, and the shown rule's inputs.
static void Status(FString &out)
{
	const TouchShownIn in = ShownIn(Device() != NULL);
	out.Format("touch shown=%d buttons=%u stick=%d dx=%.0f dy=%.0f rects=%u level=%d menu=%d console=%d automap=%d paused=%d demo=%d mode=%d seen=%d keyboard=%d",
		g_shown ? 1 : 0, g_frame.buttons, g_frame.stickHeld ? 1 : 0, g_frame.stickDx, g_frame.stickDy, (unsigned)g_rects.buttons.size(),
		in.inLevel, in.menuActive, in.consoleOpen, in.automapActive, in.paused, in.demoPlayback, in.mode, in.touchSeen, in.keyboardSinceTouch);
}

void Tick()
{
	sdlhost::TouchPointer pointers[kMaxPointers];
	sdlhost::TouchView view;
	int count = 0;
	if (!sdlhost::ReadTouch(pointers, kMaxPointers, count, view))
	{
		if (g_shown) { g_shown = false; Release(); }
		return;
	}
	if (count > 0) { NoteTouchSeen(); g_keyboardSinceTouch = false; }
	const bool shown = ComputeTouchShown(ShownIn(Device() != NULL));
	if (!shown)
	{
		if (g_shown) Release();
		g_shown = false;
		return;
	}
	g_shown = true;

	const TouchLayout &layout = Layout();
	const TouchViewIn viewIn = { view.width, view.height, view.insetLeft, view.insetTop, view.insetRight, view.insetBottom, view.portrait };
	g_rects = ComputeTouchRects(layout, viewIn, fua_touch_size, fua_touch_lefthanded);

	TouchPointerIn in[kMaxPointers];
	for (int i = 0; i < count; i++) { in[i].id = pointers[i].id; in[i].x = pointers[i].x; in[i].y = pointers[i].y; in[i].ox = pointers[i].ox; in[i].oy = pointers[i].oy; in[i].ended = pointers[i].ended; }
	g_frame = ComputeTouchFrame(g_state, in, count, g_rects);
	g_state = g_frame.next;

	const TouchAxes axes = g_frame.stickHeld ? ComputeTouchAxes(g_frame.stickDx, g_frame.stickDy, g_rects.stickRadius, layout.stick.deadZone) : TouchAxes();
	SetStick(axes.x, axes.y);

	int mouseX = 0, mouseY = 0;
	ComputeTouchLook(g_frame.lookDx * kLookPrescaleX, g_frame.lookDy * kLookPrescaleY, layout.look.sensitivity * fua_touch_look, fua_touch_invert, mouseX, mouseY);
	if (mouseX != 0 || mouseY != 0)
	{
		event_t ev = { 0, 0, 0, 0, 0, 0, 0 };
		ev.type = EV_Mouse;
		ev.x = mouseX;
		ev.y = mouseY;
		D_PostEvent(&ev);
	}

	PostButtons(g_frame.buttons);
}

void Draw()
{
	if (!g_shown) return;
	const TouchLayout &layout = Layout();
	const float alpha = fua_touch_opacity;

	// [rc4l] The stick: its base where the thumb landed while held, the knob following the thumb to the rim.
	for (int i = 0; i < kTouchSlots; i++)
	{
		const TouchSlot &s = g_state.slots[i];
		if (s.role != kTouchStick) continue;
		const float r = g_rects.stickRadius;
		const TouchRect base = { s.ox - r, s.oy - r, 2.f * r, 2.f * r };
		DrawPill(base, alpha * 0.6f, false);
		float kx = g_frame.stickDx, ky = g_frame.stickDy;
		const float len = sqrtf(kx * kx + ky * ky);
		if (len > r) { kx = kx / len * r; ky = ky / len * r; }
		const float knob = r * 0.45f;
		const TouchRect k = { s.ox + kx - knob, s.oy + ky - knob, 2.f * knob, 2.f * knob };
		DrawPill(k, alpha, true);
	}

	for (size_t i = 0; i < g_rects.buttons.size(); i++)
	{
		const TouchButtonRect &b = g_rects.buttons[i];
		const TouchButtonDef &def = layout.buttons[b.index];
		const bool lit = (g_frame.buttons & (1u << b.index)) != 0;
		DrawPill(b.rect, alpha, lit);
		// [rc4l] FTextureID's default constructor leaves it unset, so it is made invalid before the lookup; a garbage id drew a random texture and skipped the label.
		FTextureID icon;
		icon.SetInvalid();
		if (!def.icon.empty()) icon = TexMan.CheckForTexture(def.icon.c_str(), FTexture::TEX_MiscPatch);
		if (icon.isValid())
		{
			screen->DrawTexture(TexMan(icon), (int)b.rect.x, (int)b.rect.y, DTA_DestWidth, (int)b.rect.w, DTA_DestHeight, (int)b.rect.h, DTA_Alpha, FLOAT2FIXED(alpha + 0.4f > 1.f ? 1.f : alpha + 0.4f), TAG_DONE);
			continue;
		}
		DrawLabel(def.label.c_str(), b.rect.x + b.rect.w * 0.5f, b.rect.y + b.rect.h * 0.5f, g_rects.scale, lit ? CR_BLACK : CR_WHITE);
	}
}

}} // namespace zx::touch

// [rc4l] What the overlay holds this tic, for the page's checks: shown, the stick, the buttons.
CCMD(fua_touch_status)
{
	FString line;
	zx::touch::Status(line);
	Printf("%s\n", line.GetChars());
}
