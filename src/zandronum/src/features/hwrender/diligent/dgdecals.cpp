// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The decal pass: one box per mark, resolved per fragment.
//
// Split out of dgscene.cpp, which had grown to four thousand lines and was carrying the whole of
// this alongside the world, the sky, the sprites and the blended pass. Nothing here touches the
// scene's state -- it reads the depth and normal the world wrote, and the view matrix it wrote them
// with, through dgshared.h -- so it has no business living in the same file.
//
// What the marks themselves are, and why they are boxes, is in features/levelmesh/projdecals.h.

#include "gl/system/gl_system.h"

#include "MapHelper.hpp"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgshared.h"
#include "features/levelmesh/projdecals.h"

#include "r_defs.h"                  // sector_t
#include "r_state.h"                 // sectors, numsectors
#include "m_fixed.h"                 // FIXED2FLOAT
#include "r_data/r_translate.h"       // TRANSLATION, for the alpha-texture rule
#include "gl/textures/gl_material.h"    // FMaterial::tex, to ask whether it uses the base palette
#include "c_cvars.h"
#include "tarray.h"
#include "templates.h"

#include <math.h>

EXTERN_CVAR(Int, fua_decalmode)

// [rc4l] ASPECT CORRECTION: how much of the projection's stretch to undo, 0 to 1.
//
// A planar projection lands its picture on a tilted surface stretched by 1/cos. Correcting it means
// reading the picture in the receiving surface's own plane, which keeps the graphic square -- and
// which is a function of that surface's normal, so it KINKS where the normal jumps, at a corner.
// Straight projection has no kink and stretches instead. The two are the same degree of freedom, so
// this is a dial and not a decision.
//
// 0 is the pure projection everything before this was tuned around; 1 keeps the aspect everywhere.
//
// [rc4l] Defaults to 1 now, because the only marks projected by default are on FLOORS and CEILINGS.
//
// A bolt meets a floor at whatever angle it was travelling, and 1/cos of a shallow angle is a long
// smear: fua_decal_minfacing caps it at 4x, so a 64-unit scorch could be drawn 256 units long. That
// is what "that does not look like a BFG mark" was -- the right graphic, the right size in one axis,
// and stretched down the shot's direction in the other.
//
// The argument for 0 was the KINK: aspect correction reads the picture in the receiving surface's
// own plane, so it steps where the normal steps, at a corner. A mark on a floor stays on one plane
// and has no corner to step at, and walls do not come through here unless fua_decalmode is turned
// on -- so the case the kink was feared for is the case this default does not touch.
CVAR(Float, fua_decal_aspect, 1.0f, CVAR_ARCHIVE)

// [rc4l] How square-on a surface must be to receive any of the mark. See the pixel shader.
//
// It is the stretch limit as much as the facing test: 1/cos is the stretch, so 0.25 caps it at 4x
// and refuses anything flatter. Lower lets the mark reach further round a corner and onto a floor,
// at the price of the streaks that reach brings with it; higher keeps only the surfaces the blast
// genuinely faced.
CVAR(Float, fua_decal_minfacing, 0.25f, CVAR_ARCHIVE)

