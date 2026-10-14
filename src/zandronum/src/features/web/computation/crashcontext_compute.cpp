// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/web/computation/crashcontext_compute.h"
#include "features/crashreport/computation/crash_report_compute.h"
#include "features/mcp-bridge/computation/mcprpc_compute.h"

namespace zx { namespace web {

// [rc4l] A JSON string literal holding text.
static std::string Quoted(const std::string &text)
{
	std::string escaped;
	zx::mcp::JsonEscape(text, escaped);
	return "\"" + escaped + "\"";
}

std::string ComputeCrashDsn(bool official, const char *dsn)
{
	return official && dsn != nullptr ? dsn : "";
}

std::vector<std::string> ComputeCrashFiles(const std::vector<std::string> &paths)
{
	std::vector<std::string> names;
	for (const std::string &path : paths) names.push_back(zx::ComputeSafeFileLabel(path));
	return names;
}

void ComputeSetCrashTag(CrashContext &context, const std::string &key, const std::string &value)
{
	for (auto &tag : context.tags)
		if (tag.first == key) { tag.second = value; return; }
	context.tags.emplace_back(key, value);
}

std::string ComputeCrashContextJson(const CrashContext &c)
{
	std::string files, tags;
	for (const std::string &f : c.files) files += (files.empty() ? "" : ",") + Quoted(f);
	for (const auto &t : c.tags) tags += (tags.empty() ? "" : ",") + Quoted(t.first) + ":" + Quoted(t.second);
	return "{\"release\":" + Quoted(c.release) + ",\"dist\":" + Quoted(c.dist) + ",\"dsn\":" + Quoted(c.dsn)
		+ ",\"reporting\":" + (c.reporting ? "true" : "false") + ",\"map\":" + Quoted(c.map)
		+ ",\"iwad\":" + Quoted(c.files.size() > 1 ? c.files[1] : "")
		+ ",\"files\":[" + files + "],\"tags\":{" + tags + "}}";
}

}} // namespace zx::web
