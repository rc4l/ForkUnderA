#!/usr/bin/env bash
# ff-parity-tripwire: shader/fixed-function parity check for the render state.
#
# Lesson (flight 6): upstream c47c7421a routed the decal shade color through a
# shader-only uniform (objectcolor); on our shaderless macOS 2.1 path it was a
# silent no-op and every shaded decal rendered white. Upstream never noticed
# because it deleted GL 2.x wholesale at the core flip -- we keep fixed
# function alive until our own core flip, so every render-state field the
# shader consumes must ALSO be consumed by the fixed-function branch of
# Apply() (or by an explicitly marked [rc4l] shim / allowlist entry below).
#
# Mechanics: extract the m<Field> members read inside ApplyShader(), extract
# those read in the remainder of gl_renderstate.cpp after ApplyShader (the
# fixed-function Apply body plus helpers), and fail on shader-only fields.
#
# This tripwire retires together with the fixed-function path at the core
# profile flip (fc0cf4f99).
set -u
cd "$(dirname "$0")/.."

RS=src/zandronum/src/gl/renderer/gl_renderstate.cpp
[ -f "$RS" ] || { echo "ff-parity-tripwire: $RS not found"; exit 1; }

# Fields with a deliberate fixed-function story that lives OUTSIDE
# gl_renderstate.cpp (or that has no FF equivalent by design). Every entry
# must carry a one-line justification.
ALLOWLIST="
mGlowTop        # FF glow: handled per-vertex in gl_walls_draw / gl_flats FF paths
mGlowBottom     # FF glow: see mGlowTop
mGlowTopPlane   # FF glow: see mGlowTop
mGlowBottomPlane # FF glow: see mGlowTop
mLightData      # dynamic light uniform arrays: FF uses multitexture/multipass gl_dynlight path
mNumLights      # see mLightData
mDynColor       # FF dyn sprite light: applied through gl_SetColor's color math, not Apply
mCameraPos      # shader fog eye position: FF fog is GL_FOG, distance model differs by design
mSoftLight      # lightmode 8 (software-light shader emulation) is a GLSL-only mode
mDesaturation   # FF desaturation: no FF equivalent; CM_DESAT colormaps retired upstream 887d35d55
mLightParms     # shader fog attenuation (lightfactor/lightdist); FF fog is GL_FOG via mFogDensity, which the FF branch consumes
"

# Extract the ApplyShader() body: from its definition to the next function at
# column 0 ("bool FRenderState::" / "void FRenderState::").
shader_body=$(awk '/FRenderState::ApplyShader/{f=1} f{print} f && /^}/{exit}' "$RS")
# Everything else in the file (crude but effective: full file minus the shader body's line span).
ff_body=$(awk '/FRenderState::ApplyShader/{f=1} !f{print} f && /^}/{f=2; next} f==2{print}' "$RS")

fail=0
for field in $(echo "$shader_body" | grep -oE 'm[A-Z][A-Za-z]+' | sort -u); do
	# Only consider actual member fields of the class (declared in the header).
	grep -qE "(^|[^A-Za-z])$field([^A-Za-z]|$)" src/zandronum/src/gl/renderer/gl_renderstate.h || continue
	# Methods and non-state identifiers are filtered by requiring the header
	# declaration above; now check FF-side consumption.
	if echo "$ff_body" | grep -qE "(^|[^A-Za-z])$field([^A-Za-z]|$)"; then
		continue
	fi
	if echo "$ALLOWLIST" | grep -qE "^$field[[:space:]#]"; then
		continue
	fi
	echo "ff-parity-tripwire: '$field' is consumed by ApplyShader() but not by the fixed-function path."
	echo "  Add a fixed-function fallback in Apply() (see the mObjectColor [rc4l] shim) or an"
	echo "  allowlist entry with justification in tools/ff-parity-tripwire.sh."
	fail=1
done

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "ff-parity-tripwire: clean."
