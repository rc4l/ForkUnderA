// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-library/computation/pathdisplay_compute.h"

namespace zx
{

const char *const kPathPendingText = "Looking...";
const char *const kPathMissingText = "Not on this machine";

namespace
{

bool IsSeparator(char c)
{
	// Both, because a preset saved on Windows is readable here and the path in it keeps its slashes.
	return (c == '/') || (c == '\\');
}

int WidthOf(const std::string &text, PathMeasureFn measure, void *ctx)
{
	return measure(text.c_str(), ctx);
}

// [rc4l] The path as its components, each carrying the separator that introduces it.
//
// Keeping the separator on the FRONT of the component it belongs to is what makes a wrapped line
// read as a continuation: "/wads/doom" is obviously part of something, where "wads/doom" reads as a
// relative path that was never in the string.
std::vector<std::string> Components(const std::string &path)
{
	std::vector<std::string> out;

	for (size_t i = 0; i < path.size(); )
	{
		size_t end = i + 1;
		while ((end < path.size()) && !IsSeparator(path[end]))
			++end;

		out.push_back(path.substr(i, end - i));
		i = end;
	}

	return out;
}

// One component that will not fit a line of its own, split by character across as many as it takes.
void PushSplit(std::vector<std::string> &out, const std::string &piece, int maxWidth,
               PathMeasureFn measure, void *ctx)
{
	std::string line;

	for (size_t i = 0; i < piece.size(); ++i)
	{
		const std::string grown = line + piece[i];

		// Never emit an empty line: a single character wider than the cap still has to go somewhere,
		// and the alternative is a loop that makes no progress.
		if (!line.empty() && (WidthOf(grown, measure, ctx) > maxWidth))
		{
			out.push_back(line);
			line.clear();
		}

		line += piece[i];
	}

	if (!line.empty())
		out.push_back(line);
}

} // namespace

std::vector<std::string> ComputeWrappedPath(const std::string &path, int maxWidth,
                                            PathMeasureFn measure, void *ctx)
{
	std::vector<std::string> out;

	if (path.empty() || (measure == NULL))
		return out;

	// Nothing could ever fit, so wrapping would split forever. Hand it back whole and let the caller
	// overflow visibly rather than hang.
	if (maxWidth <= 0)
	{
		out.push_back(path);
		return out;
	}

	const std::vector<std::string> parts = Components(path);
	std::string line;

	for (size_t i = 0; i < parts.size(); ++i)
	{
		const std::string grown = line + parts[i];

		if (line.empty() || (WidthOf(grown, measure, ctx) <= maxWidth))
		{
			// A component that does not fit even alone is the split case; anything else joins.
			if (line.empty() && (WidthOf(parts[i], measure, ctx) > maxWidth))
			{
				PushSplit(out, parts[i], maxWidth, measure, ctx);
				line.clear();

				// The tail of the split is the line to keep growing, so it does not sit alone while
				// the next component starts a fresh one.
				if (!out.empty())
				{
					line = out.back();
					out.pop_back();
				}

				continue;
			}

			line = grown;
			continue;
		}

		out.push_back(line);
		line = parts[i];
	}

	if (!line.empty())
		out.push_back(line);

	return out;
}

std::string ComputePathTip(const std::string &name, PathTipState state, const std::string &path,
                           int maxWidth, PathMeasureFn measure, void *ctx)
{
	std::string out = name;

	switch (state)
	{
	case PathTipState::Pending:
		out += "\n";
		out += kPathPendingText;
		return out;

	case PathTipState::Missing:
		out += "\n";
		out += kPathMissingText;
		return out;

	default:
		break;
	}

	// [rc4l] Found, but with nothing to show: a resolver that reports success and hands back no path
	// is a bug elsewhere, and a tooltip that silently says only the name would hide it. Say the same
	// thing the missing case says, because from here the two are indistinguishable.
	const std::vector<std::string> lines = ComputeWrappedPath(path, maxWidth, measure, ctx);
	if (lines.empty())
	{
		out += "\n";
		out += kPathMissingText;
		return out;
	}

	for (size_t i = 0; i < lines.size(); ++i)
	{
		out += "\n";
		out += lines[i];
	}

	return out;
}

} // namespace zx
