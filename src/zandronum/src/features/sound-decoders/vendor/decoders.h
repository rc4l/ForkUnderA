#ifndef ZX_SOUND_DECODERS_VENDOR_H
#define ZX_SOUND_DECODERS_VENDOR_H

// [rc4l] The vendored decoders' declarations, built with the options their implementation (decoders_vendor.cpp) uses: memory only, no stdio.
#define DR_WAV_NO_STDIO
#define DR_FLAC_NO_STDIO
#define DR_MP3_NO_STDIO
#define STB_VORBIS_NO_STDIO

#include "dr_wav.h"
#include "dr_flac.h"
#include "dr_mp3.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#endif
