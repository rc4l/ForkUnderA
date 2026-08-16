// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Deriving an outdoor light colour from a sky texture.
//
// The honest framing: what a renderer actually wants from a sky is IRRADIANCE, which depends on the
// surface normal, and the industry answer is spherical harmonics (L2, nine coefficients) or an
// ambient cube. A single averaged colour is the L0 term of that expansion -- the zeroth order.
//
// We take the zeroth order on purpose, because Doom's lighting model cannot express anything more:
// a sector carries ONE light colour, applied to floor, ceiling, walls and sprites alike, with no
// normals anywhere. Given that ceiling, the useful work is not a better basis, it is choosing which
// pixels to average and how:
//
//   - average in LINEAR light, not in sRGB bytes. Summing gamma-encoded values is the wrong mean
//     and biases dark; this is a correctness fix, not a taste one.
//   - average the horizon band, the part of the sky a player actually looks at.
//   - clamp saturation, so a violently coloured sky cannot turn the whole level into a filter.
//
// [rc4l] This used to offer more choices than it could justify. A dominant-colour mode and a
// zenith-weighted alternative were both removed after measuring them: the two weightings landed
// 0.19 and 0.21 apart out of 255 on two of three skies tested, which is noise, and the dominant mode
// scored buckets by pixel count, so a sky that was a quarter near-black returned near-black, which
// NormaliseBrightness turns into white -- the no-tint value. The feature switched itself off on
// several maps and no slider could bring it back. What is left is the behaviour the defaults always
// had.
//
// All of it is pure so the arithmetic can be tested without a renderer, a level, or a GPU.

