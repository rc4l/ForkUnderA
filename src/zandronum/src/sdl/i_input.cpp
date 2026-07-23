#include <SDL.h>
#include <ctype.h>
#include "doomtype.h"
#include "c_dispatch.h"
#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "i_input.h"
#include "v_video.h"

#include "d_main.h"
#include "d_event.h"
#include "d_gui.h"
#include "c_console.h"
#include "c_cvars.h"
#include "i_system.h"
#include "dikeys.h"
#include "templates.h"
#include "s_sound.h"
// [BB] New #includes.
#include "chat.h"

static void I_CheckGUICapture ();
static void I_CheckNativeMouse ();

bool GUICapture;
static bool NativeMouse = true;

extern int paused;

CVAR (Bool,  use_mouse,				true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool,  m_noprescale,			false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool,	 m_filter,				false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR (Bool,  sdl_nokeyrepeat,		false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

// [EP] Allows to keep the sound turned on, when the client is not the active app.
CVAR (Bool,	 cl_soundwhennotactive, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

EXTERN_CVAR (Bool, fullscreen)

extern int WaitingForKey;
extern constate_e ConsoleState;

extern SDL_Surface *cursorSurface;
extern SDL_Rect cursorBlit;

#include "computation/keymap_compute.h"

// [rc4l] SDL2 keysyms for non-ASCII keys are >= 1<<30, so they cannot index a table; scancodes are a
// dense, physically-positional enum, which is what DIK codes already are.
static BYTE ScancodeToDIK[SDL_NUM_SCANCODES], DownState[SDL_NUM_SCANCODES];

static WORD DIKToScancode[256] =
{
	0, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
	SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB,
	SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I,
	SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_RETURN, SDL_SCANCODE_LCTRL, SDL_SCANCODE_A, SDL_SCANCODE_S,
	SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON,
	SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_GRAVE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V,
	SDL_SCANCODE_B, SDL_SCANCODE_N, SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_KP_MULTIPLY,
	SDL_SCANCODE_LALT, SDL_SCANCODE_SPACE, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4, SDL_SCANCODE_F5,
	SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9, SDL_SCANCODE_F10, SDL_SCANCODE_NUMLOCKCLEAR, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_KP_7,
	SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_1,
	SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_PERIOD, 0, 0, 0, SDL_SCANCODE_F11,
	SDL_SCANCODE_F12, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, SDL_SCANCODE_F13, SDL_SCANCODE_F14, SDL_SCANCODE_F15, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, SDL_SCANCODE_KP_EQUALS, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RCTRL, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, SDL_SCANCODE_KP_DIVIDE, 0, SDL_SCANCODE_SYSREQ,
	SDL_SCANCODE_RALT, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, SDL_SCANCODE_PAUSE, 0, SDL_SCANCODE_HOME,
	SDL_SCANCODE_UP, SDL_SCANCODE_PAGEUP, 0, SDL_SCANCODE_LEFT, 0, SDL_SCANCODE_RIGHT, 0, SDL_SCANCODE_END,
	SDL_SCANCODE_DOWN, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_INSERT, SDL_SCANCODE_DELETE, 0, 0, 0, 0,
	0, 0, 0, SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI, SDL_SCANCODE_MENU, SDL_SCANCODE_POWER, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

static void FlushDIKState (int low=0, int high=NUM_KEYS-1)
{
}

static void InitKeySymMap ()
{
	ComputeInvertKeyTable(DIKToScancode, 256, ScancodeToDIK, SDL_NUM_SCANCODES);
	ScancodeToDIK[0] = 0;
	// [rc4l] The right-hand modifiers report as their left-hand equivalents, as they did under SDL 1.2.
	ScancodeToDIK[SDL_SCANCODE_RSHIFT] = DIK_LSHIFT;
	ScancodeToDIK[SDL_SCANCODE_RCTRL] = DIK_LCONTROL;
	ScancodeToDIK[SDL_SCANCODE_RALT] = DIK_LMENU;
	ScancodeToDIK[SDL_SCANCODE_PRINTSCREEN] = DIK_SYSRQ;
}

static void I_CheckGUICapture ()
{
	bool wantCapt;

	if (menuactive == MENU_Off)
	{
		wantCapt = ConsoleState == c_down || ConsoleState == c_falling || CHAT_GetChatMode();
	}
	else
	{
		wantCapt = (menuactive == MENU_On || menuactive == MENU_OnNoPause);
	}

	if (wantCapt != GUICapture)
	{
		GUICapture = wantCapt;
		if (wantCapt)
		{
			int x, y;
			SDL_GetMouseState (&x, &y);
			cursorBlit.x = x;
			cursorBlit.y = y;

			FlushDIKState ();
			memset (DownState, 0, sizeof(DownState));
			// [rc4l] SDL2 delivers typed characters as SDL_TEXTINPUT rather than a unicode field on the key event.
			SDL_StartTextInput ();
		}
		else
		{
			SDL_StopTextInput ();
		}
	}
}

void I_SetMouseCapture()
{
}

void I_ReleaseMouseCapture()
{
}

static void CenterMouse ()
{
	SDL_WarpMouseInWindow (SDL_GL_GetCurrentWindow (), screen->GetWidth()/2, screen->GetHeight()/2);
	SDL_PumpEvents ();
	SDL_GetRelativeMouseState (NULL, NULL);
}

static void PostMouseMove (int x, int y)
{
	static int lastx = 0, lasty = 0;
	event_t ev = { 0,0,0,0,0,0,0 };
	
	if (m_filter)
	{
		ev.x = (x + lastx) / 2;
		ev.y = (y + lasty) / 2;
	}
	else
	{
		ev.x = x;
		ev.y = y;
	}
	lastx = x;
	lasty = y;
	if (ev.x | ev.y)
	{
		ev.type = EV_Mouse;
		D_PostEvent (&ev);
	}
}

static void MouseRead ()
{
	int x, y;

	if (NativeMouse)
	{
		return;
	}

	SDL_GetRelativeMouseState (&x, &y);
	if (!m_noprescale)
	{
		x *= 3;
		y *= 2;
	}
	if (x | y)
	{
		CenterMouse ();
		PostMouseMove (x, -y);
	}
}

static void WheelMoved(event_t *event)
{
	if (GUICapture)
	{
		if (event->type != EV_KeyUp)
		{
			SDL_Keymod mod = SDL_GetModState();
			event->type = EV_GUI_Event;
			event->subtype = event->data1 == KEY_MWHEELUP ? EV_GUI_WheelUp : EV_GUI_WheelDown;
			event->data1 = 0;
			event->data3 = ((mod & KMOD_SHIFT) ? GKM_SHIFT : 0) |
						  ((mod & KMOD_CTRL) ? GKM_CTRL : 0) |
						  ((mod & KMOD_ALT) ? GKM_ALT : 0);
			D_PostEvent(event);
		}
	}
	else
	{
		D_PostEvent(event);
	}
}

CUSTOM_CVAR(Int, mouse_capturemode, 1, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)
{
	if (self < 0) self = 0;
	else if (self > 2) self = 2;
}

static bool inGame()
{
	switch (mouse_capturemode)
	{
	default:
	case 0:
		return gamestate == GS_LEVEL;
	case 1:
		return gamestate == GS_LEVEL || gamestate == GS_INTERMISSION || gamestate == GS_FINALE;
	case 2:
		return true;
	}
}

static void I_CheckNativeMouse ()
{
	SDL_Window *window = SDL_GL_GetCurrentWindow ();
	const Uint32 flags = window != NULL ? SDL_GetWindowFlags (window) : 0;
	bool focus = (flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_SHOWN))
			== (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_SHOWN);

	bool wantNative = !focus || (!use_mouse || GUICapture || paused || demoplayback || !inGame());

	if (wantNative != NativeMouse)
	{
		NativeMouse = wantNative;
		SDL_ShowCursor (wantNative ? cursorSurface == NULL : 0);
		if (wantNative)
		{
			SDL_SetRelativeMouseMode (SDL_FALSE);
			FlushDIKState (KEY_MOUSE1, KEY_MOUSE8);
		}
		else
		{
			SDL_SetRelativeMouseMode (SDL_TRUE);
			CenterMouse ();
		}
	}
}

void MessagePump (const SDL_Event &sev)
{
	static int lastx = 0, lasty = 0;
	int x, y;
	event_t event = { 0,0,0,0,0,0,0 };
	
	switch (sev.type)
	{
	case SDL_QUIT:
		exit (0);

	// [rc4l] SDL2 replaces SDL_ACTIVEEVENT with per-window focus events.
	case SDL_WINDOWEVENT:
		if (sev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
			sev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
		{
			const int gain = sev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
			if (!gain)
			{ // kill focus
				FlushDIKState ();
			}
			if (( NETWORK_GetState() != NETSTATE_CLIENT ) || ( cl_soundwhennotactive == false ))	// [EP]
				S_SetSoundPaused(gain);
		}
		break;

	// [rc4l] SDL2 reports the wheel as its own event instead of mouse buttons 4/5.
	case SDL_MOUSEWHEEL:
		if (sev.wheel.y != 0)
		{
			event.type = EV_KeyDown;
			event.data1 = sev.wheel.y > 0 ? KEY_MWHEELUP : KEY_MWHEELDOWN;
			WheelMoved(&event);
		}
		break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEMOTION:
		if (!GUICapture)
		{
			if(sev.type != SDL_MOUSEMOTION)
			{
				event.type = sev.type == SDL_MOUSEBUTTONDOWN ? EV_KeyDown : EV_KeyUp;
				/* These button mappings work with my Gentoo system using the
				* evdev driver and a Logitech MX510 mouse. Whether or not they
				* carry over to other Linux systems, I have no idea, but I sure
				* hope so. (Though buttons 11 and 12 are kind of useless, since
				* they also trigger buttons 4 and 5.)
				*/
				switch (sev.button.button)
				{
				// [rc4l] SDL2 normalises these: 1/2/3 are left/middle/right and 4+ are the extra buttons.
				case 1:		event.data1 = KEY_MOUSE1;		break;
				case 2:		event.data1 = KEY_MOUSE3;		break;
				case 3:		event.data1 = KEY_MOUSE2;		break;
				case 4:		event.data1 = KEY_MOUSE4;		break;
				case 5:		event.data1 = KEY_MOUSE5;		break;
				case 6:		event.data1 = KEY_MOUSE6;		break;
				case 7:		event.data1 = KEY_MOUSE7;		break;
				case 8:		event.data1 = KEY_MOUSE8;		break;
				default:	printf("SDL mouse button %s %d\n",
					sev.type == SDL_MOUSEBUTTONDOWN ? "down" : "up", sev.button.button);	break;
				}
				if (event.data1 != 0)
				{
					D_PostEvent(&event);
				}
			}
		}
		else if (sev.type == SDL_MOUSEMOTION || (sev.button.button >= 1 && sev.button.button <= 3))
		{
			int x, y;
			SDL_GetMouseState (&x, &y);

			cursorBlit.x = event.data1 = x;
			cursorBlit.y = event.data2 = y;
			event.type = EV_GUI_Event;
			if(sev.type == SDL_MOUSEMOTION)
				event.subtype = EV_GUI_MouseMove;
			else
			{
				event.subtype = sev.type == SDL_MOUSEBUTTONDOWN ? EV_GUI_LButtonDown : EV_GUI_LButtonUp;
				event.subtype += (sev.button.button - 1) * 3;
			}
			D_PostEvent(&event);
		}
		break;

	case SDL_KEYDOWN:
	case SDL_KEYUP:
	{
		const SDL_Scancode scan = sev.key.keysym.scancode;
		if (scan >= SDL_NUM_SCANCODES)
			break;

		if (!GUICapture)
		{
			event.type = sev.type == SDL_KEYDOWN ? EV_KeyDown : EV_KeyUp;
			event.data1 = ScancodeToDIK[scan];
			if (event.data1)
			{
				if (sev.key.keysym.sym < 256)
				{
					event.data2 = sev.key.keysym.sym;
				}
				D_PostEvent (&event);
			}
		}
		else
		{
			event.type = EV_GUI_Event;
			event.subtype = sev.type == SDL_KEYDOWN ? EV_GUI_KeyDown : EV_GUI_KeyUp;
			event.data3 = ((sev.key.keysym.mod & KMOD_SHIFT) ? GKM_SHIFT : 0) |
						  ((sev.key.keysym.mod & KMOD_CTRL) ? GKM_CTRL : 0) |
						  ((sev.key.keysym.mod & KMOD_ALT) ? GKM_ALT : 0);

			// [rc4l] SDL2 repeats keys itself, but the DownState table still distinguishes a
			// genuine repeat from a fresh press, and honours sdl_nokeyrepeat.
			if (event.subtype == EV_GUI_KeyDown)
			{
				if (DownState[scan])
				{
					if (sdl_nokeyrepeat) break;
					event.subtype = EV_GUI_KeyRepeat;
				}
				DownState[scan] = 1;
			}
			else
			{
				DownState[scan] = 0;
			}

			switch (sev.key.keysym.sym)
			{
			case SDLK_KP_ENTER:	event.data1 = GK_RETURN;	break;
			case SDLK_BACKSPACE:	event.data1 = GK_BACKSPACE;	break;
			case SDLK_PAGEUP:	event.data1 = GK_PGUP;		break;
			case SDLK_PAGEDOWN:	event.data1 = GK_PGDN;		break;
			case SDLK_END:		event.data1 = GK_END;		break;
			case SDLK_HOME:		event.data1 = GK_HOME;		break;
			case SDLK_LEFT:		event.data1 = GK_LEFT;		break;
			case SDLK_RIGHT:	event.data1 = GK_RIGHT;		break;
			case SDLK_UP:		event.data1 = GK_UP;		break;
			case SDLK_DOWN:		event.data1 = GK_DOWN;		break;
			case SDLK_DELETE:	event.data1 = GK_DEL;		break;
			case SDLK_ESCAPE:	event.data1 = GK_ESCAPE;	break;
			case SDLK_F1:		event.data1 = GK_F1;		break;
			case SDLK_F2:		event.data1 = GK_F2;		break;
			case SDLK_F3:		event.data1 = GK_F3;		break;
			case SDLK_F4:		event.data1 = GK_F4;		break;
			case SDLK_F5:		event.data1 = GK_F5;		break;
			case SDLK_F6:		event.data1 = GK_F6;		break;
			case SDLK_F7:		event.data1 = GK_F7;		break;
			case SDLK_F8:		event.data1 = GK_F8;		break;
			case SDLK_F9:		event.data1 = GK_F9;		break;
			case SDLK_F10:		event.data1 = GK_F10;		break;
			case SDLK_F11:		event.data1 = GK_F11;		break;
			case SDLK_F12:		event.data1 = GK_F12;		break;
			default:
				if (sev.key.keysym.sym < 256)
				{
					event.data1 = sev.key.keysym.sym;
				}
				break;
			}
			// [rc4l] Typed characters now arrive separately as SDL_TEXTINPUT, so this only posts the key itself.
			if (event.data1 < 128)
			{
				event.data1 = toupper(event.data1);
				D_PostEvent (&event);
			}
		}
		break;
	}

	// [rc4l] SDL2's replacement for SDL 1.2's unicode field on the key event.
	case SDL_TEXTINPUT:
		if (GUICapture)
		{
			const unsigned char ch = (unsigned char)sev.text.text[0];
			if (ch != 0 && !iscntrl(ch))
			{
				event.type = EV_GUI_Event;
				event.subtype = EV_GUI_Char;
				event.data1 = ch;
				event.data2 = (SDL_GetModState () & KMOD_ALT) != 0;
				event.data3 = 0;
				D_PostEvent (&event);
			}
		}
		break;

	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		if (!GUICapture)
		{
			event.type = sev.type == SDL_JOYBUTTONDOWN ? EV_KeyDown : EV_KeyUp;
			event.data1 = KEY_FIRSTJOYBUTTON + sev.jbutton.button;
			if(event.data1 != 0)
				D_PostEvent(&event);
		}
		break;
	}
}

void I_GetEvent ()
{
	SDL_Event sev;
	
	while (SDL_PollEvent (&sev))
	{
		MessagePump (sev);
	}
	if (use_mouse)
	{
		MouseRead ();
	}
}

void I_StartTic ()
{
	I_CheckGUICapture ();
	I_CheckNativeMouse ();
	I_GetEvent ();
}

void I_ProcessJoysticks ();
void I_StartFrame ()
{
	if (ScancodeToDIK[SDL_SCANCODE_BACKSPACE] == 0)
	{
		InitKeySymMap ();
	}

	I_ProcessJoysticks();
}
