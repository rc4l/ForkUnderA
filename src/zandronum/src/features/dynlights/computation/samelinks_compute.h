// [Doruk-Aksoy] Whether a light's relink would change any of its links, from what its walk reached and what its last relink linked.
#ifndef ZX_SAMELINKS_COMPUTE_H
#define ZX_SAMELINKS_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace dynlights {

// [Doruk-Aksoy] Whether the walk reached what the last relink linked, the same targets in the same order (linkOf naming a reached one's target), so every node is in place and none would go.
template <class Reached, class LinkOf>
bool ComputeSameLinks(const std::vector<const void *> &linked, const std::vector<Reached> &reached, LinkOf linkOf)
{
	if (linked.size() != reached.size()) return false;
	for (size_t i = 0; i < linked.size(); i++)
		if (linked[i] != linkOf(reached[i])) return false;
	return true;
}

// [Doruk-Aksoy] The targets a relink linked, in walk order, kept for the next one to compare against.
template <class Reached, class LinkOf>
void ComputeKeepLinks(const std::vector<Reached> &reached, LinkOf linkOf, std::vector<const void *> &linked)
{
	linked.resize(reached.size());
	for (size_t i = 0; i < reached.size(); i++) linked[i] = linkOf(reached[i]);
}

}} // namespace zx::dynlights

#endif
