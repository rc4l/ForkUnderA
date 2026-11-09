// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The OS's own folder picker: Windows' folder dialog, macOS's NSOpenPanel, zenity or kdialog on Linux, none in a browser.

#ifndef ZX_FOLDERPICKER_H
#define ZX_FOLDERPICKER_H

#include <string>

namespace zx { namespace wadfolders {

enum class PickResult
{
	Picked,
	Cancelled,
	Unavailable,	// [rc4l] no picker on this system, so the console command is the way
};

// [rc4l] Shows the picker over `owner` (a native window handle, or NULL) and puts the chosen folder in `out`.
PickResult PickFolderNative(void *owner, const char *title, std::string &out);

// [rc4l] The game window's native handle, for the picker to sit over; NULL where there is none to give.
void *MainWindowHandle();

}} // namespace zx::wadfolders

#endif // ZX_FOLDERPICKER_H