namespace zx { namespace hwrender {

// ---------------------------------------------------------------------------
// Deferred decals, with one texture array
// ---------------------------------------------------------------------------
//
// [rc4l] A mark is drawn as its own BOX, and resolved per fragment against the depth and normal the
// world has already written. Nothing is cut, nothing is glued, and the mark is never made of pieces.
//
// That last point is the whole reason for this pass. The mesh path cuts the geometry inside the box
// into triangles on the CPU, and a triangle is the smallest thing that can carry an alpha -- so a
// mark that has to fade as it reaches away from the impact fades in SLICES, and every seam between
// two surfaces is a place where two constants meet. Here the fade is a length in the pixel stage,
// which is continuous everywhere including across a corner, and costs nothing to make smooth.
//
// One array of textures, indexed per instance, so every mark in the level is one draw call whatever
// graphic it uses -- rather than one draw per texture, which is what a scorch and its glow
// alternating turned into seventy marks and fifty-five draws. That is the bindless part: the index
// travels in the instance record and the sampler array is bound once.
// [rc4l] How many different decal graphics one draw can reach.
//
// The article's version is unbounded -- a runtime-sized array with descriptor indexing -- and this is
// the same idea with a ceiling on it, because a fixed array needs only ShaderResourceStaticArrays
// where an unbounded one needs the full bindless path, and a Doom level uses tens of decal graphics
// rather than thousands. If a level ever exceeds this the marks past it fall back to the first slot
// rather than vanishing, and fua_dg_decalstats says it happened.
#define FUA_DECAL_TEXTURES     64
#define FUA_DECAL_TEXTURES_STR "64"

static const char *kDeferredDecalVS =
	"#version 450\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	/* One record per mark: where it landed, which way its picture is turned, how far it reaches,
	   what to paint, and which texture to paint it with. */
	"struct DecalRec { vec4 centre; vec4 axisU; vec4 axisV; vec4 axisN; vec4 color; vec4 params; vec4 anchor; };\n"
	"layout(std430, binding = 9) readonly buffer Decals { DecalRec decals[]; };\n"
	/* [rc4l] Every sector's floor and ceiling height, refreshed once a frame.

	   A projected mark is cut from geometry, which is what lets it mark a floor and also why it does
	   not follow one: hold a fixed world height and a mark on a lift stays behind as the lift rises.
	   So a mark stores an OFFSET from a named plane and reads the plane's height here.

	   Read on the GPU rather than folded in by the CPU so the cost follows the number of SECTORS,
	   not the number of marks: the decal records stop changing once made, and only this small table
	   is rewritten. Ten thousand marks cost the same as ten. */ \
	"layout(std430, binding = 10) readonly buffer Planes { vec4 planes[]; };\n"
	"layout(location = 0) out vec3 vCentre;\n"
	"layout(location = 1) out vec3 vAxisU;\n"
	"layout(location = 2) out vec3 vAxisV;\n"
	"layout(location = 3) out vec3 vAxisN;\n"
	"layout(location = 4) out vec4 vColor;\n"
	"layout(location = 5) out vec4 vParams;\n"
	"layout(location = 6) flat out int vTex;\n"
	"const vec3 kCorner[8] = vec3[8](\n"
	"    vec3(-1,-1,-1), vec3( 1,-1,-1), vec3( 1, 1,-1), vec3(-1, 1,-1),\n"
	"    vec3(-1,-1, 1), vec3( 1,-1, 1), vec3( 1, 1, 1), vec3(-1, 1, 1));\n"
	"const vec2 kQuad[6] = vec2[6](\n"
	"    vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));\n"
	"void main() {\n"
	"    DecalRec d = decals[gl_InstanceIndex];\n"
	/* Ride the plane: the stored height is an offset from it, so this is where the mark is NOW. */ \
	"    vec3 c = d.centre.xyz;\n"
	"    int aSec = int(d.anchor.x);\n"
	/* The plane EQUATION, evaluated where this mark actually is. A single height per sector is
	   only right for a level plane: on a slope the mark stores its offset from the plane at ITS
	   OWN point, so reading the height at the sector centre puts it out by the tilt across the
	   sector -- tens of units here. A big scorch survived that because its box is deep enough to
	   still catch the floor; a bullet puff, a plasma mark and the BFG's own glow all missed and
	   vanished. Ax + By + Cz + D = 0, so z = -(Ax + By + D)/C, and w carries 1/C. */ \
	"    if (aSec >= 0) {\n"
	"        vec4 pl = planes[aSec * 2 + int(d.anchor.y)];\n"
	"        c.y = -(pl.x * c.x + pl.y * c.z + pl.z) * pl.w + d.anchor.z;\n"
	"    }\n"
	"    vCentre = c;\n"
	"    vAxisU = d.axisU.xyz; vAxisV = d.axisV.xyz; vAxisN = d.axisN.xyz;\n"
	"    vColor = d.color;\n"
	"    vParams = d.params;\n"
	"    vTex = int(d.centre.w);\n"
	/* Where this mark's box lands on screen. Its eight corners are projected and the extremes kept
	   -- conservative, which is all a bound has to be, and it means the pixel stage only runs where
	   the mark could possibly be rather than over the whole screen for every mark. */
	"    float reach = max(d.params.x, max(d.params.y, d.params.z));\n"
	"    vec2 lo = vec2( 1e9), hi = vec2(-1e9);\n"
	"    bool whole = false;\n"
	/* Inside the box, or straddling the plane through the camera, and there are no honest screen
	   bounds to compute -- so take the whole screen and let the pixel stage decide. This is the case
	   that made a box vanish when you stood in it. */
	"    if (distance(uCameraPos.xyz, c) < reach * 1.75) whole = true;\n"
	"    for (int i = 0; i < 8 && !whole; i++) {\n"
	"        vec4 cp = uMVP * vec4(c + kCorner[i] * reach, 1.0);\n"
	"        if (cp.w <= 0.0001) { whole = true; break; }\n"
	"        vec2 ndc = cp.xy / cp.w;\n"
	"        lo = min(lo, ndc); hi = max(hi, ndc);\n"
	"    }\n"
	"    if (whole) { lo = vec2(-1.0); hi = vec2(1.0); }\n"
	"    lo = max(lo, vec2(-1.0)); hi = min(hi, vec2(1.0));\n"
	/* Straight to clip space. Depth is unused -- the pass neither tests nor writes it -- so the quad
	   sits at the near plane and nothing can clip it away. */
	"    vec2 q = kQuad[gl_VertexIndex];\n"
	"    gl_Position = vec4(mix(lo, hi, q), 0.0, 1.0);\n"
	"}\n";

static const char *kDeferredDecalPS =
	"#version 450\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(binding = 6) uniform Decal { mat4 uInvMVP; vec4 uDecalDebug; };\n"
	"layout(binding = 7) uniform sampler2D uSceneDepth;\n"
	"layout(binding = 8) uniform sampler2D uSceneNormal;\n"
	/* [rc4l] An ARRAY, indexed per instance, so the level is two draws whatever it is wearing.

	   This was tried once and parked, with the note that the array bound and reflected its 64 slots
	   without complaint and every element sampled white. The reason was the BINDING, not the index.
	   The draw loop reached this pass through GetMaterialSRB, which binds uTex with a scalar Set --
	   right for a lone sampler, and against an array a binding of element 0 alone with 1..63 left
	   undefined. Undefined reads white, so every slot but the first came back white and the indexing
	   was blamed for it.

	   The pass owns its binding now and sets the whole array with SetArray. uDecalDebug.z still pins
	   every mark to slot zero, which is the experiment that tells a bad index from a bad binding
	   apart: pinned and visible means the array is bound and the index is wrong; pinned and blank
	   means nothing is bound and the index was never the problem. */
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) in vec3 vCentre;\n"
	"layout(location = 1) in vec3 vAxisU;\n"
	"layout(location = 2) in vec3 vAxisV;\n"
	"layout(location = 3) in vec3 vAxisN;\n"
	"layout(location = 4) in vec4 vColor;\n"
	"layout(location = 5) in vec4 vParams;\n"
	"layout(location = 6) flat in int vTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(uScreen.x, uScreen.y);\n"
	"    float d = texture(uSceneDepth, uv).r;\n"
	/* Nothing was drawn here -- the far plane. There is no surface to paint. */
	"    if (d >= 1.0) discard;\n"
	/* Reconstruct the world position of whatever the depth buffer says is here. Clip space is x,y in
	   -1..1 and z in 0..1, the Vulkan convention this backend's projection uses.
	   [rc4l] Y is flipped between the framebuffer and clip space: gl_FragCoord.y counts DOWN from the
	   top while clip-space y runs up, and reconstructing without the flip mirrors the world position
	   vertically. Because the error grows with distance from the centre of the screen, the mark then
	   appears to slide about as the camera turns -- "it follows the camera" is what that looks like. */
	"    vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);\n"
	"    vec4 world = uInvMVP * clip;\n"
	"    vec3 P = world.xyz / world.w;\n"
	"    vec3 rel = P - vCentre;\n"
	/* Into the picture's own axes. U and V arrive divided by their half-extents, so the picture is
	   whatever falls in -1..1; N is unit, so the depth test is in world units and asymmetric -- the
	   box reaches further back towards the shooter than forward through the surface. */
	/* The depth slab first, which needs nothing but the projection axis. */
	"    float w = dot(rel, vAxisN);\n"
	"    if (w < -vParams.x || w > vParams.y) discard;\n"
	/* [rc4l] The surface's EXACT normal, out of the G-buffer. Estimating it from four depth taps was
	   wrong in every place that mattered: across a silhouette it straddled two surfaces, on a
	   two-pixel sliver there was no neighbourhood to sample, and at a grazing angle it was a
	   difference of nearly-equal large numbers. Alpha zero means nothing was drawn here. */
	"    vec4 gbuf = texture(uSceneNormal, uv);\n"
	"    if (gbuf.a < 0.5) discard;\n"
	"    vec3 nrm = normalize(gbuf.xyz * 2.0 - 1.0);\n"
	/* [rc4l] How square-on a surface must be to receive any of the mark, as a cosine.
	
	   Two jobs in one number. Below zero a surface faces AWAY from the projectile and cannot have
	   been sprayed by it -- without that a pillar prints a mirrored copy of the mark on the face
	   nobody shot at. Above zero it is the STRETCH limit: a planar projection lands its picture on a
	   surface stretched by 1/cos, so a floor nearly edge-on to a horizontal shot is stretched ten or
	   twenty times and its texels come out as long straight streaks running away from the corner.
	
	   No correction fixes that case -- a surface parallel to the projection has no picture on it to
	   correct, the mapping is genuinely infinite -- so the only honest answers are to fade it out or
	   to refuse it. This refuses it. uDecalDebug.y is the cosine; 0.25 is about 75 degrees. */
	"    float facing = -dot(nrm, vAxisN);\n"
	"    if (facing < uDecalDebug.y) discard;\n"
	/* [rc4l] ASPECT CORRECTION, per fragment -- uDecalDebug.w between 0 and 1.
	
	   A planar projection lands its picture on a tilted surface stretched by 1/cos: correct for a
	   slide projector, and a smear on a decal. The fix is to read the picture in the SURFACE's own
	   plane instead -- project the picture's axes onto it and restore their length, so a unit of
	   picture is a unit of surface however the surface is turned.
	
	   It cannot be free, and the cost is worth stating: this is a function of the NORMAL, and the
	   normal jumps at a corner, so a mark spanning one gets a kink in its picture there. Straight
	   projection has no kink and stretches instead. There is no third option -- the two properties
	   are the same degree of freedom -- so it is a dial rather than a decision, and 0 is the shape
	   the earlier work was tuned around. */
	"    vec3 rAx = vAxisU, uAx = vAxisV;\n"
	"    if (uDecalDebug.w > 0.001) {\n"
	"        vec3 rS = vAxisU - nrm * dot(vAxisU, nrm);\n"
	"        vec3 uS = vAxisV - nrm * dot(vAxisV, nrm);\n"
	"        float lr = length(rS), lu = length(uS);\n"
	/* Edge-on to one of the picture's own axes there is nothing left to renormalise, and the
	   correction would divide by nothing. Keep the uncorrected axis, which is what it degenerates
	   to anyway. */
	"        if (lr > 1e-4) rS *= length(vAxisU) / lr; else rS = vAxisU;\n"
	"        if (lu > 1e-4) uS *= length(vAxisV) / lu; else uS = vAxisV;\n"
	"        rAx = mix(vAxisU, rS, uDecalDebug.w);\n"
	"        uAx = mix(vAxisV, uS, uDecalDebug.w);\n"
	"    }\n"
	"    float u = dot(rel, rAx);\n"
	"    float v = dot(rel, uAx);\n"
	"    if (abs(u) > 1.0 || abs(v) > 1.0) discard;\n"
	"    vec2 t = vec2(u * 0.5 + 0.5, 0.5 - v * 0.5);\n"
	/* [rc4l] uDecalDebug.z pins every mark to slot zero, which is the one experiment that tells
	   a bad INDEX from a bad BINDING apart: if the mark appears when pinned, the array is bound
	   and the index is wrong; if it stays blank, nothing is bound and the index is innocent. */
	/* [rc4l] The slot travels in the instance record, so one draw covers every graphic.
	   nonuniformEXT because the index differs BETWEEN INSTANCES of the same draw, which is the
	   whole point of it -- without the qualifier that is undefined behaviour, and it shows up as
	   marks wearing each other's textures depending on how the driver batches waves. */ \
	/* [rc4l] The gradients are taken BEFORE the array is indexed, and handed over explicitly.

	   texture() picks its mip from screen-space derivatives, and those are only defined when the
	   whole quad agrees on which sampler it is reading. Here it does not: the slot varies BETWEEN
	   INSTANCES of one draw, which is the entire point of the array, so the implicit LOD is undefined
	   and a driver is free to answer with the smallest mip. That is a 1x1 texel -- the graphic's
	   average colour, stretched over the mark's whole box -- so every mark painted its own bounding
	   rectangle in a flat wash. A pale graphic gave a white box and a dark one gave a black box.

	   It looked exactly like a wrong texture, and survived being read as one for a long time: the
	   mask came out 1 across the quad, the picture coordinate was a clean 0..1, the slots were in
	   range and the textures were real. What separated it was pinning every mark to slot zero --
	   which makes the index uniform, and the marks came back. Not a different texture: the same
	   texture, sampled at a mip the driver was entitled to pick.

	   dFdx/dFdy of t are uniform control flow, so they are well defined; textureGrad then uses them
	   instead of guessing, and mipmapping still works. */ \
	"    vec2 tdx = dFdx(t), tdy = dFdy(t);\n"
	"    vec4 texel = textureGrad(uTex, t, tdx, tdy);\n"
	/* A shaded decal's texture is an alpha MASK: the red channel is the shape and the colour comes
	   from the decal itself. Sampled as an ordinary image the red channel reads as brightness and a
	   black burn paints a white blob. */
	/* [rc4l] 4 and 5 show the TEXEL itself -- colour, then alpha -- and they answer BEFORE anything
	   can discard the fragment, which the later debug views cannot: by the time those run, every
	   transparent part of the graphic is already gone, so a flat mark and a correct one look equally
	   solid. Every other view shows a term derived from the texel, and when a mark reads flat the
	   question is precisely which of the two is at fault: the bytes, or what was made of them.

	   The sampler is the only thing left that still knows. Reading the bytes on the CPU does not
	   answer it -- the upload is a hundred frames past by the time anyone asks and the cache answers
	   instead, and asking FMaterial for the buffer a second time returns nothing at all. */ \
	/* 6 asks the same sampler for mip ZERO explicitly. If 4 is flat and 6 is not, the bytes are fine
	   and the level being read is the fault; if both are flat, the texture really did upload flat. */ \
	/* 7 shows the SLOT this mark is reading, as a shade: 0 black, 1 a quarter grey, and so on. The
	   CPU says which slot it wrote into every record; this says which one arrived. */ \
	"    if (uDecalDebug.x > 3.5) {\n"
	"        if (uDecalDebug.x > 6.5) outColor = vec4(vec3(float(vTex) * 0.25), 1.0);\n"
	"        else if (uDecalDebug.x > 5.5) outColor = vec4(textureLod(uTex, t, 0.0).rgb, 1.0);\n"
	"        else outColor = (uDecalDebug.x < 4.5) ? vec4(texel.rgb, 1.0) : vec4(vec3(texel.a), 1.0);\n"
	"        return;\n"
	"    }\n"
	"    bool redAlpha = vParams.w > 0.5;\n"
	"    float mask = redAlpha ? texel.r : texel.a;\n"
	"    float a = mask * vColor.a;\n"
	/* [rc4l] The run-out, per fragment. Inside the picture's own radius nothing fades, so the mark on
	   the surface that was actually hit is exactly what the decal says it is; beyond that the mark is
	   reaching onto geometry the picture never covered and it runs out smoothly.
	   Radial, so it does not know a corner is there -- which is why there is no step at one. This is
	   the same rule the mesh path applies in slices, done properly. */
	/* The picture's own corner radius, recovered from the axes: they arrive divided by their
	   half-extents, so the length of one is the reciprocal of the other. Cheaper than another
	   slot in the record, and it cannot fall out of step with the axes it is derived from. */
	"    float r = length(rel);\n"
	"    float hw = 1.0 / max(length(vAxisU), 1e-6);\n"
	"    float hh = 1.0 / max(length(vAxisV), 1e-6);\n"
	"    float inner = sqrt(hw*hw + hh*hh);\n"
	"    if (vParams.z > inner) a *= 1.0 - smoothstep(inner, vParams.z, r);\n"
	"    if (a <= 0.004) discard;\n"
	/* 1 shows the picture coordinate, 2 the mask that came out of the texture, 3 how squarely the
	   surface was met. A mark that is wrong looks the same whichever of the three is at fault. */
	/* [rc4l] 4 and 5 show the TEXEL itself -- colour, then alpha -- before anything is done with it.
	   Every other view shows a term derived from it, and when a mark reads flat the question is
	   which: the bytes, or what was made of them. Reading the bytes on the CPU does not answer it
	   either, twice over -- the upload is a hundred frames past by the time anyone asks and the cache
	   answers instead, and asking FMaterial for the buffer again returns nothing at all. The sampler
	   is the only thing that still knows. */ \
	"    if (uDecalDebug.x > 0.5) {\n"
	"        if (uDecalDebug.x < 1.5) outColor = vec4(fract(t), 0.0, 1.0);\n"
	"        else if (uDecalDebug.x < 2.5) outColor = vec4(vec3(mask), 1.0);\n"
	"        else outColor = vec4(vec3(facing), 1.0);\n"
	"        return;\n"
	"    }\n"
	/* [rc4l] An alpha-mask decal takes its colour from the DECAL, not from the texture.
	   Multiplying the two together looks harmless and is not: the mask lives in the red channel,
	   so a green glow times (mask, 0, 0) is black, and the BFG's glow would disappear into its
	   own scorch. An ordinary decal is the other way round -- the texture is the picture and the
	   colour is white. */
	"    vec3 rgb = redAlpha ? vColor.rgb : (vColor.rgb * texel.rgb);\n"
	"    outColor = vec4(rgb, a);\n"
	"}\n";

// [rc4l] Show what the pass is computing instead of what it paints: the picture coordinate, so a
// mark that is being cut somewhere says WHERE rather than merely being absent. Every term that can
// discard a fragment produces the same result on screen, and guessing which one cost several rounds.
CVAR(Int, fua_dg_decaldebug, 0, 0)

// [rc4l] Pin every mark to the first texture slot. See the pixel shader: it separates a wrong
// index from an unbound array, which look identical on screen.
CVAR(Bool, fua_dg_decalslot0, false, 0)


// [rc4l] One record per mark, read by the vertex stage -- see kDeferredDecalVS.
struct DeferredDecalInstance
{
	float centre[4];   // xyz where it landed (mesh space), w = which texture slot
	float axisU[4];    // xyz the picture's across-axis, divided by its half-width
	float axisV[4];    // xyz the picture's up-axis, divided by its half-height
	float axisN[4];    // xyz unit, the way the projectile was going
	float color[4];    // rgb tint and alpha, already faded
	float params[4];   // near, far, run-out radius, red-as-alpha flag
	// [rc4l] Which sector plane this mark rides: (sector, 0 floor / 1 ceiling, height above it).
	// sector < 0 means it rides nothing, which is every mark on a wall.
	float anchor[4];
};

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_ddPSO;      // ordinary blend
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_ddAddPSO;   // additive
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_ddSRB;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_ddAddSRB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_ddInstBuf;
static Diligent::IBufferView *g_ddInstSRV = NULL;
// [rc4l] One record per sector: (floor height, ceiling height, unused, unused).
//
// Rewritten once a frame, which is the whole point -- the decal records themselves become static
// once made, so a mark riding a lift costs nothing per mark. Sized to the level and reallocated
// only when the level changes.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_ddPlaneBuf;
static Diligent::IBufferView *g_ddPlaneSRV = NULL;
static int g_ddPlaneCapacity = 0;
static unsigned int g_ddCapacity = 0;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_ddCB;
static int g_ddDrawn = 0, g_ddBoxes = 0, g_ddTextures = 0;
static const char *g_ddBail = "";

void GetDeferredDecalStats(int &boxes, int &draws, int &textures, const char **bail)
{
	boxes = g_ddBoxes; draws = g_ddDrawn; textures = g_ddTextures;
	if (bail) *bail = g_ddBail;
}

void ReleaseDeferredDecalPass()
{
	g_ddSRB.Release();
	g_ddAddSRB.Release();
	g_ddPSO.Release();
	g_ddAddPSO.Release();
}

static bool EnsureDeferredDecalPass()
{
	if (g_ddPSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::ShaderCreateInfo ci;
	ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
	ci.Desc.Name = "fua deferred decal VS";
	ci.Source = kDeferredDecalVS;
	dev->CreateShader(ci, &vs);
	ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
	ci.Desc.Name = "fua deferred decal PS";
	ci.Source = kDeferredDecalPS;
	dev->CreateShader(ci, &ps);
	if (!vs || !ps) { g_ddBail = "shader compilation failed"; return false; }

	if (!g_ddInstBuf)
	{
		g_ddCapacity = 1024;
		Diligent::BufferDesc bd;
		bd.Name = "fua deferred decal instances";
		bd.Size = (Diligent::Uint64)g_ddCapacity * sizeof(DeferredDecalInstance);
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = sizeof(DeferredDecalInstance);
		dev->CreateBuffer(bd, nullptr, &g_ddInstBuf);
		if (!g_ddInstBuf) { g_ddCapacity = 0; g_ddBail = "instance buffer creation failed"; return false; }
		g_ddInstSRV = g_ddInstBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
	}

	// [rc4l] The plane table, sized to the level.
	if (!g_ddPlaneBuf || g_ddPlaneCapacity < numsectors)
	{
		g_ddPlaneBuf.Release();
		g_ddPlaneSRV = NULL;
		g_ddPlaneCapacity = (numsectors > 0) ? numsectors : 1;
		Diligent::BufferDesc bd;
		bd.Name = "fua decal sector planes";
		bd.Size = (Diligent::Uint64)g_ddPlaneCapacity * 32;   // floor and ceiling equations
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = 16;   // one vec4; two per sector
		dev->CreateBuffer(bd, nullptr, &g_ddPlaneBuf);
		if (!g_ddPlaneBuf) { g_ddPlaneCapacity = 0; g_ddBail = "plane buffer creation failed"; return false; }
		g_ddPlaneSRV = g_ddPlaneBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
	}

	if (!g_ddCB)
	{
		Diligent::BufferDesc bd;
		bd.Name = "fua deferred decal constants";
		bd.Size = 20 * sizeof(float);   // the inverse view-projection, plus a debug selector
		bd.Usage = Diligent::USAGE_DYNAMIC;
		bd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		bd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(bd, nullptr, &g_ddCB);
		if (!g_ddCB) { g_ddBail = "constant buffer creation failed"; return false; }
	}

	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneNormal", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	// CLAMP both ways: the picture is bounded by the box, and a wrap would tile the graphic across
	// whatever the box happens to reach.
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	static Diligent::SamplerDesc depthSamp;
	depthSamp.MinFilter = depthSamp.MagFilter = depthSamp.MipFilter = Diligent::FILTER_TYPE_POINT;
	depthSamp.AddressU = depthSamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneDepth", depthSamp },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneNormal", depthSamp },
	};

