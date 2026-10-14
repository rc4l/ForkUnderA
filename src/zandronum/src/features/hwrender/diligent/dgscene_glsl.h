// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The world pass's GLSL: the scene vertex shader and its fragment stages, included once by dgscene.cpp inside zx::hwrender where its pipelines are built.

#ifndef ZX_DGSCENE_GLSL_H
#define ZX_DGSCENE_GLSL_H

// [rc4l] kWalkSlots and kMainWalkSlot spelled for the scene shader, which reads one bit per walk for every subsector and sidedef.
#define FUA_WALK_SLOTS_STR "10"
#define FUA_MAIN_WALK_SLOT_STR "8"
static_assert(zx::levelmesh::kWalkSlots == 10, "FUA_WALK_SLOTS_STR spells kWalkSlots");
static_assert(zx::levelmesh::kMainWalkSlot == 8, "FUA_MAIN_WALK_SLOT_STR spells kMainWalkSlot");
static const char *kSceneVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in float aPiece;\n"
	FUA_CONSTANTS_GLSL
	// [rc4l] Everything constant across a piece, read once here instead of ridden in on every vertex.
	FUA_PIECEDATA_GLSL
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec3 vColor;\n"
	"layout(location = 2) out vec3 vLightParm;\n"
	"layout(location = 3) out vec4 vFog;\n"
	"layout(location = 4) out vec4 vPixelPos;\n"
	"layout(location = 5) flat out int vLightIndex;\n"
	"layout(location = 6) out vec3 vNormal;\n"
	"layout(location = 7) flat out vec4 vPlane;\n"
	"layout(location = 8) flat out vec2 vWarp;\n"
	"layout(location = 9) flat out vec4 vFloodU;\n"
	"layout(location = 10) flat out vec4 vFloodV;\n"
	"layout(location = 11) flat out vec4 vActorLight;\n"
	"layout(location = 12) flat out int vBrightSlot;\n"
	"layout(location = 13) flat out int vLinkId;\n"
	"layout(location = 14) flat out float vPlain;\n"
	"layout(location = 15) flat out float vDesaturation;\n"
	"layout(location = 16) flat out float vHoleCut;\n"
	"layout(location = 17) flat out float vTexFlags;\n"
	"layout(location = 18) out vec2 vGlowDist;\n"
	"layout(location = 19) flat out vec4 vGlowTop;\n"
	"layout(location = 20) flat out vec4 vGlowBottom;\n"
	"layout(location = 21) flat out vec4 vTint;\n"
	"layout(location = 22) flat out vec3 vTintRange;\n"
	"layout(std430, binding = 10) readonly buffer StackVis { uint stackVis[]; };\n"
	"layout(std430, binding = 11) readonly buffer SlotRemap { uint slotRemap[]; };\n"
	// [rc4l] ComputeStackVisible, line for line: whether walk slot's walk drew id this frame.
	"bool fuaWalked(int id, int slot) {\n"
	"    int bit = id * " FUA_WALK_SLOTS_STR " + slot;\n"
	"    return (stackVis[bit >> 5] & (1u << uint(bit & 31))) != 0u;\n"
	"}\n"
	"void main() {\n"
	"    PieceData pd = pieces[int(aPiece + 0.5)];\n"
	"    vWarp = vec2(pd.fogColor.w, pd.normal.w);\n"
	"    vFloodU = pd.floodU;\n"
	"    vFloodV = pd.floodV;\n"
	"    vActorLight = pd.actorLight;\n"
	"    vBrightSlot = int(pd.bright.x + 0.5);\n"
	// [rc4l] bright.z marks a baked piece on an animated texture, whose slots SlotRemap turns into those of the frame showing.
	"    if (pd.bright.z > 0.5) vBrightSlot = int(slotRemap[vBrightSlot]);\n"
	// [rc4l] The piece's subsector or sidedef as GL's light links name it (bright.y is PieceStackId plus one), or -1 for a sprite or model.
	"    vLinkId = int(pd.bright.y + 0.5) - 1;\n"
	"    vPlain = pd.bright.w;\n"
	"    vDesaturation = pd.extra.x;\n"
	"    vTexFlags = pd.extra.w;\n"
	// [rc4l] main.vp's glow distances, below the ceiling and above the floor the glow is measured from, the mesh's (x, height, y) being GL's world coordinate.
	"    vTint = pd.tint;\n"
	"    vTintRange = pd.tintRange.xyz;\n"
	"    vGlowTop = pd.glowTop;\n"
	"    vGlowBottom = pd.glowBottom;\n"
	"    vGlowDist = vec2(-((pd.glowTopPlane.w + pd.glowTopPlane.x * aPos.x + pd.glowTopPlane.y * aPos.z) * pd.glowTopPlane.z) - aPos.y,\n"
	"                     aPos.y + ((pd.glowBottomPlane.w + pd.glowBottomPlane.x * aPos.x + pd.glowBottomPlane.y * aPos.z) * pd.glowBottomPlane.z));\n"
	"    vec4 clip = uMVP * vec4(aPos, 1.0);\n"
	// [rc4l] A stacked view draws only what GL's walk through it drew, subsector for a flat and sidedef for a wall (ComputeMarkStackVisible); any other piece's triangle goes off screen whole.
	"    int stackSlot = int(uFrame.y + 0.5) - 1;\n"
	"    int stackId = int(pd.bright.y + 0.5) - 1;\n"
	// [rc4l] With the player's sky holes in uFrame.z, a piece the player's walk did not draw is one a hole may drop (fuaBehindSkyHole); a sprite or model GL drew is never one.
	"    vHoleCut = (uFrame.z > 0.5 && stackId >= 0 && !fuaWalked(stackId, " FUA_MAIN_WALK_SLOT_STR ")) ? 1.0 : 0.0;\n"
	"    if (stackSlot >= 0) {\n"
	"        bool unseen = stackId >= 0 && !fuaWalked(stackId, stackSlot);\n"
	// [rc4l] GL draws a surface only from its front, a floor from above and a ceiling from below, and a stacked view's eye sits under every rooftop it looks up at.
	"        if (unseen || dot(pd.normal.xyz, uCameraPos.xyz - aPos) < -0.01) clip = vec4(2.0, 2.0, 2.0, 1.0);\n"
	"    }\n"
	// [rc4l] A HUD model's surface (texFlags 4) at the near end of the depth range, kept in order among its own, so the world never covers it.
	"    if ((int(pd.extra.w + 0.5) & 4) != 0) clip.z *= 0.0001;\n"
	"    gl_Position = clip;\n"
	// [rc4l] extra.yz is the piece's scroll since the bake, so a scroller moves the texture without new vertices.
	"    vUV = aUV + pd.extra.yz;\n"
	"    vColor = pd.colorLight.rgb;\n"
	// softLight, fogDensity, alpha -- the three the fragment shader reads as vLightParm.
	"    vLightParm = vec3(pd.colorLight.w, pd.fogAlphaMat.x, pd.fogAlphaMat.y);\n"
	"    vFog = vec4(pd.fogColor.rgb, pd.fogAlphaMat.z);\n"
	// [rc4l] main.vp keeps pixelpos as (world xyz, view depth). The view depth is what the plane
	// fog mode measures along, and clip.w IS that depth for a standard perspective matrix -- the
	// projection's -1 in the w row copies eye-space -z straight through.
	"    vPixelPos = vec4(aPos, clip.w);\n"
	"    vLightIndex = int(pd.fogAlphaMat.w);\n"
	"    if (pd.bright.z > 0.5) vLightIndex = int(slotRemap[vLightIndex]);\n"
	"    vNormal = pd.normal.xyz;\n"
	// [rc4l] The surface PLANE, flat: its normal and its distance from the origin.
	//
	// Whether a light reaches a surface is a property of the SURFACE, not of the fragment -- one
	// answer for the whole face. Asking it per fragment means asking it of an interpolated
	// position, and the rasteriser returns a world height on a flat floor that wobbles in the last
	// thousandth of a unit. That is harmless until a light sits exactly ON the plane it lights, and
	// then the wobble is the entire answer: the test flips from fragment to fragment and the floor
	// comes out in hard horizontal stripes of lit and unlit. Freedoom MAP01 has such a light at the
	// map start -- a green one at z 8.0 on a floor at 8.0 -- which is how this was found.
	//
	// Interpolating nothing removes the wobble rather than hiding it under a tolerance: every
	// vertex of a planar face gives the same dot(n, p), so the flat value is that number exactly and
	// the test is identical across the face however far away or however oblique it is.
	"    vPlane = vec4(pd.normal.xyz, dot(pd.normal.xyz, aPos));\n"
	"}\n";

