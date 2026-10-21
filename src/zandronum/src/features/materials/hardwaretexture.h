// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A renderer's own copy of a texture's pixels (GL's FHardwareTexture), which the texture system owns and throws away when it flushes.

#ifndef ZX_HARDWARETEXTURE_H
#define ZX_HARDWARETEXTURE_H

class IHardwareTexture
{
public:
	virtual ~IHardwareTexture() {}
	// [rc4l] Drop the copies made under translations, or everything when `all`.
	virtual void Clean(bool all) = 0;
};

#endif // ZX_HARDWARETEXTURE_H
