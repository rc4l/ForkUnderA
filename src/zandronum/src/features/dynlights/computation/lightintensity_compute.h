// [Doruk-Aksoy] How bright a dynamic light is drawn, before its colour: its GLDEFS intensity, and its carrier's alpha when the carrier asks for it.
#ifndef ZX_LIGHTINTENSITY_COMPUTE_H
#define ZX_LIGHTINTENSITY_COMPUTE_H

namespace zx { namespace dynlights {

// [Doruk-Aksoy] gldefsIntensity times carrierAlpha (0 to 1) when the carrier has +LIGHTMULTALPHA, as UZDoom's AddLightToList and GetDynSpriteLight scale it.
float ComputeLightIntensity(float gldefsIntensity, bool multAlpha, float carrierAlpha);

} }

#endif