	for (int pass = 0; pass < 2; pass++)
	{
		const bool additive = (pass == 1);
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = additive ? "fua deferred decal PSO additive" : "fua deferred decal PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
		// [rc4l] NO depth attachment, which is not the same as "depth test off".
		//
		// This pass READS the scene depth as a texture, and it is the texture the world pass just drew
		// into. Leaving it attached while sampling it is a read-write hazard on one resource: the
		// driver is entitled to serve stale or partially-resolved tiles, and it does -- a fine diagonal
		// hatch across every surface a decal reached, which reads exactly like z-fighting and is not.
		pci.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		// Nothing to cull and no depth to test: the geometry is a screen-space quad over the mark's
		// own bounds, so it has no inside to be caught in and each pixel is visited once from
		// anywhere. The shader finds the surface in the depth BUFFER; the quad's own depth is nothing.
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		{
			auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
			rt.BlendEnable = true;
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
			rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
			rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
		}
		// No input layout: the quad comes from gl_VertexIndex and the mark from gl_InstanceIndex.
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 3;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 3;
		pci.pVS = vs;
		pci.pPS = ps;

		Diligent::RefCntAutoPtr<Diligent::IPipelineState> made;
		dev->CreateGraphicsPipelineState(pci, &made);
		if (!made) { g_ddBail = "pipeline creation failed"; return false; }
		for (int st = 0; st < 2; st++)
		{
			const Diligent::SHADER_TYPE stage = st ? Diligent::SHADER_TYPE_PIXEL : Diligent::SHADER_TYPE_VERTEX;
			if (auto *v = made->GetStaticVariableByName(stage, "Constants")) v->Set(SceneConstantsCB());
			if (auto *v = made->GetStaticVariableByName(stage, "Decal")) v->Set(g_ddCB);
			if (auto *v = made->GetStaticVariableByName(stage, "Decals")) v->Set(g_ddInstSRV);
			if (auto *v = made->GetStaticVariableByName(stage, "Planes")) v->Set(g_ddPlaneSRV);
		}
		if (additive) g_ddAddPSO = made; else g_ddPSO = made;
	}

