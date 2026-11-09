// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Customize Controls with a search row: typing narrows the key list by label or command.
#ifndef ZX_CONTROLSMENU_H
#define ZX_CONTROLSMENU_H

struct FOptionMenuDescriptor;

namespace zx
{

// [Doruk-Aksoy] Gives the controls menu the searchable class unless its MENUDEF already chose one of its own.
void ControlsSearch_Install( FOptionMenuDescriptor *desc );

} // namespace zx

#endif // ZX_CONTROLSMENU_H