// [rc4l] The engine's real lighting, ported.
//
// This is a transcription of wadsrc/static/shaders/glsl/main.fp -- R_DoomLightingEquation and the
// getLightColor/applyFog pair -- not an approximation of it. Doom's light is not a multiply: it is a
// colormap *index* that darkens with distance, and the band structure that produces is most of what
// the game looks like. A flat multiply renders a world that is legible but visibly not Doom.
//
// The dist argument is gl_FragCoord.z exactly as the engine passes it, which is why the projection
// matrix had to be moved to Vulkan's [0,1] depth range first -- feeding a [-1,1] z into a 1/dist
// falloff would have produced light bands in the wrong places, and worse, plausible-looking ones.
//
// [rc4l] Ported here: software-mode light diminishing, fog-mode diminishing, coloured fog and both fog
// distance modes, with dynamic lights, brightmaps, desaturation, glowing walls and the fixed colormaps
// as separate uniforms and buffers elsewhere here.
//
// uCameraPos.w carries fua_dg_lightmode -- riding in the unused w of a vector already in the buffer
// makes the debug views free (no resize, no extra binding) so they can stay in the shipped shader.
// Two silent-failure bugs this port survived were caught only by looking at a picture; being able to
// ask the shader what it thinks depth and colour are is worth the three lines.
// [rc4l] FUA_MAT_SLOTS, the material array's size, is defined by CompileShader from the device's limits (MaterialSlotCapacity).

