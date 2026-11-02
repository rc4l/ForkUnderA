// [Doruk-Aksoy] How much room the ACS string pool gains after a collection, so a pool that stays nearly full is not collected again a few strings later.
#ifndef ZX_STRINGPOOL_COMPUTE_H
#define ZX_STRINGPOOL_COMPUTE_H

namespace zx { namespace acsstrings {

// [Doruk-Aksoy] Entries to add to the room of a pool of size entries after a collection freed freed of them: size when under a quarter came free, so the next collection waits for that many new strings, else none.
unsigned ComputeStringPoolGrowth(unsigned freed, unsigned size);

}} // namespace zx::acsstrings

#endif
