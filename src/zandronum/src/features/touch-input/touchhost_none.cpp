// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The host seam's touch read on a platform with its own input layer and no touch screen (Cocoa, Win32), where the SDL host files are not built.

#include "posix/sdl/sdlhost.h"

namespace zx { namespace sdlhost {

bool ReadTouch(TouchPointer *, int, int &count, TouchView &) { count = 0; return false; }

}} // namespace zx::sdlhost