#define FUA_LIGHT_GLSL \
	"#extension GL_EXT_nonuniform_qualifier : require\n" \
	"layout(location = 0) in vec2 vUV;\n" \
	"layout(location = 1) in vec3 vColor;\n" \
	"layout(location = 2) in vec3 vLightParm;\n" \
	"layout(location = 3) in vec4 vFog;\n" \
	"layout(location = 4) in vec4 vPixelPos;\n" \
	"layout(location = 5) flat in int vLightIndex;\n" \
	"layout(location = 6) in vec3 vNormal;\n" \
	"layout(location = 7) flat in vec4 vPlane;\n" \
	"layout(location = 8) flat in vec2 vWarp;\n" \
	"layout(location = 9) flat in vec4 vFloodU;\n" \
	"layout(location = 10) flat in vec4 vFloodV;\n" \
	"layout(location = 11) flat in vec4 vActorLight;\n" \
	"layout(location = 12) flat in int vBrightSlot;\n" \
	"layout(location = 13) flat in int vLinkId;\n" \
	"layout(location = 14) flat in float vPlain;\n" \
	"layout(location = 15) flat in float vDesaturation;\n" \
	"layout(location = 16) flat in float vHoleCut;\n" \
	"layout(location = 17) flat in float vTexFlags;\n" \
	"layout(location = 18) in vec2 vGlowDist;\n" \
	"layout(location = 19) flat in vec4 vGlowTop;\n" \
	"layout(location = 20) flat in vec4 vGlowBottom;\n" \
	"layout(location = 21) flat in vec4 vTint;\n" \
	"layout(location = 22) flat in vec3 vTintRange;\n" \
	FUA_DAMAGE_TINT_GLSL \
	/* [rc4l] GL's texture modes, as getTexel applies them: TM_MASK (texFlags 1) keeps only a stencil's shape, white, for its fill colour, and TM_INVERSE (16) inverts the colour, green and blue swapped as main.fp swaps them; then the damage tint getTexel mixes in. */ \
	"vec4 fuaTexMode(vec4 t) {\n" \
	"    int f = int(vTexFlags + 0.5);\n" \
	"    if ((f & 1) != 0) t = vec4(vec3(1.0), t.a);\n" \
	"    else if ((f & 16) != 0) t = vec4(1.0 - t.r, 1.0 - t.b, 1.0 - t.g, t.a);\n" \
	"    return fuaDamageTint(t, vUV.t, vTint, vTintRange);\n" \
	"}\n" \
	/* [rc4l] ComputeDesaturate, line for line: main.fp's desaturate(), on the texel and the brightmap but never the light. */ \
	"vec3 fuaDesaturate(vec3 c) {\n" \
	"    if (vDesaturation <= 0.0) return c;\n" \
	"    float gray = c.r * 0.3 + c.g * 0.56 + c.b * 0.14;\n" \
	"    return mix(c, vec3(gray), vDesaturation);\n" \
	"}\n" \
	FUA_CONSTANTS_GLSL \
	FUA_FIXED_COLORMAP_GLSL \
	FUA_FUZZ_GLSL \
	"layout(binding = 1) uniform sampler2D uTex;\n" \
	/* [rc4l] The brightmap layer, present on EVERY material -- black where there is none.
	   The brightmap is added to the lit colour, so black is the identity, and that removes the
	   alternative: a per-batch flag, a branch here, and a second pipeline permutation. */ \
	"layout(binding = 5) uniform sampler2D uBrightmap;\n" \
	/* [rc4l] Every material in the level, reachable from one descriptor set.

	   uSkyColor.w carries the live slot count, so zero means "use the bound texture" and the whole
	   thing switches from one constant, with no second pipeline and no shader permutation. The index
	   is vLightIndex -- the vertex field that used to hold a dynamic light index and has been dead
	   since this shader started testing every light -- so bindless costs no vertex bytes.

	   nonuniformEXT is not decoration: neighbouring fragments in one quad can sit on different
	   surfaces with different slots, and without it the index is assumed uniform across the wave
	   and the whole quad samples whichever one won. */ \
	"layout(binding = 8) uniform sampler2D uMaterials[FUA_MAT_SLOTS];\n" \
	/* [rc4l] GL's func_warp1.fp and func_warp2.fp, line for line -- see warp_compute for the tested copy. */ \
	"vec2 fuaWarp(vec2 uv) {\n" \
	"    if (vWarp.x < 0.5 || vWarp.x > 2.5) return uv;\n" \
	"    const float twoPi = 6.28318530717958647692;\n" \
	"    float t = uFrame.x * vWarp.y;\n" \
	"    vec2 o;\n" \
	"    if (vWarp.x < 1.5) {\n" \
	"        o.y = sin(twoPi * (uv.x + t * 0.125)) * 0.1;\n" \
	"        o.x = sin(twoPi * (uv.y + t * 0.125)) * 0.1;\n" \
	"    } else {\n" \
	"        float siny = sin(twoPi * (uv.y * 2.2 + t * 0.75)) * 0.03;\n" \
	"        o.y = siny + sin(twoPi * (uv.x * 0.75 + t * 0.75)) * 0.03;\n" \
	"        o.x = siny + sin(twoPi * (uv.x * 1.1 + t * 0.45)) * 0.02;\n" \
	"    }\n" \
	"    return uv + o;\n" \
	"}\n" \
	/* [rc4l] GL's flooded gap: where the eye's ray through this fragment meets the plane at vFloodU.w, w 1 when it does; flood_compute is the tested twin. */ \
	"vec4 fuaFloodPoint() {\n" \
	"    if (vFloodV.w < 0.5) return vec4(0.0);\n" \
	"    float rise = vPixelPos.y - uCameraPos.y;\n" \
	"    if (rise == 0.0) return vec4(0.0);\n" \
	"    float t = (vFloodU.w - uCameraPos.y) / rise;\n" \
	"    if (!(t > 0.0 && t <= 1.0)) return vec4(0.0);\n" \
	"    return vec4(uCameraPos.xyz + t * (vPixelPos.xyz - uCameraPos.xyz), 1.0);\n" \
	"}\n" \
	/* [rc4l] The plane's own texture coordinate there (mesh x and z are map x and y), or the wall's own when the eye is on the plane's far side. */ \
	"vec2 fuaFloodUV(vec2 uvIn) {\n" \
	"    vec4 q = fuaFloodPoint();\n" \
	"    if (q.w < 0.5) return uvIn;\n" \
	"    return vec2(dot(vFloodU.xy, q.xz) + vFloodU.z, dot(vFloodV.xy, q.xz) + vFloodV.z);\n" \
	"}\n" \
	"vec4 fuaTexel(vec2 uvIn) {\n" \
	"    vec2 uv = fuaWarp(fuaFloodUV(uvIn));\n" \
	"    if (uSkyColor.w > 0.5 && vLightIndex > 0)\n" \
	"        return texture(uMaterials[nonuniformEXT(vLightIndex)], uv);\n" \
	"    return texture(uTex, uv);\n" \
	"}\n" \
	"layout(std430, binding = 2) readonly buffer LightBuffer { vec4 lights[]; };\n" \
	/* [rc4l] ComputeBehindSkyHole, line for line: whether the eye's line to this fragment passes through one of the player's sky holes (two vec4s each, ComputeSkyHoleRecord) first. */ \
	"layout(std430, binding = 14) readonly buffer SkyHoles { vec4 skyHoles[]; };\n" \
	"bool fuaBehindSkyHole() {\n" \
	"    if (vHoleCut < 0.5) return false;\n" \
	"    vec2 d = vPixelPos.xz - uCameraPos.xz;\n" \
	"    int n = int(uFrame.z + 0.5);\n" \
	"    for (int i = 0; i < n; i++) {\n" \
	"        vec4 seg = skyHoles[2 * i], zs = skyHoles[2 * i + 1];\n" \
	"        vec2 e = seg.zw - seg.xy;\n" \
	"        float den = d.x * e.y - d.y * e.x;\n" \
	"        if (abs(den) < 1e-6) continue;\n" \
	"        vec2 w = seg.xy - uCameraPos.xz;\n" \
	"        float t = (w.x * e.y - w.y * e.x) / den;\n" \
	"        float s = (w.x * d.y - w.y * d.x) / den;\n" \
	"        if (s < 0.0 || s > 1.0 || t <= 0.0 || t >= 0.9999) continue;\n" \
	"        float h = uCameraPos.y + t * (vPixelPos.y - uCameraPos.y);\n" \
	"        if (h >= zs.x + s * (zs.z - zs.x) && h <= zs.y + s * (zs.w - zs.y)) return true;\n" \
	"    }\n" \
	"    return false;\n" \
	"}\n" \
	/* [rc4l] The cluster grid: (offset, count) per cell, and the runs those point into. */ \
	"layout(std430, binding = 6) readonly buffer ClusterTable { uvec2 clusterCells[]; };\n" \
	"layout(std430, binding = 7) readonly buffer ClusterLightIndices { uint clusterLights[]; };\n" \
	/* [rc4l] ComputeLightTouches, line for line: a light lights only the ids GL linked it to, and a count of all ones lights everything. */ \
	"layout(std430, binding = 12) readonly buffer LightTouchRanges { uvec2 lightTouch[]; };\n" \
	"layout(std430, binding = 13) readonly buffer LightTouchIds { uint touchIds[]; };\n" \
	"bool fuaLightTouches(int li, uint id) {\n" \
	"    uvec2 r = lightTouch[li];\n" \
	"    if (r.y == 0xFFFFFFFFu) return true;\n" \
	"    uint lo = r.x, hi = r.x + r.y;\n" \
	"    while (lo < hi) {\n" \
	"        uint mid = (lo + hi) >> 1;\n" \
	"        uint v = touchIds[mid];\n" \
	"        if (v == id) return true;\n" \
	"        if (v < id) lo = mid + 1u; else hi = mid;\n" \
	"    }\n" \
	"    return false;\n" \
	"}\n" \
	"layout(location = 0) out vec4 outColor;\n" \
	/* [rc4l] Every light, tested per fragment -- no per-surface light list at all.

	   The engine builds a list of lights touching each surface and hands the shader an index into it.
	   That index is rebuilt EVERY FRAME, so it cannot live in a static mesh: baking it produced a
	   world where the dynamic lights simply never appeared, because every piece had recorded -1 on
	   the frame it was baked.

	   Rather than re-upload geometry per frame to fix the index, this drops the whole mechanism: the
	   buffer holds all active lights and the fragment shader tests each one. That machinery exists to
	   save GPU work, and the scale probe measured this GPU at 0.018 ms/frame for the entire visible
	   world -- roughly 0.1% utilisation -- so there is nothing to save. It is also the shape a
	   clustered forward renderer wants next.

	   Layout: pairs of (x, z, y, radius) and (r, g, b, mode), mode 1 meaning subtractive. Positions
	   use the same axis swap as the mesh vertices, so they compare directly against vPixelPos.xyz. */ \
	"vec3 fuaDynLight(vec3 base) {\n" \
	"    int n = int(uLightParams.x);\n" \
	"    if (n <= 0) return base;\n" \
	/* [rc4l] A zero normal means the CPU already did this piece's dynamic lighting -- take none here.

	   Sprites carry no normal, because a billboard turns to face the camera and has no side for a
	   light to be in front of or behind. That much was already true; what it also means is that a
	   sprite's dynamic light does not come from this loop at all. gl_SetDynSpriteLight computes it
	   on the CPU and the sprite derivation carries the result with the vertex colour, so adding the lights
	   again here counts them twice AND ignores everything that function knows.

	   DONTLIGHTSELF is what makes that visible. An item does not light itself: gl_SetDynSpriteLight
	   skips a light whose target is the actor being lit, so Freedoom MAP01's armor bonus is supposed
	   to sit DARK in the green pool its own glow throws on the floor. GL draws it that way. This loop
	   knows nothing of owners, lit it with its own light, and the sprite came out green: 48.7 against
	   GL's 15.4 in the green channel, with the floor beneath it agreeing in both.

	   So the marker now carries its full meaning -- no side, and no light from here. */ \
	"    if (dot(vPlane.xyz, vPlane.xyz) <= 0.0001) return base;\n" \
	"    vec3 dyn = vec3(0.0);\n" \
	/* [rc4l] The cell this fragment is in, and the short list of lights that can reach it.

	   uLightParams.zw carry the cluster depth range, and a zero in z means the grid is off -- in
	   which case this loops over every light exactly as it always has. The two paths must draw the
	   SAME picture, so fua_dg_clusters flips between them in a frozen frame and a diff says whether
	   they do.

	   Depth is 1.0 / gl_FragCoord.w: the clip w the hardware interpolated, which is the quantity
	   ComputeLightClustersFromMVP slices the light by on the CPU. Two sides agreeing because they
	   read the same number is worth more than two sides computing numbers that ought to match.

	   The 64 and 24 are kClusterTilePixels and kClusterSlices; a static_assert keeps them honest. */ \
	"    int first = 0;\n" \
	"    int count = n;\n" \
	"    bool clustered = uLightParams.z > 0.0 && uLightParams.w > uLightParams.z;\n" \
	"    if (clustered) {\n" \
	"        int tilesX = int((uScreen.x + 63.0) / 64.0);\n" \
	"        int tilesY = int((uScreen.y + 63.0) / 64.0);\n" \
	"        int tx = clamp(int(gl_FragCoord.x) / 64, 0, tilesX - 1);\n" \
	"        int ty = clamp(int(gl_FragCoord.y) / 64, 0, tilesY - 1);\n" \
	"        float depth = 1.0 / gl_FragCoord.w;\n" \
	"        int slice = 0;\n" \
	"        if (depth > uLightParams.z)\n" \
	"            slice = clamp(int(floor(log(depth / uLightParams.z) /\n" \
	"                log(uLightParams.w / uLightParams.z) * 24.0)), 0, 23);\n" \
	"        uvec2 cell = clusterCells[(slice * tilesY + ty) * tilesX + tx];\n" \
	"        first = int(cell.x);\n" \
	"        count = int(cell.y);\n" \
	/* [rc4l] CLAMP the loop bound. It comes out of a buffer, and a buffer can be stale.\
	   Every light in the level is at most n, so anything larger is nonsense -- and nonsense is what\
	   the cluster grid holds around a level change, before the CPU rebuilds it for the new map. A\
	   garbage count runs this loop for millions of iterations, and a GPU loop that long IS a hang:\
	   Windows times it out, resets the device, and the driver terminates the process -- no\
	   exception, no dump, nothing in the log. See docs/tdr-crash-after-map-loads.md. One\
	   instruction makes a stale grid a wrong pixel for one frame instead of a dead process. */ \
	"        count = clamp(count, 0, n);\n" \
	"        first = max(first, 0);\n" \
	"    }\n" \
	"    for (int i = 0; i < count; i++) {\n" \
	/* ...and the index it reads: lights[] has n entries and a stale grid can name anything. An\
	   out-of-range read is undefined, not merely wrong. */ \
	"        int li = clustered ? int(clusterLights[first + i]) : i;\n" \
	"        li = clamp(li, 0, max(n - 1, 0));\n" \
	/* [rc4l] GL's flood stops at solid walls, so a light whose links do not name this surface does not light it. */ \
	"        if (vLinkId >= 0 && !fuaLightTouches(li, uint(vLinkId))) continue;\n" \
	"        vec4 lp = lights[li*2];\n" \
	"        vec4 lc = lights[li*2+1];\n" \
	"        vec3 d = lp.xyz - vPixelPos.xyz;\n" \
	/* The side test gl_GetLight does per surface: a light behind the plane does not light it.
	   Without this the backs of walls and the room next door get lit, which reads as a scene far
	   more saturated than GL's. */ \
	/* [rc4l] ...and the same test read off the plane instead of the fragment.

	   dot(n, lightPos) - planeD is dot(n, lightPos - fragPos) for any fragment ON the plane, so this
	   is the same quantity, computed from numbers that do not vary across the face. A light lying
	   exactly in the plane gives exactly zero and is KEPT, which is what gl_flats.cpp does: it drops
	   a light only when the plane is strictly on the wrong side of it. */ \
	/* [rc4l] ...and a light sitting exactly ON the plane is KEPT, which needs slack to survive.

	   gl_flats.cpp drops a light only when the plane is STRICTLY above it, so a light exactly in the
	   plane stays -- and Doom puts lights exactly in planes constantly. A projectile that dies on a
	   floor comes to rest ON it, so its z equals the plane there to the last bit of fixed point:
	   measured on dbab04 as a plasma ball at -59.053 under a floor plane of -59.053. GL keeps that
	   light. This test computes the same quantity in floats, from a normalised normal and a plane
	   constant built out of vertex coordinates in the thousands, so the exact zero arrives as a few
	   thousandths either side of it -- and on the wrong side an entire surface is culled at once.

	   That is what the reported hard cut-off was. Every plasma bolt landing on that floor lit the
	   walkway above and left the floor it was resting on completely black, along a dead straight line
	   at the piece boundary, in Vulkan only. GL 30.6 against Vulkan 0.0 over the same rectangle of
	   the same frozen frame.

	   The slack is a tenth of a map unit: thousands of times the float error being covered, and far
	   below anything a player could see -- a light a tenth of a unit behind a surface lights it now.
	   It is not an attempt to place the boundary exactly, which floats cannot do; it is to keep the
	   case Doom actually produces on the same side of it as GL. */ \
	/* [rc4l] ...and only while r_lights_checkside asks for it (uOptions.x), as gl_GetLight and the flats' test do. */ \
	"        if (uOptions.x > 0.5 && dot(vPlane.xyz, lp.xyz) - vPlane.w < -0.1) continue;\n" \
	"        float a = max(lp.w - length(d), 0.0) / lp.w;\n" \
	"        if (a <= 0.0) continue;\n" \
	"        if (lc.a > 0.5) dyn -= lc.rgb * a;\n" \
	"        else            dyn += lc.rgb * a;\n" \
	"    }\n" \
	"    return clamp(base + dyn, 0.0, 1.4);\n" \
	"}\n" \
	FUA_SECTOR_LIGHT_GLSL \
	"vec3 fuaShadeEx(vec3 texel, bool wantDynLight) {\n" \
	"    vec3 undesaturated = texel;\n" \
	"    texel = fuaDesaturate(texel);\n" \
	"    float dbg = uCameraPos.w;\n" \
	"    if (dbg == 2.0) return vec3(gl_FragCoord.z);\n" \
	"    if (dbg == 3.0) return vec3(fract(gl_FragCoord.z * 64.0));\n" \
	"    if (dbg == 4.0) return vColor;\n" \
	"    if (dbg == 10.0) return texel;\n" \
	"    if (dbg == 11.0) return fuaDynLight(vec3(0.0));\n" \
	"    if (dbg == 12.0) return vec3(uLightParams.x / 16.0);\n" \
	"    if (dbg == 13.0) return vec3(lights[0].w / 256.0);\n" \
	/* [rc4l] 14: the surface NORMAL, 15: whether the interpolated world height holds still.

	   fuaDynLight decides whether a light reaches a surface with a SIGN TEST on the interpolated
	   world position, and a sign test is only as trustworthy as its inputs. On a floor the world
	   height is the same number at every fragment, so 15 has to come out flat black. Anything else
	   is the rasteriser handing back a height that wobbles -- and a light sitting AT that height
	   then switches on and off from fragment to fragment as the wobble crosses it. */ \
	"    if (dbg == 14.0) return vNormal * 0.5 + 0.5;\n" \
	"    if (dbg == 15.0) return vec3(fract(vPixelPos.y * 64.0));\n" \
	/* [rc4l] 16: the MATERIAL SLOT the fragment is using, as a grey ramp; 17: whether it is
	   taking the array at all. A surface that comes out white with bindless on looks the same
	   whether the slot is zero, the array is empty, or the switch is off, and these separate the
	   three without reading it off a guess. */ \
	"    if (dbg == 16.0) return vec3(float(vLightIndex) / 255.0);\n" \
	"    if (dbg == 17.0) return (uSkyColor.w > 0.5 && vLightIndex > 0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n" \
	"    if (dbg == 0.0) return texel * vColor;\n" \
	/* [rc4l] A flooded gap is lit and fogged where the plane it shows is, as DrawFloodedPlane's own polygon would be, and takes no dynamic light. */ \
	"    vec4 surfPos = vPixelPos;\n" \
	"    float surfDepth = gl_FragCoord.z;\n" \
	"    vec4 fq = fuaFloodPoint();\n" \
	"    if (fq.w > 0.5) { vec4 fc = uMVP * vec4(fq.xyz, 1.0); surfPos = vec4(fq.xyz, fc.w); surfDepth = fc.z / fc.w; wantDynLight = false; }\n" \
	"    float fogMode = vFog.a;\n" \
	"    float fogfactor = fuaFogFactor(fogMode, vLightParm.y, surfPos, uCameraPos.xyz);\n" \
	"    if (dbg == 5.0) return vec3(fogfactor);\n" \
	"    if (dbg == 6.0) return vFog.rgb;\n" \
	"    if (dbg == 7.0) return vec3(clamp(vPixelPos.w / 2000.0, 0.0, 1.0));\n" \
	"    if (dbg == 8.0) return vec3(clamp(-vLightParm.y * 20000.0, 0.0, 1.0));\n" \
	"    if (dbg == 9.0) return vec3(fogMode > 0.0 ? 1.0 : 0.0, fogMode < 0.0 ? 1.0 : 0.0, 0.0);\n" \
	/* [rc4l] Mirror clipping. Everything on the far side of the mirror plane is behind the glass and
	   must not appear in the reflection -- without this the wall the mirror hangs on renders first
	   from the reflected camera and fills the whole reflection. w == 0 means no mirror is active. */ \
	FUA_MIRROR_CLIP_GLSL("vPixelPos.xyz") \
	/* [rc4l] What GL's walk never drew behind a sky hole is GL's sky there, which the frame painted first. */ \
	"    if (fuaBehindSkyHole()) discard;\n" \
	/* [rc4l] A model GL culls (texFlags 2048 with its front faces clockwise, 4096 counter-clockwise) loses the faces GL loses; the test follows the view, so a mirror culls what GL's mirror culls. */ \
	"    int fuaCull = (int(vTexFlags + 0.5) >> 11) & 3;\n" \
	"    if (fuaCull != 0 && gl_FrontFacing != (fuaCull == 1)) discard;\n" \
	/* [rc4l] Under a fixed colormap main.fp takes no light, fog, desaturation or dynamic light, recolouring the texel in the piece's own colour: white for a level surface (texFlags 8), whose baked colour is its light, and a sprite's or decal's built under the colormap. */ \
	/* [rc4l] ...and a 3D fog layer's panel (texFlags 64) is not drawn at all, as GL's walls leave out an untextured one. */ \
	"    if (uFixedColormap.w > 0.5) {\n" \
	"        if ((int(vTexFlags + 0.5) & 64) != 0) discard;\n" \
	"        return fuaFixedColormap(undesaturated * ((int(vTexFlags + 0.5) & 8) != 0 ? vec3(1.0) : vColor));\n" \
	"    }\n" \
	"    vec3 color = fuaSectorLight(vColor, vLightParm.x, fogMode, fogfactor, surfDepth);\n" \
	/* [rc4l] main.fp's glowing walls (wallglow_compute): a glowing flat's colour added to the light, fading over its height from the plane. */ \
	"    if (vGlowTop.w > 0.0 && vGlowDist.x < vGlowTop.w) color += fuaDesaturate(vGlowTop.rgb * (1.0 - vGlowDist.x / vGlowTop.w));\n" \
	"    if (vGlowBottom.w > 0.0 && vGlowDist.y < vGlowBottom.w) color += fuaDesaturate(vGlowBottom.rgb * (1.0 - vGlowDist.y / vGlowBottom.w));\n" \
	"    color = min(color, vec3(1.0));\n" \
	/* Dynamic lights add to the light COLOUR before the texture is modulated, exactly as
	   getLightColor does -- adding after would light the black parts of a texture too. */ \
	/* [rc4l] Brightmaps add to the LIGHT, not to the finished fragment.
	   main.fp calls ProcessLight(color) while `color` is still the light colour -- the texel is
	   multiplied in afterwards -- so a white brightmap texel means "light this texel fully", and the
	   surface keeps its own colour. Adding the same white to texel * color instead means "make this
	   texel white", and a lava floor came out as a washed-out pink slab: luminance 41 -> 162 at the
	   same camera. Right value, right texture, wrong point in the pipeline. */ \
	/* [rc4l] Under bindless the brightmap is the array's at its own slot, as the texel is, since one binding cannot carry every material's. */ \
	"    vec2 bmUV = fuaFloodUV(vUV);\n" \
	"    vec3 bm = (uSkyColor.w > 0.5 && vLightIndex > 0) ? (vBrightSlot > 0 ? texture(uMaterials[nonuniformEXT(vBrightSlot)], bmUV).rgb : vec3(0.0)) : texture(uBrightmap, bmUV).rgb;\n" \
	"    color = min(color + fuaDesaturate(bm), vec3(1.0));\n" \
	/* [rc4l] Dynamic lights are optional, because a SHADED DECAL must not take them.
	   Its colour is its own -- a scorch mark is black -- and that colour arrives folded into the
	   vertex light, so a black decal has vColor 0 and the dynamic light term is then the ONLY thing
	   left in it. Firing a weapon lit every burn mark in the room with the muzzle flash's colour.
	   GL never has this problem: it applies the decal's colour as an OBJECT COLOUR that multiplies
	   the finished fragment, and zero times anything is still zero. */ \
	/* [rc4l] A sprite or model takes the light GL worked out for its actor, added after the sector light as main.fp adds uDynLightColor, and never the per-light loop. */ \
	"    if (vActorLight.w > 0.5) { if (wantDynLight) color = clamp(color + vActorLight.rgb, 0.0, 1.4); }\n" \
	"    else if (wantDynLight) color = fuaDynLight(color);\n" \
	"    vec3 frag = texel * color;\n" \
	"    return fuaColouredFog(frag, vFog.rgb, fogMode, fogfactor);\n" \
	"}\n" \
	"vec3 fuaShade(vec3 texel) { return fuaShadeEx(texel, true); }\n" \
	/* [rc4l] CompileShader puts GLDEFS' hardware shaders here, with fuaSurfaceTexel, which runs a piece's or samples its texture (features/hwshaders). */ \
	"//FUA_USER_SHADERS\n"

