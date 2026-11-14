// [Doruk-Aksoy] A GLDEFS light's attenuate setting and the lightsizefactor that goes with it, as UZDoom resolves them.
#ifndef ZX_ATTENUATE_COMPUTE_H
#define ZX_ATTENUATE_COMPUTE_H

namespace zx { namespace gldefs {

// [Doruk-Aksoy] A definition's attenuate tag: 0 or 1 as written, kAttenuateUnset where it has none.
constexpr int kAttenuateUnset = -1;

// [Doruk-Aksoy] Whether a light is attenuated: its own tag where it has one, else the level's MAPINFO attenuatelights.
bool ComputeLightAttenuated(int attenuate, bool levelAttenuates);

// [Doruk-Aksoy] A size scaled by the GLDEFS lightsizefactor in force, unless the definition says attenuate 0.
int ComputeLightSizeFactor(int size, int attenuate, double factor);

} }

#endif
