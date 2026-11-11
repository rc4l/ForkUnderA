#include "features/database/computation/database_path_compute.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace zx
{

std::string ComputeDatabaseStripVerbatimPrefix(const std::string &path)
{
	static const std::string unc = "\\\\?\\UNC\\", local = "\\\\?\\";
	if (path.compare(0, unc.size(), unc) == 0)
		return "\\\\" + path.substr(unc.size());
	if (path.compare(0, local.size(), local) == 0)
		return path.substr(local.size());
	return path;
}

#ifdef _WIN32

namespace
{

std::wstring wide(const std::string &text)
{
	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	std::wstring out(size > 0 ? size : 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &out[0], size);
	out.resize(out.size() - 1);
	return out;
}

std::string narrow(const std::wstring &text)
{
	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string out(size > 0 ? size : 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &out[0], size, nullptr, nullptr);
	out.resize(out.size() - 1);
	return out;
}

}

std::string ComputeDatabaseResolvedPath(const std::string &path)
{
	// [Doruk-Aksoy] SQLite reads file names as UTF-8, so the path is resolved as the file SQLite opened.
	const HANDLE file = CreateFileW(wide(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return path;
	// [Doruk-Aksoy] Asked for its length first, so a path past MAX_PATH needs no second try.
	const DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
	const DWORD needed = GetFinalPathNameByHandleW(file, nullptr, 0, flags);
	std::wstring resolved(needed + 1, L'\0');
	const DWORD length = GetFinalPathNameByHandleW(file, &resolved[0], needed + 1, flags);
	CloseHandle(file);
	resolved.resize(length <= needed ? length : 0);
	return resolved.empty() ? path : ComputeDatabaseStripVerbatimPrefix(narrow(resolved));
}

#else

std::string ComputeDatabaseResolvedPath(const std::string &path)
{
	char *resolved = realpath(path.c_str(), nullptr);
	if (resolved == nullptr)
		return path;
	const std::string out = resolved;
	free(resolved);
	return out;
}

#endif

}