	g_ddPSO->CreateShaderResourceBinding(&g_ddSRB, true);
	g_ddAddPSO->CreateShaderResourceBinding(&g_ddAddSRB, true);
	if (!g_ddSRB || !g_ddAddSRB) { g_ddBail = "resource binding creation failed"; return false; }

	return true;
}

// [rc4l] Draw this frame's marks as boxes, resolved against the depth and normal buffers.
//
// Runs AFTER the world and before the sprites: a decal marks a surface, so it belongs with the
// surfaces, and anything standing in front of one must be drawn over it. That is the whole of the
// ordering problem, settled by which pass a thing is in rather than by sorting a mark against every
// sprite in the room.
void DrawDeferredDecals(Diligent::IDeviceContext *ctx)
{
	g_ddDrawn = 0;
	g_ddBoxes = 0;
	g_ddTextures = 0;
	g_ddBail = "";

	const zx::levelmesh::GpuDecal *decals = NULL;
	const int n = zx::levelmesh::GetProjectedDecalsGpu(&decals);
	if (n <= 0 || decals == NULL) { g_ddBail = "nothing registered"; return; }
	if (!EnsureDeferredDecalPass()) return;

	Diligent::ITextureView *depthSRV = SceneDepthSRV();
	Diligent::ITextureView *normalSRV = SceneNormalSRV();
	if (!depthSRV) { g_ddBail = "no scene depth"; return; }
	if (!normalSRV) { g_ddBail = "no scene normal"; return; }

	float invMVP[16];
	if (!InvertMatrix4(SceneMVP(), invMVP)) { g_ddBail = "view matrix not invertible"; return; }
	{
		Diligent::MapHelper<float> cb(ctx, g_ddCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int i = 0; i < 16; i++) cb[i] = invMVP[i];
		cb[16] = (float)fua_dg_decaldebug;
		cb[17] = (float)fua_decal_minfacing;
		// [rc4l] fua_dg_decalslot0, which was declared and then never read.
		//
		// This pins every mark to texture slot zero, and it is the one experiment that separates "the
		// graphic is wrong" from "the mark is sampling the wrong slot" -- a mask that is 1 across the
		// whole quad is what BOTH look like, because an out-of-range slot lands on the white padding
		// the array is filled with. Hardcoded to zero here, the knob answered every question with
		// silence, and its absence looked exactly like a knob that was on and telling the truth.
		cb[18] = fua_dg_decalslot0 ? 1.f : 0.f;
		cb[19] = (float)fua_decal_aspect;
	}

	// [rc4l] Every graphic this frame needs, gathered into ONE array the shader indexes per mark.
	//
	// This is what makes the whole level one draw. Grouping by texture instead -- which is what a
	// single bound sampler forces -- costs a draw call every time the graphic changes, and a scorch
	// and its glow alternate, so seventy marks came to fifty-five draws. The slot travels in the
	// instance record and nothing has to be sorted to keep it.
	static TArray<const void *> mats;
	static TArray<int> matTrans;
	static TArray<Diligent::IDeviceObject *> views;
	static TArray<DeferredDecalInstance> instAlpha, instAdd;
	static TArray<const void *> matAlpha, matAdd, matOf;
	mats.Clear(); matTrans.Clear(); views.Clear(); instAlpha.Clear(); instAdd.Clear();
	matAlpha.Clear(); matAdd.Clear(); matOf.Clear();

	Diligent::IDeviceObject *fallback = GetMaterialSRV(NULL, 0);
	for (int i = 0; i < n; i++)
	{
		const zx::levelmesh::GpuDecal &d = decals[i];
		if (d.a <= 0.004f || d.material == NULL) continue;

		// [rc4l] Keyed on the material AND its translation, because one graphic under two remaps is
		// two different pictures. Keyed on the material alone, the second remap silently reuses the
		// first one's slot.
		int slot = -1;
		for (unsigned m = 0; m < mats.Size(); m++)
			if (mats[m] == d.material && matTrans[m] == d.translation) { slot = (int)m; break; }
		if (slot < 0)
		{
			if (mats.Size() >= FUA_DECAL_TEXTURES) slot = 0;   // over the ceiling: see FUA_DECAL_TEXTURES
			else
			{
				// [rc4l] A shaded mark asks for its texture the way GL asks for it.
				//
				// gl_renderstate.h sets a special translation for an alpha texture whose graphic uses
				// the base palette: the colour INDEX becomes the alpha directly, which is what makes
				// the red channel the mark's shape. Asked for with translation 0 instead, the red
				// channel is just how red the graphic is -- near 1 across a pale scorch -- so the mask
				// reads 1 everywhere and the mark paints its whole box solid.
				// [rc4l] The mark's OWN remap, which this pass never asked for.
				//
				// gl_decal.cpp binds with decal->Translation and the wall path carries it too; only
				// here was it hardcoded to zero, so the mark drew whatever the untranslated graphic
				// happens to be. For the BFG's scorch that is a flat mask, and the mark painted its
				// whole box in one colour -- while the same texture on a wall, asked for with its
				// translation, came out as soot.
				int trans = d.translation;
				if (d.redToAlpha)
				{
					FMaterial *fm = (FMaterial *)d.material;
					if (fm != NULL && fm->tex != NULL && fm->tex->UseBasePalette())
						trans = TRANSLATION(TRANSLATION_Standard, 8);
				}
				Diligent::IDeviceObject *srv = GetMaterialSRV(d.material, trans);
				if (!srv) continue;
				// [rc4l] Say when a mark's texture came back as the WHITE fallback.
				//
				// GetMaterialSRV answers white for anything it cannot produce, which is the right default
				// and an invisible failure: a mask of 1 everywhere paints the mark's whole box solid, and
				// that looks like a clipping fault, a blend fault or a UV fault in turn.
				if (fua_dg_decaldebug > 0)
				{
					FMaterial *fm2 = (FMaterial *)d.material;
					Printf("decal tex %s %dx%d box %.0fx%.0f near %.0f far %.0f: slot %u, redToAlpha %d, additive %d, "
						"rgba %.2f %.2f %.2f %.2f, basePalette %d, canvas %d, warp %d, complex %d, "
						"translation %d%s\n",
						(fm2 && fm2->tex && fm2->tex->Name != NULL) ? fm2->tex->Name : "?",
						(fm2 && fm2->tex) ? fm2->tex->GetWidth() : -1,
						(fm2 && fm2->tex) ? fm2->tex->GetHeight() : -1,
						d.halfW, d.halfH, d.near_, d.far_,
						mats.Size(), d.redToAlpha ? 1 : 0, d.additive ? 1 : 0,
						d.r, d.g, d.b, d.a,
						(fm2 && fm2->tex && fm2->tex->UseBasePalette()) ? 1 : 0,
						(fm2 && fm2->tex && fm2->tex->bHasCanvas) ? 1 : 0,
						(fm2 && fm2->tex && fm2->tex->bWarped) ? 1 : 0,
						(fm2 && fm2->tex && fm2->tex->bComplex) ? 1 : 0,
						(int)trans, (srv == GetMaterialSRV(NULL, 0)) ? "  <-- WHITE FALLBACK" : "");
				}
				slot = (int)mats.Size();
				mats.Push(d.material);
				matTrans.Push(d.translation);
				views.Push(srv);
			}
		}

		DeferredDecalInstance inst;
		inst.centre[0] = d.centre[0]; inst.centre[1] = d.centre[1]; inst.centre[2] = d.centre[2];
		inst.centre[3] = (float)slot;
		inst.axisU[0] = d.right[0]; inst.axisU[1] = d.right[1]; inst.axisU[2] = d.right[2]; inst.axisU[3] = 0.f;
		inst.axisV[0] = d.up[0];    inst.axisV[1] = d.up[1];    inst.axisV[2] = d.up[2];    inst.axisV[3] = 0.f;
		inst.axisN[0] = d.axis[0];  inst.axisN[1] = d.axis[1];  inst.axisN[2] = d.axis[2];  inst.axisN[3] = 0.f;
		inst.color[0] = d.r; inst.color[1] = d.g; inst.color[2] = d.b; inst.color[3] = d.a;
		inst.params[0] = d.near_;
		inst.params[1] = d.far_;
		// Where the run-out ends: the picture's own corner radius plus the box's reach behind it.
		// Inside the corner radius nothing fades, which is stated in the shader.
		inst.params[2] = sqrtf(d.halfW*d.halfW + d.halfH*d.halfH) + d.near_;
		inst.params[3] = d.redToAlpha ? 1.f : 0.f;
		// [rc4l] The plane this mark rides, straight from the record that made it.
		inst.anchor[0] = (float)d.anchorSector;
		inst.anchor[1] = (float)d.anchorPlane;
		inst.anchor[2] = d.anchorOffset;
		inst.anchor[3] = 0.f;
		if (d.additive) { instAdd.Push(inst); matAdd.Push(d.material); }
		else { instAlpha.Push(inst); matAlpha.Push(d.material); }
	}

	if (fua_dg_decaldebug > 0)
	{
		// [rc4l] What slot each mark actually asks for, against how many were filled.
		//
		// A mask of 1 across the whole quad has two causes that look identical -- the graphic is
		// wrong, or the mark is reading a slot nothing was put in, which is the white padding. The
		// range says which, and it is two numbers.
		int lo = 1 << 30, hi = -1;
		for (unsigned i = 0; i < instAlpha.Size(); i++)
		{
			const int s = (int)instAlpha[i].centre[3];
			if (s < lo) lo = s;  if (s > hi) hi = s;
		}
		for (unsigned i = 0; i < instAdd.Size(); i++)
		{
			const int s = (int)instAdd[i].centre[3];
			if (s < lo) lo = s;  if (s > hi) hi = s;
		}
		Printf("decal slots used %d..%d, %u filled of %d\n", lo, hi, mats.Size(), FUA_DECAL_TEXTURES);
	}

	const unsigned total = instAlpha.Size() + instAdd.Size();
	if (total == 0) { g_ddBail = "every mark filtered out (no texture, or faded to nothing)"; return; }
	g_ddBoxes = (int)total;
	g_ddTextures = (int)mats.Size();

	// Pad the array: an unbound slot in a sampler array reads as undefined, and the shader may index
	// any of them from any instance in the draw.
	while (views.Size() < FUA_DECAL_TEXTURES) views.Push(fallback);

	// [rc4l] Ordinary blend first, additive second. Additive only ever brightens, so nothing can
	// meaningfully be drawn over it and it can very easily be drawn under something and lost -- a
	// scorch and the glow that belongs on top of it land in exactly the same place.
	static TArray<DeferredDecalInstance> all;
	all.Clear();
	for (unsigned i = 0; i < instAlpha.Size(); i++) { all.Push(instAlpha[i]); matOf.Push(matAlpha[i]); }
	for (unsigned i = 0; i < instAdd.Size(); i++) { all.Push(instAdd[i]); matOf.Push(matAdd[i]); }
	if (all.Size() > g_ddCapacity) all.Resize(g_ddCapacity);
	// [rc4l] Refresh the plane heights. O(sectors), and nothing here depends on how many marks
	// exist -- which is the reason the anchor is resolved on the GPU rather than folded into every
	// decal record by the CPU.
	if (g_ddPlaneBuf && numsectors > 0)
	{
		static TArray<float> planeData;
		planeData.Resize((unsigned)numsectors * 8);
		for (int si = 0; si < numsectors; si++)
		{
			const sector_t &sec = sectors[si];
			// [rc4l] The plane EQUATION, not a height. secplane_t is Ax + By + Cz + D = 0 in fixed
			// point, so the float form is z = -(Ax + By + D)/C and 1/C is stored ready to multiply.
			// Storing a height instead only works for a level plane, and got every small mark on a
			// slope placed far enough off the surface to disappear.
			const secplane_t *pl[2] = { &sec.floorplane, &sec.ceilingplane };
			for (int q = 0; q < 2; q++)
			{
				const float C = FIXED2FLOAT(pl[q]->c);
				planeData[si*8 + q*4 + 0] = FIXED2FLOAT(pl[q]->a);
				planeData[si*8 + q*4 + 1] = FIXED2FLOAT(pl[q]->b);
				planeData[si*8 + q*4 + 2] = FIXED2FLOAT(pl[q]->d);
				planeData[si*8 + q*4 + 3] = (C != 0.f) ? (1.f / C) : 0.f;
			}
		}
		ctx->UpdateBuffer(g_ddPlaneBuf, 0, (Diligent::Uint64)numsectors * 32, &planeData[0],
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}

	ctx->UpdateBuffer(g_ddInstBuf, 0, (Diligent::Uint64)all.Size() * sizeof(DeferredDecalInstance),
		&all[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// The depth buffer comes off the render target for this pass -- it is being READ.
	auto *swap = GetSwapChain();
	Diligent::ITextureView *rtv = swap ? swap->GetCurrentBackBufferRTV() : NULL;
	if (rtv) ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// [rc4l] TWO draws for the whole level, whatever it is wearing.
	//
	// The instance record has carried its texture slot all along and the shader now indexes the
	// sampler array with it, so a draw no longer has to be one material wide. What is left that can
	// force a break is the BLEND MODE, because that is fixed-function state and not something an
	// instance can choose: ordinary marks first, additive over them. The array is built alpha-then-
	// additive above, so those two groups are already contiguous.
	//
	// Measured on dbab04 with a plasma load: 43 marks were 21 draws, and are now 2. It matters
	// because this renderer is CPU-bound -- roughly 4 ms of CPU against 1.7 ms of GPU -- so a draw
	// call is spent from the scarce budget and the pixels the box shades are spent from the spare
	// one. The trade only goes one way.
	const unsigned alphaCount = (instAlpha.Size() > all.Size()) ? all.Size() : instAlpha.Size();
	for (int pass = 0; pass < 2; pass++)
	{
		const bool additive = (pass == 1);
		const unsigned first = additive ? alphaCount : 0;
		const unsigned count = additive ? (all.Size() - alphaCount) : alphaCount;
		if (count == 0) continue;

		Diligent::IPipelineState *pso = additive ? g_ddAddPSO.RawPtr() : g_ddPSO.RawPtr();
		Diligent::IShaderResourceBinding *srb = additive ? g_ddAddSRB.RawPtr() : g_ddSRB.RawPtr();
		if (!pso || !srb) continue;

		auto *vTexVar = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex");
		if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSceneDepth")) v->Set(depthSRV);
		if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSceneNormal")) v->Set(normalSRV);
		ctx->SetPipelineState(pso);

		// [rc4l] One draw per RUN of marks that share a graphic.
		//
		// The instances are already grouped: they were appended in the order the slots were assigned,
		// so marks with the same texture sit together and a run is found by walking until the slot
		// changes. Rebinding the sampler between runs is what every other pass in this backend does,
		// and it is the thing the array was meant to avoid -- but a mark drawn with the right graphic
		// and one extra draw call beats a mark drawn with the wrong one.
		unsigned i = first;
		while (i < first + count)
		{
			const int slot = (int)all[i].centre[3];
			unsigned run = 1;
			while (i + run < first + count && (int)all[i + run].centre[3] == slot) run++;

			if (vTexVar)
				vTexVar->Set((slot >= 0 && (unsigned)slot < views.Size()) ? views[slot] : fallback);
			ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

			Diligent::DrawAttribs draw;
			draw.NumVertices = 6;
			draw.NumInstances = run;
			draw.StartVertexLocation = 0;
			draw.FirstInstanceLocation = i;
			draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(draw);
			g_ddDrawn++;
			i += run;
		}
	}


	if (rtv) ctx->SetRenderTargets(1, &rtv, SceneDepthDSV(),
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

}} // namespace zx::hwrender
