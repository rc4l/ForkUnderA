// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The application icon read from the core pk3, for window systems that take it at run time.

#ifndef ZX_APPICON_H
#define ZX_APPICON_H

#include <vector>

namespace zx { namespace appicon {

// [rc4l] fua_appicon.png at `size` by `size` as BGRA texels in out; false when the lump is missing or smaller than that.
bool LoadAppIcon(int size, std::vector<unsigned char> &out);

}} // namespace zx::appicon

#endif // ZX_APPICON_H
