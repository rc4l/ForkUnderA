// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] ZADF_FORCE_VIDEO_DEFAULTS as the render settings read it, the rule GL's OVERRIDE_*_IF_NECESSARY macros apply in place.

#ifndef ZX_FORCEDVIDEO_COMPUTE_H
#define ZX_FORCEDVIDEO_COMPUTE_H

namespace zx { namespace rendersettings {

// [rc4l] An int video setting as read: its default while the server forces the defaults, else its own value.
int ComputeForcedVideoInt(bool forced, int value, int def);

// [rc4l] gl_fogmode as read: forced defaults never turn fog off, so 0 reads as 1 while the server forces them.
int ComputeForcedFogMode(bool forced, int mode);

}} // namespace zx::rendersettings

#endif // ZX_FORCEDVIDEO_COMPUTE_H