#ifndef ZX_SKYTINT_COMPUTE_H
#define ZX_SKYTINT_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx
{

// 8-bit sRGB, the form pixels arrive in and tints leave in.
struct SkyRgb
{
	int r, g, b;

	SkyRgb();
	SkyRgb(int r, int g, int b);
	bool operator==(const SkyRgb &other) const;
};

// sRGB byte <-> linear light. The whole reason the averaging is done twice over.
double LinearFromSrgb(int byte);
int SrgbFromLinear(double linear);

// Weight for one row of the sky texture, row 0 at the top. The lower half only: that is the band a
// player looks at. Never negative.
double RowWeight(int row, int height);

// [rc4l] Lay one sky layer over another, the way a double sky is drawn: LEVEL_DOUBLESKY puts
// sky1texture in front of sky2texture and lets sky1's transparency show the back layer through. The
// colour on screen is the composite, so that is what has to be averaged -- taking either layer alone
// gives a colour nobody ever sees.
//
// Composited in LINEAR light, for the same reason everything else here is: `over` at half alpha
// should land halfway in light, not halfway in the gamma encoding.
//
// The layers need not share dimensions. `under` is sampled proportionally, so a 256x128 back layer
// under a 512x128 front one lines up rather than tiling by accident.
SkyRgb CompositeOver(SkyRgb over, int overAlpha, SkyRgb under);

std::vector<SkyRgb> CompositeSkyLayers(const std::vector<SkyRgb> &over, const std::vector<int> &overAlpha,
	int overWidth, const std::vector<SkyRgb> &under, int underWidth);

// Reduce a sky image to one colour. `pixels` is row-major, `width` columns per row.
//
// Averaging happens in linear light and the result is re-encoded, so the answer is the colour of
// the light rather than the average of its encoding.
SkyRgb AverageSky(const std::vector<SkyRgb> &pixels, int width);

// Scale so the largest component is 255: keeps hue, discards brightness. The sky says what colour
// the light is; the map's own light levels say how much of it there is.
SkyRgb NormaliseBrightness(SkyRgb colour);

// [rc4l] Rebalance a tint so multiplying by it does not DARKEN what it touches.
//
// The tint is applied as a per-channel multiply, so an orange (255,120,0) halves a surface's green
// and deletes its blue. The surface comes out oranger AND dimmer, and that dimming is what reads as
// a filter laid over the screen rather than as light falling on the level -- the whole "everything
// looks like it is behind coloured glass" complaint.
//
// This scales the colour so its Rec.709 luminance is 1.0: the channels the sky is short of stay
// down, and the ones it is strong in go UP to pay for them, clamping at 255. The hue shift survives,
// the brightness loss does not. Note the compensation is capped by the clamp, so a very saturated
// tint still loses some light; it just loses far less than an uncorrected multiply.
SkyRgb PreserveLuminance(SkyRgb colour);

// [rc4l] A "follow the sky's own brightness" scale used to live here, letting a dim sky tint more
// gently than a blazing one. Removed after measuring it: on Speed of Doom MAP01 and MAP20, the two
// maps it was built to tell apart, it cut the tint by 90% and 86% respectively. It scaled everything
// down without discriminating, which is what the plain Strength dial already does, more legibly.
// StrengthForSectorLight below is the one that actually separates those maps.

// [rc4l] Perceptual colour, and the reason the Strength dial does not mean one thing.
//
// Measured on Speed of Doom: MAP01's sky derives a green tint at 66% saturation, MAP20's derives an
// orange one at 99%. By every sky-side measure MAP20 should dominate. In play MAP01 screams and
// MAP20 does nothing, because Doom's textures are brown and grey:
//
//   green on brown   a long way round the hue circle. The surface becomes a colour it was not.
//   orange on brown  browner brown. 99% saturation buys almost no visible change.
//
// So the quantity a player reacts to is not the multiplier's magnitude, it is the perceptual
// DISTANCE between the surface before and after. CIELAB is the standard space for measuring that:
// distances in it correspond roughly to how different two colours look, which distances in RGB
// emphatically do not.
struct SkyLab
{
	double L, a, b;

	SkyLab();
	SkyLab(double L, double a, double b);
};

SkyLab LabFromSrgb(SkyRgb colour);

// CIE76. Newer formulae (CIE94, CIEDE2000) correct known non-uniformities, mostly around saturated
// blues and near-neutrals, at a good deal more arithmetic. This is choosing a strength for a lighting
// hint, not matching paint, and 76 is monotonic enough for that.
double DeltaE76(SkyLab x, SkyLab y);

// [rc4l] The strength at which this tint moves THIS scene by `targetDelta`, in CIELAB units.
//
// BlendFromWhite is linear in its percentage, and dE is near enough linear in the blend over the
// range that matters, so one evaluation at full strength gives the slope and the answer is a
// division. Returns `maxPct` when the tint cannot reach the target even at full strength, which is
// the common case for a tint whose hue already matches the scene.
//
// This is what makes one Strength setting mean the same thing on two different maps: the dial stops
// being "how much do I multiply" and becomes "how much change do I want to see".
int StrengthForTargetDelta(SkyRgb scene, SkyRgb tint, double targetDelta, int maxPct);

// [rc4l] Strength scaled by how bright THIS SECTOR already is (Doom light level, 0..255).
//
// The sky-side dials could not tell Speed of Doom MAP01 from MAP20: both skies are dark, so both
// were reduced alike. What actually separates them is the scene -- MAP01 is near-neutral dark grey
// so a green cast screams, MAP20 is already warm brown so a warm cast disappears into it. A dim
// sector taking a weaker tint targets that directly, and it is what light does anyway: a room that
// receives little light shows little of its colour.
int StrengthForSectorLight(int pct, int lightLevel, int respectPct);

// Pull toward grey until saturation is at most `maxPct` (0 = grey, 100 = untouched).
SkyRgb ClampSaturation(SkyRgb colour, int maxPct);

// Saturation as a percentage, by the HSV definition: (max - min) / max.
int SaturationPct(SkyRgb colour);

// White blended toward `tint` by `pct`. pct 0 leaves white, so the caller can fade the effect out
// without special-casing "off".
SkyRgb BlendFromWhite(SkyRgb tint, int pct);

// [rc4l] Give every hue the same push, instead of letting green ride free.
//
// Measured on Speed of Doom MAP01, same spot, same scene, only the sky swapped: the native green sky
// moved the ground by 14.0, MAP20's orange by 10.3, MAP29's red by 6.6. More than two to one for a
// difference that is nothing to do with the map.
//
// The cause is the luminance curve. Perceived brightness is 71.5% green, 21% red, 7% blue, so a
// fully saturated GREEN multiply still passes 84% of the light and barely dims anything, while a
// fully saturated red passes 22%. PreserveLuminance then tries to undo that dimming by scaling the
// colour up, which for the saturated hues clamps at 255 and arrives as a faint wash. Green gets a
// free ride and every other hue pays for its own chroma.
//
// So the tax is charged evenly. A multiplier blended `c` of the way from white toward a direction
// whose peak is 255 has channel spread exactly c * (1 - min/255), so solving for a FIXED spread
// gives the same colour shift whatever the hue:
//
//     c = target / (1 - min/255)
//
// A red direction (min ~0) buys its spread nearly one-for-one; a green one (min ~0.33) has to blend
// further to reach the same spread. No scaling, so nothing clamps, and nothing needs undoing after.
//
// The honest cost is that the saturated hues still dim more, because a red sky IS dimmer than a
// green one. That is light behaving like light; what it no longer does is vanish.
//
// `dir` is expected to be NormaliseBrightness'd (peak 255). `chromaPct` is 0..100.
SkyRgb EqualiseHuePush(SkyRgb dir, int chromaPct);

// [rc4l] How much of the sky light a sector the mapper has already coloured should still receive,
// as a percentage.
//
// This used to be a yes/no: ANY non-white sector colour disqualified a sector outright, so it got no
// tint and did not light its neighbours either. The cliff that rule creates is the problem. Measured
// on Eon Collection aeon13, where the whole level carries a single faint [254,194,194] wash: 158 of
// 180 sky-seeing spots were excluded by a colour barely visible on screen, and the feature simply
// switched itself off across an entire mapset.
//
// A mapper who lights a room hard red means it. One who lays a whisper of atmosphere over everything
// is not vetoing sunlight. Scaling by how strongly the sector is coloured says both of those with
// one rule and no threshold to argue about: the faint wash keeps most of its sky light, a saturated
// room keeps almost none, and nothing jumps at the boundary because there is no boundary.
int SkyShareForSectorColour(SkyRgb sectorColour);

// [rc4l] How much a sky this dark deserves to be believed, as a percentage.
//
// NormaliseBrightness throws brightness away on purpose -- the sky says WHICH colour the light is,
// the map's light levels say how much. That holds for a real sky and breaks down completely for a
// nearly black one, where the few surviving bits are as much noise as signal and get multiplied up
// into a confident, fully saturated colour.
//
// Measured on Eon Collection aeon13, whose second skybox sampled as [6,0,2]: scaled by 42x that
// becomes a vivid red, and sectors lit by it wore a hue present nowhere in the actual image, right
// beside sectors carrying the real warm sky. Same failure as a mostly-black GvH sky normalising to
// white and switching the tint off entirely.
//
// So a sky is trusted in proportion to how much light it actually emits, fading to nothing as it
// approaches black. Above the knee a normal sky is unaffected, which is the point: this must not
// quietly dim skies that were working.
int SkyConfidenceForBrightness(SkyRgb rawSky);

// [rc4l] Propagation is by DISTANCE, not by sector count.
//
// It used to halve per sector crossed, which made the reach depend on how finely the mapper chopped
// their geometry: two hops crosses a whole room in a blocky map and dies inside one doorway's trim
// in a detailed one, so the setting meant something different on every map. Distance is invariant
// to that -- a room cut into forty detail sectors is the same number of map units across as the
// same room built as one -- so the control finally means one thing everywhere.

// Strength at `distance` map units from open sky, reaching nothing at `reach`. Falls off quickly at
// first and then trails, which is roughly how a doorway lights a room and, more practically, avoids
// the banding a per-sector step produced across adjacent trim.
int StrengthAtDistance(int pct, double distance, double reach);

// How much of the light an opening passes: 1.0 for a full-height gap, less for a slit. Without this
// a 4-unit crack under a door lets through exactly as much as an archway.
double OpeningFactor(double openingHeight, double fullHeight);

// What one step through an opening costs, in effective map units. A narrow gap makes the light
// travel FURTHER rather than stopping it outright, which keeps the falloff smooth and means a
// tight opening and a long corridor are expressed in the same currency.
double StepCost(double distance, double openingFactor);

} // namespace zx

#endif
