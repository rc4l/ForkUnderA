// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A browser has no folders to pick, so the page's own file panel is the way in.

#include "features/wad-folders/zx_folderpicker.h"

#include <cstddef>

namespace zx { namespace wadfolders {

PickResult PickFolderNative(void *, const char *, std::string &)
{
	return PickResult::Unavailable;
}

void *MainWindowHandle()
{
	return NULL;
}

}} // namespace zx::wadfolders
