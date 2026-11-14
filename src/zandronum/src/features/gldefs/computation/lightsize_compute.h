// [Doruk-Aksoy] The sizes a GLDEFS light block may give, as UZDoom's parser clamps them.
#ifndef ZX_LIGHTSIZE_COMPUTE_H
#define ZX_LIGHTSIZE_COMPUTE_H

namespace zx { namespace gldefs {

// [Doruk-Aksoy] A size or secondarySize as written, clamped to UZDoom's 1 to 1024.
int ComputeLightSize(int written);

// [Doruk-Aksoy] A sector light's scale as written (1.0 being 255), clamped to UZDoom's 1 to 1024.
int ComputeSectorLightScale(float written);

} }

#endif
