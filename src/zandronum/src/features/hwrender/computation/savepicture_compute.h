// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A savegame is a PNG whose own picture (IHDR, gAMA, PLTE, IDAT) comes first and the game's chunks after it; these swap that picture in a save already written, for a device that reads the picture back only after the save is made.

#ifndef ZX_SAVEPICTURE_COMPUTE_H
#define ZX_SAVEPICTURE_COMPUTE_H

#include <cstddef>
#include <string>

namespace zx {

// [rc4l] The length of a PNG's leading picture chunks from its start, signature included; 0 when bytes is no PNG, starts with no picture chunk, or has a chunk running past its end.
size_t ComputePngPictureLength(const unsigned char *bytes, size_t size);

// [rc4l] save with its picture replaced by picture's (each a PNG's bytes, picture's own chunks taken up to whatever follows them) into out; false, out untouched, when either is malformed.
bool ComputeSpliceSavePicture(const std::string &save, const std::string &picture, std::string &out);

} // namespace zx

#endif // ZX_SAVEPICTURE_COMPUTE_H
