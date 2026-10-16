// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/web/computation/crashcontext_compute.h"
#include "features/crashreport/computation/crash_report_compute.h"
#include "features/json/computation/json_compute.h"

namespace zx { namespace web {

using zx::json::ComputeJsonQuoted;

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
	for (const std::string &f : c.files) files += (files.empty() ? "" : ",") + ComputeJsonQuoted(f);
	for (const auto &t : c.tags) tags += (tags.empty() ? "" : ",") + ComputeJsonQuoted(t.first) + ":" + ComputeJsonQuoted(t.second);
	return "{\"release\":" + ComputeJsonQuoted(c.release) + ",\"dist\":" + ComputeJsonQuoted(c.dist) + ",\"dsn\":" + ComputeJsonQuoted(c.dsn)
		+ ",\"reporting\":" + (c.reporting ? "true" : "false") + ",\"map\":" + ComputeJsonQuoted(c.map)
		+ ",\"iwad\":" + ComputeJsonQuoted(c.files.size() > 1 ? c.files[1] : "")
		+ ",\"files\":[" + files + "],\"tags\":{" + tags + "}}";
}

}} // namespace zx::web
