#ifndef ZX_DATABASE_PATH_COMPUTE_H
#define ZX_DATABASE_PATH_COMPUTE_H

#include <string>

namespace zx
{

// [Doruk-Aksoy] The real path of an existing file, symlinks followed and (on Windows) 8.3 short names expanded; the path as given when it cannot be resolved.
std::string ComputeDatabaseResolvedPath(const std::string &path);

// [Doruk-Aksoy] A Windows final path without its \\?\ prefix, \\?\UNC\server\share becoming \\server\share.
std::string ComputeDatabaseStripVerbatimPrefix(const std::string &path);

}

#endif
