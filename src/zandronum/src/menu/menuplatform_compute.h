// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] MENUDEF's IfOption platform keywords, so a menu can offer a desktop's window settings and leave a browser's page to size its canvas.

#ifndef ZX_MENUPLATFORM_COMPUTE_H
#define ZX_MENUPLATFORM_COMPUTE_H

namespace zx {

enum class MenuPlatform { Windows, Mac, Unix, Web };

// [rc4l] Whether keyword (any case) is one of IfOption's platform names: Windows, Mac, unix or Web.
bool ComputeIsMenuPlatformKeyword(const char *keyword);

// [rc4l] Whether that keyword names platform; a browser is Web and not unix, though Emscripten defines __unix__.
bool ComputeMenuPlatformMatches(const char *keyword, MenuPlatform platform);

} // namespace zx

#endif // ZX_MENUPLATFORM_COMPUTE_H
