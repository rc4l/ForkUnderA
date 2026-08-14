// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Pure, engine-free core of the GPU render profiler (gl.timers RPC). The bridge brackets each
// render pass with GL_TIMESTAMP timer queries; everything that is arithmetic rather than a GL call --
// which ring slot a frame writes, when a slot is safe to read back without stalling, turning raw
// nanosecond timestamps into milliseconds, deciding whether a driver's timer results are real, and
// assembling the report JSON -- lives here so it is unit-tested off-engine at 100% coverage. The thin
// GL glue (mcp_glperf.cpp) only issues the queries and reads them back.
//
// Why a profiler at all: on a laggy map you first need to know WHERE the GPU time goes -- opaque world
// geometry, the translucent/overdraw pass (smoke, blended sprites -- the usual fill-rate killer), or
// the 2D/HUD -- before you can fix it. The CPU sampler (fuactl sample) can't see GPU-side cost; these
// queries can.

#ifndef ZX_GLPERF_COMPUTE_H
#define ZX_GLPERF_COMPUTE_H

#include <string>
#include <vector>
#include <cstdint>

namespace zx { namespace mcp {

// The GPU render zones we time, in wire order. SCENE = opaque BSP (walls/flats/sprites); TRANSLUCENT =
// the blended/additive pass; PORTALS and POSTPROCESS are reserved (enumerated now so the wire format
// is stable, instrumented later); HUD2D = the status bar / menu / console 2D draw. Keep GLZoneName in
// sync with this order.
enum GLZone
{
	GLZONE_SCENE = 0,
	GLZONE_TRANSLUCENT,
	GLZONE_PORTALS,
	GLZONE_POSTPROCESS,
	GLZONE_HUD2D,
	GLZONE_COUNT
};

// Stable lowercase JSON key for a zone id; "?" for an out-of-range id so a bad index can never corrupt
// the object's key set.
const char *GLZoneName( int zone );

// --- Query ring bookkeeping ------------------------------------------------
// One set of query objects per in-flight frame, cycled through `ringSize` sets. A frame's results are
// read back `ringSize` frames later, by which point the GPU has certainly finished them -- so the read
// never stalls the CPU on the pipeline (the whole point of the ring).

// Which ring slot frame `frameIndex` writes into. ringSize <= 0 is treated as 1 (single slot).
int GLRingSlot( uint64_t frameIndex, int ringSize );

// True once enough frames have elapsed that the slot being (re)claimed holds a fully-issued past frame
// safe to read. Before this, the slot has never been written and must not be read back.
bool GLRingReady( uint64_t frameIndex, int ringSize );

// --- Timestamp arithmetic --------------------------------------------------
// GL_TIMESTAMP results are uint64 nanoseconds on the GPU clock.

double GLNanosToMs( uint64_t ns );

// A span is valid only if both endpoints were actually recorded (nonzero) and time did not run
// backwards (end >= begin). A zero endpoint means "this marker was never issued this frame"; a
// reversed pair means a wrapped/garbage counter -- either way the span is not a real measurement.
bool GLSpanValid( uint64_t beginNs, uint64_t endNs );

// Milliseconds for a span, or 0.0 when the span is invalid (never a negative or nonsense number).
double GLSpanMs( uint64_t beginNs, uint64_t endNs );

// --- Timing availability verdict -------------------------------------------
// Some GL drivers (notably legacy Apple OpenGL) accept timer queries but return all-zero results. We
// must never report fabricated numbers, so after a capture we judge the per-frame total-GPU samples:
// no samples, or every sample effectively zero, means the timers are not usable on this driver.
struct GLTimingVerdict
{
	bool        available; // true only if at least one frame measured real (>0) GPU time
	std::string note;      // empty when available; otherwise why timing is unusable
};

GLTimingVerdict GLAssessTiming( const std::vector<double> &totalsMs );

// --- Report assembly -------------------------------------------------------
// Build the gl.timers report object. perZoneMs is indexed by GLZone (a vector of per-frame ms samples
// for each zone; a zone with no samples is omitted). totalMs is the per-frame whole-GPU-frame vector.
// countersJson, if non-empty, is a already-formed JSON object ("{...}") of last-frame draw counters and
// is embedded verbatim under "counters". Percentile/1%-low stats reuse SummarizeFrameTimes so the
// numbers line up with perf.capture. When the verdict is unavailable the zones/total are still emitted
// (as whatever was measured) but "available":false and "note" tell the caller not to trust them.
std::string GLTimersJson( const GLTimingVerdict &verdict,
                          const std::vector<std::vector<double>> &perZoneMs,
                          const std::vector<double> &totalMs,
                          const std::string &countersJson );

}} // namespace zx::mcp

#endif