// [rc4l] Opaque: no discard, so early-Z survives and overdrawn fragments are never shaded.
static const char *kScenePSOpaque =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    outColor = vec4(fuaShade(fuaSurfaceTexel(vUV).rgb), 1.0);\n"
	"}\n";

static const char *kScenePS =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    vec4 t = fuaSurfaceTexel(vUV);\n"
	/* [rc4l] GL's solid pass tests no alpha, so an upper, lower or one-sided middle shows its see-through texels in their own colour. */
	"    if (t.a < 0.5 && vPlain < 0.5) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), 1.0);\n"
	"}\n";

// [rc4l] Translucent: no alpha test, and the texture's own alpha multiplied by the piece's. The
// blend factors live in the pipeline, not here, so this one shader serves both the normal and the
// additive pass -- additive differs only in its destination factor.
// [rc4l] The world pass, writing its SURFACE NORMAL out alongside the colour.
//
// This is what makes a projected decal legitimate. A decal reads the depth buffer to find where a
// surface is, and it needs to know which way that surface faces to lay a picture into it. Without a
// normal to read it has to estimate one from neighbouring depth samples, and that estimate is wrong
// in all the places that matter: across a silhouette it straddles two unrelated surfaces, on a
// two-pixel sliver like the front of a low ledge there is no neighbourhood to sample, and at a
// grazing angle it is a difference of nearly-equal large numbers. Every decal artifact reported today
// traces back to that estimate -- a notch cut through a ledge, a mark that changed as the camera
// moved, a scorch smeared into radial streaks.
//
// The estimate was never necessary. This shader has the exact normal already: it arrives per vertex
// from the mesh, correct for walls, flats, slopes and both faces of a 3D floor. Writing it to a
// second target costs one attachment and deletes an entire class of bug.
//
// It also handles moving geometry for free, which the alternative does not: a door's normal is
// written fresh every frame from where the door actually is.
static const char *kScenePSGBuffer =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"layout(location = 1) out vec4 outNormal;\n"
	"void main() {\n"
	"    vec4 t = fuaSurfaceTexel(vUV);\n"
	/* [rc4l] GL's solid pass tests no alpha, so an upper, lower or one-sided middle shows its see-through texels in their own colour. */
	"    if (t.a < 0.5 && vPlain < 0.5) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), 1.0);\n"
	/* Encoded to 0..1 because the target is 8-bit unorm. That is coarse for lighting and ample here:
	   the decal pass uses this to lay axes into a plane and to ask how far a surface is from the
	   impact, and neither needs better than a degree or so. */
	"    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);\n"
	"}\n";

