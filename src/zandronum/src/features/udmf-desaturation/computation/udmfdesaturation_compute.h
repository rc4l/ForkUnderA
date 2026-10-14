// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A UDMF sector's desaturation key as the colormap's 0..255 amount, held to the range GL's shader can mix by.

#ifndef ZX_UDMFDESATURATION_COMPUTE_H
#define ZX_UDMFDESATURATION_COMPUTE_H

namespace zx { namespace udmf {

// [rc4l] desaturation is a fraction, 0 for none and 1 for grey; anything past either end is held there, as a percentage written for it (WDI09's 99) turned a grey corner bright blue.
int ComputeUdmfDesaturation(double value);

}} // namespace zx::udmf

#endif
