// [rc4l] Implementation of the crash title/fingerprint decisions. See crash_symbolize_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "crash_symbolize_compute.h"

#include <cctype>

namespace zx { namespace crashreport {

namespace {

// How many innermost resolved frames make up the grouping fingerprint. Enough to separate distinct
// crash sites, few enough that unrelated deeper noise doesn't split recurrences of the same crash.
const size_t kFingerprintDepth = 5;

bool IsDigits(const std::string &s)
{
	if (s.empty()) return false;
	for (char c : s)
		if (!std::isxdigit((unsigned char)c) && c != 'x') return false;
	return true;
}

} // namespace

std::string NormalizeFrameName(const std::string &raw)
{
	// Trim leading/trailing whitespace.
	size_t b = 0, e = raw.size();
	while (b < e && std::isspace((unsigned char)raw[b])) ++b;
	while (e > b && std::isspace((unsigned char)raw[e - 1])) --e;
	std::string s = raw.substr(b, e - b);

	// Strip a trailing "+ <offset>" (backtrace_symbols / dladdr style): "func + 3024", "func + 0x8".
	size_t plus = s.rfind(" + ");
	if (plus != std::string::npos)
	{
		std::string tail = s.substr(plus + 3);
		if (IsDigits(tail))
			s = s.substr(0, plus);
	}

	// Re-trim in case stripping left trailing space.
	e = s.size();
	while (e > 0 && std::isspace((unsigned char)s[e - 1])) --e;
	return s.substr(0, e);
}

CrashIdentity ComputeCrashIdentity(const std::vector<ResolvedFrame> &framesOutermostFirst)
{
	CrashIdentity id;

	// Walk innermost-first (crash site is the deepest frame).
	std::vector<const ResolvedFrame *> inner;
	inner.reserve(framesOutermostFirst.size());
	for (auto it = framesOutermostFirst.rbegin(); it != framesOutermostFirst.rend(); ++it)
		inner.push_back(&*it);

	// Title: the deepest MAIN-MODULE frame if any resolved (the app code responsible), else the
	// deepest resolved frame of any module (e.g. a driver function), else a stable no-symbols label.
	const ResolvedFrame *titleFrame = nullptr;
	for (const ResolvedFrame *f : inner)
		if (f->mainModule && !NormalizeFrameName(f->func).empty()) { titleFrame = f; break; }
	if (titleFrame == nullptr)
		for (const ResolvedFrame *f : inner)
			if (!NormalizeFrameName(f->func).empty()) { titleFrame = f; break; }

	if (titleFrame != nullptr)
		id.title = NormalizeFrameName(titleFrame->func);
	else
		id.title = "crash (" + std::to_string(framesOutermostFirst.size()) + " frames, no symbols)";

	// Fingerprint: the innermost few resolved names joined. Same crash site -> same fingerprint;
	// distinct sites -> distinct. If nothing resolved, group by frame count so at least the
	// truly-opaque crashes don't all collapse into one another arbitrarily.
	std::string fp;
	size_t used = 0;
	for (const ResolvedFrame *f : inner)
	{
		std::string n = NormalizeFrameName(f->func);
		if (n.empty()) continue;
		if (!fp.empty()) fp += ";";
		fp += n;
		if (++used >= kFingerprintDepth) break;
	}
	id.fingerprint = fp.empty() ? ("nosym:" + std::to_string(framesOutermostFirst.size())) : fp;

	return id;
}

}} // namespace zx::crashreport
