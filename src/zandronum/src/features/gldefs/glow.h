
// [rc4l] GLDEFS' glowing flats, moved from gl/dynlights/gl_glow.h.

#ifndef __GL_GLOW
#define __GL_GLOW

struct sector_t;
class FScanner;

void gl_InitGlow(FScanner &sc);
int gl_CheckSpriteGlow(sector_t *sec, int lightlevel, fixed_t x, fixed_t y, fixed_t z);

#endif
