// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What ACS's CheckAutomap reports of the automap.

#ifndef ZX_ACSAUTOMAP_COMPUTE_H
#define ZX_ACSAUTOMAP_COMPUTE_H

namespace zx {

// [rc4l] CheckAutomap's answers, as ACS sees them.
enum { AUTOMAP_OFF, AUTOMAP_FULLMAP, AUTOMAP_OVERLAYMAP };

// [rc4l] Adapted from zandronum@6869793234d6828e47a2aa3b57a9ea7a87736e0b: an active automap is an overlay while the view still draws, else the full map.
int ComputeAutomapStatus(bool automapActive, bool viewActive);

} // namespace zx

#endif
