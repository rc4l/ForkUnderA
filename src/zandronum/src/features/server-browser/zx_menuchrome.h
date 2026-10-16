// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the server browser's menu asks of the bar above it (features/global-header, which installs it since the bar sits above the menu); before that, no bar.

#ifndef ZX_MENUCHROME_H
#define ZX_MENUCHROME_H

namespace zx
{

struct MenuChrome
{
	// [rc4l] Where the bar's bottom is on the screen, in screen pixels.
	int ( *screenBottom )( void );
	// [rc4l] Whether the bar has the keyboard.
	bool ( *hasFocus )( void );
};

void SetMenuChrome( const MenuChrome *chrome );

} // namespace zx

#endif // ZX_MENUCHROME_H
