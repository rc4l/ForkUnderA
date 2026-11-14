// [Doruk-Aksoy] Spot lights as UZDoom draws them: a map spot's arguments, the direction its cone points, and how much of its light a point in the cone takes.
#ifndef ZX_SPOTLIGHT_COMPUTE_H
#define ZX_SPOTLIGHT_COMPUTE_H

namespace zx { namespace dynlights {

// [Doruk-Aksoy] A map spot light's colour and cone, which its thing gives as arg0 0xRRGGBB, arg1 inner and arg2 outer angle in degrees.
struct SpotMapArgs
{
	int red, green, blue;
	float inner, outer;
};

SpotMapArgs ComputeSpotMapArgs(int arg0, int arg1, int arg2);

// [Doruk-Aksoy] The direction the light's cone is compared against, in world x, y, z (z up): its facing negated, as UZDoom's AddLightToList packs it, pitch positive down.
void ComputeSpotDirection(double yawDegrees, double pitchDegrees, float out[3]);

// [Doruk-Aksoy] The share of a spot's light a point takes: smoothstep from the outer to the inner cone's cosine of the angle between toLight (the point to the light) and the direction, a step where the cone has no soft edge.
float ComputeSpotFactor(float toLightX, float toLightY, float toLightZ, const float direction[3], float cosInner, float cosOuter);

} }

#endif
