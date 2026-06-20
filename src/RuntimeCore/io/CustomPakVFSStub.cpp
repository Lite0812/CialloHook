#include "CustomPakVFS.h"

namespace Rut
{
	namespace HookX
	{
		void ConfigureCustomPakVFS(bool enable, const wchar_t* const* pakPaths, size_t pakCount, bool enableLog)
		{
			(void)enable;
			(void)pakPaths;
			(void)pakCount;
			(void)enableLog;
		}

		bool ResolveCustomPakVFSPath(const wchar_t* originalPath, std::wstring& resolvedPath)
		{
			(void)originalPath;
			resolvedPath.clear();
			return false;
		}

		bool ResolveCustomPakVFSData(const wchar_t* originalPath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			(void)originalPath;
			resolvedData.reset();
			return false;
		}

		bool ResolveCustomPakArchiveData(const wchar_t* archivePath, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			(void)archivePath;
			(void)relativePath;
			resolvedData.reset();
			return false;
		}

		bool ResolveCustomPakArchiveDataEx(const wchar_t* archivePath, const std::vector<std::wstring>& nestedArchives, const wchar_t* relativePath, std::shared_ptr<const std::vector<uint8_t>>& resolvedData)
		{
			(void)archivePath;
			(void)nestedArchives;
			(void)relativePath;
			resolvedData.reset();
			return false;
		}

		bool ResolveCustomPakVFSFileSize(const wchar_t* originalPath, uint64_t& outSize)
		{
			(void)originalPath;
			outSize = 0;
			return false;
		}

		bool QueryCustomPakVFSPathInfo(const wchar_t* originalPath, bool& isDirectory, uint64_t& outSize)
		{
			(void)originalPath;
			isDirectory = false;
			outSize = 0;
			return false;
		}

		bool EnumerateCustomPakVFSDirectory(const wchar_t* directoryPath, std::vector<CustomPakVFSDirectoryEntry>& outEntries)
		{
			(void)directoryPath;
			outEntries.clear();
			return false;
		}

		bool IsCustomPakArchivePath(const wchar_t* path)
		{
			(void)path;
			return false;
		}

		bool IsCustomPakInternalIoActive()
		{
			return false;
		}
	}
}
