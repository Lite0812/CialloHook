#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Rut
{
	namespace HookX
	{
		void ConfigureCustomPakVFS(bool enable, const wchar_t* const* pakPaths, size_t pakCount, bool enableLog);
		bool ResolveCustomPakVFSPath(const wchar_t* originalPath, std::wstring& resolvedPath);
		bool ResolveCustomPakVFSData(const wchar_t* originalPath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData);
		bool ResolveCustomPakArchiveData(const wchar_t* archivePath, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData);
		bool ResolveCustomPakArchiveDataEx(const wchar_t* archivePath, const std::vector<std::wstring>& nestedArchives, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData);
		bool ResolveCustomPakVFSFileSize(const wchar_t* originalPath, uint64_t& outSize);
		bool IsCustomPakArchivePath(const wchar_t* path);
		bool IsCustomPakInternalIoActive();
	}
}
