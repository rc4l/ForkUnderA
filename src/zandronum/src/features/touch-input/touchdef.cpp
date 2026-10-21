// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/touch-input/touchdef.h"

#include "doomtype.h"
#include "tarray.h"
#include "zstring.h"
#include "c_bind.h"
#include "c_dispatch.h"
#include "doomdef.h"
#include "sc_man.h"
#include "w_wad.h"

namespace zx { namespace touch {

namespace {

TouchLayout g_layout = DefaultTouchLayout();

// [rc4l] The spare joystick keys a `command` button takes, in order of appearance: joy9 up to joy16, past the four DEFBINDS binds.
const int kFirstSpareKey = KEY_FIRSTJOYBUTTON + 8;
const int kSpareKeys = 8;

void ParseZone(FScanner &sc, TouchZone &zone)
{
	sc.MustGetFloat(); zone.x0 = (float)sc.Float;
	sc.MustGetFloat(); zone.y0 = (float)sc.Float;
	sc.MustGetFloat(); zone.x1 = (float)sc.Float;
	sc.MustGetFloat(); zone.y1 = (float)sc.Float;
}

void ParseStick(FScanner &sc, TouchStickDef &stick)
{
	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("zone")) ParseZone(sc, stick.zone);
		else if (sc.Compare("radius")) { sc.MustGetFloat(); stick.radius = (float)sc.Float; }
		else if (sc.Compare("deadzone")) { sc.MustGetFloat(); stick.deadZone = (float)sc.Float; }
		else sc.ScriptError("Unknown stick property '%s' in FUATOUCH", sc.String);
	}
}

void ParseLook(FScanner &sc, TouchLookDef &look)
{
	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("zone")) ParseZone(sc, look.zone);
		else if (sc.Compare("sensitivity")) { sc.MustGetFloat(); look.sensitivity = (float)sc.Float; }
		else sc.ScriptError("Unknown look property '%s' in FUATOUCH", sc.String);
	}
}

// [rc4l] `button <id> { key NAME | command "..."  at X Y  size N  label "..."  icon LUMP }`, a command taking the next spare joystick key and binding it unless the player has.
void ParseButton(FScanner &sc, TouchLayout &layout, int &nextSpare)
{
	sc.MustGetString();
	TouchButtonDef button;
	button.id = sc.String;
	button.key = 0; button.ax = 0.5f; button.ay = 0.5f; button.size = 60.f;
	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetString();
		if (sc.Compare("key"))
		{
			sc.MustGetString();
			button.key = C_KeyFromName(sc.String);
			if (button.key == 0) sc.ScriptError("Unknown key '%s' for button %s in FUATOUCH", sc.String, button.id.c_str());
		}
		else if (sc.Compare("command")) { sc.MustGetString(); button.command = sc.String; }
		else if (sc.Compare("at")) { sc.MustGetFloat(); button.ax = (float)sc.Float; sc.MustGetFloat(); button.ay = (float)sc.Float; }
		else if (sc.Compare("size")) { sc.MustGetFloat(); button.size = (float)sc.Float; }
		else if (sc.Compare("label")) { sc.MustGetString(); button.label = sc.String; }
		else if (sc.Compare("icon")) { sc.MustGetString(); button.icon = sc.String; }
		else sc.ScriptError("Unknown button property '%s' in FUATOUCH", sc.String);
	}
	if (button.key == 0 && !button.command.empty())
	{
		if (nextSpare >= kSpareKeys) sc.ScriptError("No spare joystick key left for button %s in FUATOUCH", button.id.c_str());
		button.key = kFirstSpareKey + nextSpare++;
		if (Bindings.GetBinding(button.key).IsEmpty()) Bindings.SetBind(button.key, button.command.c_str());
	}
	if (button.key == 0) sc.ScriptError("Button %s names no key and no command in FUATOUCH", button.id.c_str());
	if (button.label.empty()) button.label = button.id;
	MergeTouchButton(layout, button);
}

// [rc4l] `portrait { button <id> { at X Y  size N } ... }` and `landscape { ... }`: what changes in that orientation.
void ParseOrientation(FScanner &sc, std::vector<TouchButtonOverride> &overrides)
{
	sc.MustGetStringName("{");
	while (!sc.CheckString("}"))
	{
		sc.MustGetStringName("button");
		sc.MustGetString();
		TouchButtonOverride o;
		o.id = sc.String; o.hasAt = false; o.ax = 0.f; o.ay = 0.f; o.hasSize = false; o.size = 0.f;
		sc.MustGetStringName("{");
		while (!sc.CheckString("}"))
		{
			sc.MustGetString();
			if (sc.Compare("at")) { o.hasAt = true; sc.MustGetFloat(); o.ax = (float)sc.Float; sc.MustGetFloat(); o.ay = (float)sc.Float; }
			else if (sc.Compare("size")) { o.hasSize = true; sc.MustGetFloat(); o.size = (float)sc.Float; }
			else sc.ScriptError("Unknown orientation property '%s' in FUATOUCH", sc.String);
		}
		MergeTouchOverride(overrides, o);
	}
}

void ParseLump(int lump, TouchLayout &layout, int &nextSpare)
{
	FScanner sc(lump);
	while (sc.GetString())
	{
		if (sc.Compare("clear")) ClearTouchLayout(layout);
		else if (sc.Compare("stick")) ParseStick(sc, layout.stick);
		else if (sc.Compare("look")) ParseLook(sc, layout.look);
		else if (sc.Compare("button")) ParseButton(sc, layout, nextSpare);
		else if (sc.Compare("portrait")) ParseOrientation(sc, layout.portrait);
		else if (sc.Compare("landscape")) ParseOrientation(sc, layout.landscape);
		else sc.ScriptError("Unknown FUATOUCH keyword '%s'", sc.String);
	}
}

} // namespace

void LoadDefs()
{
	TouchLayout layout = DefaultTouchLayout();
	int nextSpare = 0;
	int lump, lastLump = 0;
	while ((lump = Wads.FindLump("FUATOUCH", &lastLump)) != -1)
		ParseLump(lump, layout, nextSpare);
	g_layout = layout;
}

const TouchLayout &Layout() { return g_layout; }

}} // namespace zx::touch
