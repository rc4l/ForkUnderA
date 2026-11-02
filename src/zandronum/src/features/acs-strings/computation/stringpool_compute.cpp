// [Doruk-Aksoy] The ACS string pool's growth after a collection.
#include "features/acs-strings/computation/stringpool_compute.h"

namespace zx { namespace acsstrings {

unsigned ComputeStringPoolGrowth(unsigned freed, unsigned size)
{
	return (unsigned long long)freed * 4 < size ? size : 0;
}

}} // namespace zx::acsstrings
