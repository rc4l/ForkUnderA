// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/versionrelation_compute.h"

#include <cctype>
#include <vector>

namespace zx
{

namespace
{

// [rc4l] The dotted numbers at the front of a build string, plus whether anything followed them.
struct ParsedVersion
{
	std::vector<int> parts;
	bool hasSuffix;		// commits past the tag, i.e. "-29-gde55d35"
	bool ok;

	ParsedVersion() : hasSuffix(false), ok(false) {}
};

// Reads "v0.2.19", "0.2.19", "v0.2.19-29-gde55d35" and "v0.2.19 on windows" alike. Anything with no
// leading number at all is a refusal, because a version we cannot read must not compare equal to one
// we can -- that is how an unreadable string would silently become joinable.
ParsedVersion ParseVersion(const std::string &text)
{
	ParsedVersion out;

	size_t i = 0;
	while ((i < text.size()) && std::isspace(static_cast<unsigned char>(text[i])))
		++i;

	// The leading 'v' is conventional, not required.
	if ((i < text.size()) && ((text[i] == 'v') || (text[i] == 'V')))
		++i;

	for (;;)
	{
		const size_t digitsBegin = i;
		int value = 0;

		while ((i < text.size()) && std::isdigit(static_cast<unsigned char>(text[i])))
		{
			// [rc4l] Saturate rather than overflow. A version component big enough to wrap is
			// nonsense from a remote host, and wrapping would make it compare SMALLER than ours --
			// turning a garbage string into a server that merely looks out of date.
			if (value < 1000000)
				value = (value * 10) + (text[i] - '0');
			++i;
		}

		if (i == digitsBegin)
			return out;		// no digits where a component belongs

		out.parts.push_back(value);

		if ((i < text.size()) && (text[i] == '.'))
		{
			++i;
			continue;
		}
		break;
	}

	// Anything at all after the numbers means this build is past its tag. A space counts: the string
	// a server sends carries trailing text, and that text is not a suffix in the describe sense, so
	// only '-' is read as "commits since".
	out.hasSuffix = ((i < text.size()) && (text[i] == '-'));
	out.ok = true;
	return out;
}

int ComponentAt(const ParsedVersion &v, size_t index)
{
	// Missing components are zero, so "v0.2" and "v0.2.0" are one version rather than two.
	return (index < v.parts.size()) ? v.parts[index] : 0;
}

} // namespace

VersionRelation CompareFuaVersions(const std::string &theirs, const std::string &ours)
{
	const ParsedVersion a = ParseVersion(theirs);
	const ParsedVersion b = ParseVersion(ours);

	if (!a.ok || !b.ok)
		return VersionRelation::Unknown;

	const size_t count = (a.parts.size() > b.parts.size()) ? a.parts.size() : b.parts.size();
	for (size_t i = 0; i < count; ++i)
	{
		const int x = ComponentAt(a, i);
		const int y = ComponentAt(b, i);
		if (x < y)
			return VersionRelation::Older;
		if (x > y)
			return VersionRelation::Newer;
	}

	// [rc4l] Equal numbers, so the suffix decides. A build some commits past v0.2.19 is genuinely
	// ahead of v0.2.19 itself, and two builds both past the same tag are not comparable from here --
	// the describe string does not say which came first, so they are treated as the same version,
	// which is what the join check has always assumed.
	if (a.hasSuffix && !b.hasSuffix)
		return VersionRelation::Newer;
	if (!a.hasSuffix && b.hasSuffix)
		return VersionRelation::Older;

	return VersionRelation::Same;
}

bool VersionRelationCanJoin(VersionRelation relation)
{
	return (relation == VersionRelation::Same);
}

bool VersionRelationSinks(VersionRelation relation)
{
	// Older and Unknown both mean "nothing the player does changes this", which is what sinking is
	// for. Newer is reachable by updating, so it keeps its place.
	return (relation == VersionRelation::Older) || (relation == VersionRelation::Unknown);
}

} // namespace zx
