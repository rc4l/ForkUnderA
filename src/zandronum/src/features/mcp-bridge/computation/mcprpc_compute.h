// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The rock-solid, engine-free core of the native MCP "programmable engine" bridge: NDJSON
// request framing, minimal JSON field extraction, the deterministic state-hash mixer, and sim-step
// planning. Everything here is pure -- no engine types, no sockets -- so it is unit-tested off-engine
// and the engine-facing glue (mcp_rpc.cpp / mcp_bridge.cpp) stays a thin shell over tested logic.
//
// Wire protocol v2 (NDJSON, one JSON object per line over loopback TCP):
//   request  : {"id":<int>,"cmd":"<name>","args":{...}}          client -> engine
//   response : {"id":<int>,"ok":true,"result":{...}}             engine -> client (success)
//              {"id":<int>,"ok":false,"error":"<msg>"}           engine -> client (failure)
//   event    : {"t":"event","event":"<name>","data":{...}}       engine -> client (unsolicited)
//   hello    : {"t":"hello",...}                                 engine -> client (on connect)

#ifndef ZX_MCPRPC_COMPUTE_H
#define ZX_MCPRPC_COMPUTE_H

#include <string>
#include <vector>
#include <cstdint>

namespace zx { namespace mcp {

// ---- Request parsing -------------------------------------------------------

struct RpcRequest
{
	bool        valid;   // false if the line was not a well-formed v2 request
	long        id;      // correlation id (-1 if absent)
	std::string cmd;     // command name
	std::string args;    // the raw JSON substring of the "args" object ("{}" if absent)
};

// Parse one NDJSON line into a request. A line with no "cmd" is not a request (valid=false).
RpcRequest ParseRequest(const std::string &line);

// ---- Minimal JSON field extraction (flat objects; values are int/string) ---

// Extract an integer field ("key":N, N may be negative) from a flat JSON object substring.
bool GetInt(const std::string &obj, const char *key, long &out);

// Extract a floating-point field ("key":N.N, sign + optional fraction). Integers parse too.
bool GetFloat(const std::string &obj, const char *key, double &out);

// Convert a view rotation in degrees to the integer G_AddViewAngle/G_AddViewPitch expect (1 unit =
// 360/65536 deg). Rounds to the nearest BAM step; exact for angles dividing 360 (135 -> 24576).
long DegreesToViewUnits(double deg);

// Extract a string field ("key":"...") with standard escape decoding.
bool GetStr(const std::string &obj, const char *key, std::string &out);

// ---- Framing (all inputs are already-formed JSON fragments) ----------------

// JSON-escape a raw string into out (CR dropped, control chars -> \uXXXX).
void JsonEscape(const std::string &in, std::string &out);

// {"id":<id>,"ok":true,"result":<bodyJson>}  (bodyJson must be a complete JSON value)
std::string BuildOkResponse(long id, const std::string &bodyJson);

// {"id":<id>,"ok":false,"error":"<message>"}  (message is escaped)
std::string BuildErrResponse(long id, const std::string &message);

// {"t":"event","event":"<name>","data":<dataJson>}
std::string BuildEvent(const std::string &name, const std::string &dataJson);

// ---- Deterministic state hash (FNV-1a/64) ----------------------------------
// A stable fingerprint of sim state, mixed field by field in a fixed order. Two engine instances at
// the same level.time with identical simulation produce the same value; the first differing tic is a
// desync. Kept here so the mixing is testable and identical across every consumer.

uint64_t FnvInit();
uint64_t FnvMixU64(uint64_t h, uint64_t v);
uint64_t FnvMixStr(uint64_t h, const std::string &s);

// ---- Sim-step planning -----------------------------------------------------

// Advancing the world by `tics` means unpausing until level.time reaches this target, then refreezing.
// tics <= 0 is clamped to 1 (a step always advances at least one tic).
long StepTarget(long currentLevelTime, int tics);

// True once the world clock has reached (or passed) the planned target.
bool StepComplete(long currentLevelTime, long targetLevelTime);

// ---- Performance summary ---------------------------------------------------
// Frametime statistics done the way the industry reports them: percentiles + the "1% low" (the mean
// of the worst 1% of frames, expressed as FPS) rather than a single average FPS, because a stutter --
// exactly what a laggy weapon effect causes -- shows up in the tail, not the mean. Pure, so it is
// unit-tested and identical for every consumer.
struct PerfSummary
{
	int    n;
	double mean, min, max, p50, p95, p99; // frametimes in ms
	double fpsAvg;                          // 1000 / mean
	double fps1pctLow;                      // 1000 / (mean of the worst 1% frametimes)
};

PerfSummary SummarizeFrameTimes(const std::vector<double> &frameMs);

// Serialize a PerfSummary to a compact JSON object (values fixed to 3 decimals).
std::string PerfSummaryJson(const PerfSummary &s);

}} // namespace zx::mcp

#endif
