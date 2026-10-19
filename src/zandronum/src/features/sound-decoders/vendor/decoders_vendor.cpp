// [rc4l] The vendored decoders' implementations, compiled once into the engine and the tests; their warnings are switched off in CMake, since this code is not ours.
#define DR_WAV_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#include "decoders.h"

// [rc4l] decoders.h took stb_vorbis's declarations only; its include guard skips them here and compiles the rest.
#include "stb_vorbis.c"
