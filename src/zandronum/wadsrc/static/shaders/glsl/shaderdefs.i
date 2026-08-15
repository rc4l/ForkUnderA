// This file contains common data definitions for both vertex and fragment shader

// these settings are actually pointless but there seem to be some old ATI drivers that fail to compile the shader without setting the precision here.
precision highp int;
precision highp float;

uniform vec4 uCameraPos;
uniform int uTextureMode;
uniform float uClipHeightTop, uClipHeightBottom;

uniform float uAlphaThreshold;


// colors
uniform vec4 uObjectColor;

// [rc4l] features/damage-tint: per-pixel tint. rgb = the floor's color, a = strength; a == 0 (the
// default) is a no-op for every draw that doesn't arm it. Range = (gradient top, gradient bottom,
// mode, unused) in texture-V space -- full tint at bottom, fading to none at top. mode 0 is a
// multiplicative stain (taking damage); mode 1 is a screen-blend glow (protected by a suit/invuln).
uniform vec4 uDamageTint;
uniform vec4 uDamageTintRange;
uniform vec4 uDynLightColor;
uniform vec4 uFogColor;
uniform float uDesaturationFactor;
uniform float uInterpolationFactor;

// Fixed colormap stuff
uniform int uFixedColormap;				// 0, when no fixed colormap, 1 for a light value, 2 for a color blend, 3 for a fog layer
uniform vec4 uFixedColormapStart;
uniform vec4 uFixedColormapRange;

// Glowing walls stuff
uniform vec4 uGlowTopPlane;
uniform vec4 uGlowTopColor;
uniform vec4 uGlowBottomPlane;
uniform vec4 uGlowBottomColor;

// Lighting + Fog
uniform vec4 uLightAttr;
#define uLightLevel uLightAttr.a
#define uFogDensity uLightAttr.b
#define uLightFactor uLightAttr.g
#define uLightDist uLightAttr.r
uniform int uFogEnabled;

// dynamic lights
uniform int uLightIndex;

// matrices
uniform mat4 ProjectionMatrix;
uniform mat4 ViewMatrix;
uniform mat4 ModelMatrix;
uniform mat4 TextureMatrix;

