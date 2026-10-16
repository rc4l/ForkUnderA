// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The SDL layer's host on a desktop (sdlhost.h): its own window, its own crash catcher, and stderr.

#include <signal.h>

#include "sdlhost.h"
#include "version.h"   // [rc4l] GAMENAMELOWERCASE

extern "C" int cc_install_handlers(int, char**, int, int*, const char*, int(*)(char*, char*));

namespace zx { namespace sdlhost {

void Start(int argc, char **argv, void (*)(), int (*crashInfo)(char *, char *))
{
	// [rc4l] Apple's own crash reporter handles these.
#ifndef __APPLE__
	int s[4] = { SIGSEGV, SIGILL, SIGFPE, SIGBUS };
	cc_install_handlers(argc, argv, 4, s, GAMENAMELOWERCASE "-crash.log", crashInfo);
#else
	(void)argc; (void)argv; (void)crashInfo;
#endif
}

void AddArgs(DArgs *) {}

bool OwnsFullscreen() { return false; }

void SetFullscreen(bool) {}

bool MouseHeld() { return true; }

void ShowFatal(const char *) {}

bool CanAskForIWad() { return true; }

bool CaptureFrameLater(const char *) { return false; }

}} // namespace zx::sdlhost
