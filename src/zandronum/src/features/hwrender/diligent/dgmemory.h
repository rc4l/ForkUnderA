// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's GPU memory by tag, for features/telemetry's gpu/mem counters and the report of what is still held at shutdown (dgmemory.cpp).

#ifndef ZX_DGMEMORY_H
#define ZX_DGMEMORY_H

#include "Texture.h"
#include "Buffer.h"

namespace zx { namespace hwrender {

// [rc4l] Counts a texture or buffer the backend made under the tag its name gives, until nothing holds it; NULL is ignored.
void TrackDeviceTexture(Diligent::ITexture *texture, const Diligent::TextureDesc &desc);
void TrackDeviceBuffer(Diligent::IBuffer *buffer, const Diligent::BufferDesc &desc);

// [rc4l] Prints every tag still holding GPU memory, saying when.
void ReportHeldDeviceObjects(const char *when);

}} // namespace zx::hwrender

#endif // ZX_DGMEMORY_H
