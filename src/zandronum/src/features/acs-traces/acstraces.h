// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSTRACES_H
#define ZX_ACSTRACES_H

#include <cstdint>

#include "features/acs-traces/computation/traces_compute.h"

class AActor;
class FArchive;

namespace zx { namespace acstraces {

// [Doruk-Aksoy] fua_TracerCreate: a new tracer of the calling script, 0 when it already holds 16 (warned once a map).
int32_t TracerCreate(ScriptTracers &tracers);
void TracerFree(ScriptTracers &tracers, int32_t id);
void TracerClear(ScriptTracers &tracers, int32_t id);

// [Doruk-Aksoy] fua_LineTrace, ZScript's Actor.LineTrace in ACS units, from handle `h` or the activator for 0; returns the hit type.
int LineTrace(ScriptTracers &tracers, int32_t id, AActor *activator, int32_t h, int32_t angle, int32_t distance, int32_t pitch,
	int flags, int32_t offsetZ, int32_t offsetForward, int32_t offsetSide);

// [Doruk-Aksoy] fua_TraceRule with `count` arguments after its action, `name` the filter's string for class, flag and texture filters.
bool TraceRule(ScriptTracers &tracers, int32_t id, int kind, int action, int count, int filter, int32_t a, int32_t b, const char *name);

// [Doruk-Aksoy] fua_TraceCallback: the function a TRACE_Callback rule hands hits to, called with the tracer and `context`; 0 clears it.
bool TraceCallback(ScriptTracers &tracers, int32_t id, int32_t function, int32_t context);

// [Doruk-Aksoy] fua_Trace: LineTracer.Trace from a map point along a direction, each hit decided by the tracer's rules and callback, run as `activator`; returns how many hits they kept.
int Trace(ScriptTracers &tracers, int32_t id, AActor *activator, int32_t x, int32_t y, int32_t z, int32_t dx, int32_t dy, int32_t dz, int32_t distance, int flags, int32_t ignore);

int TraceHitCount(const ScriptTracers &tracers, int32_t id);

// [Doruk-Aksoy] fua_TraceResult: a TR_ property of hit `index`, or of the last result for -1; an actor that went reads 0.
int32_t TraceResult(const ScriptTracers &tracers, int32_t id, int prop, int index);

// [Doruk-Aksoy] fua_TraceResultStr: the hit's texture name, "" for none or another property.
const char *TraceResultStr(const ScriptTracers &tracers, int32_t id, int prop, int index);

// [Doruk-Aksoy] fua_LineInfo and fua_SectorInfo: a property of a line or sector by index, 0 for an unknown one.
int32_t LineInfo(int line, int prop);
int32_t SectorInfo(int sector, int prop);

// [Doruk-Aksoy] From DLevelScript::Serialize: the script's tracers, hits included.
void SerializeScript(FArchive &arc, ScriptTracers &tracers);

// [Doruk-Aksoy] A level was entered, so the full-script warning may show again.
void EnterLevel();

}} // namespace zx::acstraces

#endif