static const char *kScenePSTrans =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	/* [rc4l] A fog boundary sheet (texFlags 1024) is fogboundary.fp: its fog's colour at the fog's opacity there, none with fog off or under a fixed colormap, as gl_CheckFog and RenderFogBoundary decide. */
	"    if ((int(vTexFlags + 0.5) & 1024) != 0) {\n"
	"        if (uFixedColormap.w > 0.5 || vFog.a == 0.0) discard;\n"
	FUA_MIRROR_CLIP_GLSL("vPixelPos.xyz")
	"        outColor = vec4(vFog.rgb, 1.0 - fuaFogFactor(vFog.a, vLightParm.y, vPixelPos, uCameraPos.xyz));\n"
	"        return;\n"
	"    }\n"
	/* [rc4l] A fuzzy sprite under r_fuzztype takes GL's fuzz shader (texFlags / 128), the jagged one sampling a splat of its coordinate. */
	"    int fuzz = (int(vTexFlags + 0.5) >> 7) & 7;\n"
	"    vec4 t = fuzz == 5 ? fuaTexMode(fuaTexel(fuaFuzzSplat(vUV, uFrame.x))) : fuaSurfaceTexel(vUV);\n"
	"    if (fuzz > 0) t = fuaFuzz(fuzz, vUV, uFrame.x, t);\n"
	/* [rc4l] An alpha-tested sprite drops what GL's uAlphaThreshold drops (uFrame.w); anything else only what is all but invisible. */
	"    if ((int(vTexFlags + 0.5) & 2) != 0 ? t.a <= uFrame.w : t.a < 0.04) discard;\n"
	"    vec3 c = fuaShade(t.rgb);\n"
	/* [rc4l] A sprite blended by its own colour (texFlags 32) is GL's GL_SRC_COLOR onto one, colour squared onto the screen, which the additive pipeline draws at alpha 1. */
	"    outColor = (int(vTexFlags + 0.5) & 32) != 0 ? vec4(c * c, 1.0) : vec4(c, t.a * vLightParm.z);\n"
	"}\n";

// [rc4l] The texture is an ALPHA MASK: its red channel is the shape, the colour is the vertex light.
//
// Shaded decals -- scorch marks, most bullet impacts -- store their silhouette in red and carry
// their colour on the decal itself, which RegisterDecal has already folded into the vertex light.
// Sampling one as an ordinary image reads the red channel as brightness and paints a white blob
// where GL paints a black burn. GL reaches the same result with TM_REDTOALPHA plus an object colour.
static const char *kScenePSRedAlpha =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    float a = fuaTexel(vUV).r * vLightParm.z;\n"
	"    if (a <= 0.0) discard;\n"
	// No dynamic lights: see fuaShadeEx. A scorch mark is black, so its vertex colour is zero, and
	// the dynamic light term would be the only thing left in it -- every burn mark in the room lit
	// up with the muzzle flash's colour when the weapon fired.
	"    outColor = vec4(fuaShadeEx(vec3(1.0), false), a);\n"
	"}\n";

#endif // ZX_DGSCENE_GLSL_H
