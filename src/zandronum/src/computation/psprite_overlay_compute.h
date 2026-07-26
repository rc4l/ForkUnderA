// [MGOOOOOO] Pure decision/offset math for the DECORATE A_Overlay family, extracted from
// p_pspr.cpp / the GL weapon renderer so it is unit-testable without the engine.
// Implementation in psprite_overlay_compute.cpp.
#ifndef ZX_PSPRITE_OVERLAY_COMPUTE_H
#define ZX_PSPRITE_OVERLAY_COMPUTE_H

#include <climits>
#include <cstdint>

// [MGOOOOOO] Canonical engine-reserved psprite layer ids. These MUST match the ps_* enum
// in p_pspr.h; p_pspr.cpp static_asserts the equality so the two never drift.
static const int ZX_PSP_WEAPON       = 1;
static const int ZX_PSP_FLASH        = 1000;
static const int ZX_PSP_TARGETCENTER = INT_MAX - 2;
static const int ZX_PSP_TARGETLEFT   = INT_MAX - 1;
static const int ZX_PSP_TARGETRIGHT  = INT_MAX;

// [MGOOOOOO] WOF_* offset/scale flag bits (must match the WOF_ enum in p_pspr.h).
static const int ZX_WOF_KEEPX       = 1 << 0;
static const int ZX_WOF_KEEPY       = 1 << 1;
static const int ZX_WOF_ADD         = 1 << 2;
static const int ZX_WOF_INTERPOLATE = 1 << 3;

// [MGOOOOOO] PSPF_* bits needed by the pure draw-offset math (must match the PSPF_ enum).
static const int ZX_PSPF_ADDWEAPON = 1 << 0;
static const int ZX_PSPF_ADDBOB    = 1 << 1;

// [MGOOOOOO] True for one of the five engine-reserved (non-overlay) layers.
bool ComputeIsReservedPSpriteLayer(int layer);

// [MGOOOOOO] Index at which newLayer should be inserted into an ascending-sorted array of
// layer ids to keep it sorted. `layers` holds `count` ascending ids; newLayer is assumed
// absent. Returns 0..count.
unsigned int ComputeSortedInsertIndex(const int *layers, unsigned int count, int newLayer);

// [MGOOOOOO] Whether A_ClearOverlays(start, stop, safety) should remove the given layer.
// start==stop==0 means "all"; safety keeps the reserved layers.
bool ComputeClearOverlayShouldRemove(int layer, int start, int stop, bool safety);

// [MGOOOOOO] Apply A_OverlayFlags: set (OR) or clear (AND NOT) the given bits on `existing`.
unsigned int ComputeOverlayFlags(unsigned int existing, unsigned int flags, bool set);

// [MGOOOOOO] Result of applying one axis of A_OverlayOffset/A_OverlayScale. `keep` leaves
// the value untouched; `add` adds to it; otherwise it is replaced. `oldValue` is the
// current value, `newValue` the argument. Values are raw 48.16 fixed_t, so int64_t.
int64_t ComputeOverlayAxis(int64_t oldValue, int64_t newValue, bool keep, bool add);

// [MGOOOOOO] A_OverlayScale: a scaleY of 0 means "square" (copy scaleX). Raw fixed_t values.
int64_t ComputeOverlaySquareScaleY(int64_t scaleX, int64_t scaleY);

// [MGOOOOOO] Additional (x, y) to add to a layer's own offset when drawing. `ridesBob` is
// true for the reserved weapon/flash layers, which always follow the bob and ignore flags;
// otherwise the weapon offset and/or bob are added per PSPF_ADDWEAPON / PSPF_ADDBOB. Offsets
// are raw 48.16 fixed_t (int64_t) so a large map coordinate cannot truncate.
void ComputeOverlayDrawOffset(bool ridesBob, int flags, int64_t weaponX, int64_t weaponY,
	int64_t bobX, int64_t bobY, int64_t *outAddX, int64_t *outAddY);

#endif // ZX_PSPRITE_OVERLAY_COMPUTE_H
