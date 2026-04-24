#include "LocaleEmulatorSupport.h"

#include "../io/File.h"
#include "Hook_API.h"
#include "../io/CustomPakVFS.h"

#include <memory>

using namespace Rut::FileX;

namespace Rut
{
	namespace HookX
	{
		static bool IsAbsolutePath(const std::wstring& path)
		{
			if (path.size() >= 2 && path[1] == L':')
			{
				return true;
			}
			if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
			{
				return true;
			}
			return false;
		}

		static std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
		{
			if (left.empty())
			{
				return right;
			}
			if (right.empty())
			{
				return left;
			}
			wchar_t last = left.back();
			if (last == L'\\' || last == L'/')
			{
				return left + right;
			}
			return left + L"\\" + right;
		}

		static std::wstring GetDirectoryPath(const std::wstring& path)
		{
			if (path.empty())
			{
				return L"";
			}
			size_t pos = path.find_last_of(L"\\/");
			if (pos == std::wstring::npos)
			{
				return L"";
			}
			return path.substr(0, pos);
		}

		static bool IsSamePath(const std::wstring& left, const std::wstring& right)
		{
			if (left.empty() || right.empty())
			{
				return false;
			}
			return _wcsicmp(left.c_str(), right.c_str()) == 0;
		}

		static std::wstring ToBaseAbsolutePath(const std::wstring& baseDir, const std::wstring& path)
		{
			if (path.empty())
			{
				return L"";
			}
			if (IsAbsolutePath(path))
			{
				return path;
			}
			return JoinPath(baseDir, path);
		}

		static bool IsFileExists(const std::wstring& path)
		{
			if (path.empty())
			{
				return false;
			}
			DWORD attr = GetFileAttributesW(path.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		static std::vector<std::wstring> BuildLocaleEmulatorDependencyCandidates(const wchar_t* fileName)
		{
			std::vector<std::wstring> candidates;
			if (!fileName || fileName[0] == L'\0')
			{
				return candidates;
			}
			candidates.emplace_back(fileName);
			candidates.emplace_back(std::wstring(L"dll\\") + fileName);
			candidates.emplace_back(std::wstring(L"dlls\\") + fileName);
			candidates.emplace_back(std::wstring(L"LocaleEmulator\\") + fileName);
			candidates.emplace_back(std::wstring(L"locale\\") + fileName);
			return candidates;
		}

		static bool TryResolveLocaleEmulatorDependencyFromPatchFolders(
			const LocaleEmulatorLoaderOptions& options,
			const wchar_t* fileName,
			std::wstring& resolvedPath,
			bool& foundCandidate)
		{
			resolvedPath.clear();
			foundCandidate = false;
			if (!options.patchFolders || options.patchFolders->empty() || !fileName || fileName[0] == L'\0')
			{
				return false;
			}

			std::vector<std::wstring> candidates = BuildLocaleEmulatorDependencyCandidates(fileName);
			for (size_t i = options.patchFolders->size(); i > 0; --i)
			{
				const std::wstring& folder = (*options.patchFolders)[i - 1];
				if (folder.empty())
				{
					continue;
				}
				std::wstring absFolder = ToBaseAbsolutePath(options.patchBaseDir, folder);
				for (const std::wstring& candidate : candidates)
				{
					std::wstring absCandidate = JoinPath(absFolder, candidate);
					if (IsFileExists(absCandidate))
					{
						foundCandidate = true;
						resolvedPath = absCandidate;
						return true;
					}
				}
			}
			return false;
		}

		bool CopyFileToRuntimeTemp(
			const std::wstring& sourcePath,
			const std::wstring& tempDir,
			const wchar_t* logPrefix,
			std::wstring& copiedPath)
		{
			copiedPath.clear();
			if (sourcePath.empty() || tempDir.empty())
			{
				return false;
			}

			std::wstring fileName = PathGetFileName(sourcePath);
			if (fileName.empty())
			{
				return false;
			}

			std::wstring targetPath = JoinPath(tempDir, fileName);
			if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE))
			{
				LogMessage(LogLevel::Error, L"%s extraction failed: cannot copy %s to %s (error=%u)",
					logPrefix ? logPrefix : L"LocaleEmulator", sourcePath.c_str(), targetPath.c_str(), GetLastError());
				return false;
			}

			copiedPath = targetPath;
			return true;
		}

		bool ExtractCustomPakFileToTemp(
			const std::wstring& filePath,
			const std::wstring& tempDir,
			const wchar_t* logPrefix,
			std::wstring& extractedPath,
			bool& foundCandidate)
		{
			extractedPath.clear();
			foundCandidate = false;
			if (tempDir.empty())
			{
				return false;
			}
			std::shared_ptr<const std::vector<uint8_t>> data;
			if (!ResolveCustomPakVFSData(filePath.c_str(), data) || !data || data->empty())
			{
				return false;
			}
			foundCandidate = true;
			std::wstring fileName = PathGetFileName(filePath);
			if (fileName.empty())
			{
				return false;
			}
			std::wstring targetPath = JoinPath(tempDir, fileName);
			HANDLE hFile = CreateFileW(targetPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				LogMessage(LogLevel::Error, L"%s extraction failed: cannot create %s (error=%u)",
					logPrefix ? logPrefix : L"LocaleEmulator", targetPath.c_str(), GetLastError());
				return false;
			}
			DWORD writeSize = static_cast<DWORD>(data->size());
			DWORD written = 0;
			BOOL writeOk = WriteFile(hFile, data->data(), writeSize, &written, nullptr);
			CloseHandle(hFile);
			if (!writeOk || written != writeSize)
			{
				DeleteFileW(targetPath.c_str());
				LogMessage(LogLevel::Error, L"%s extraction failed: write error for %s",
					logPrefix ? logPrefix : L"LocaleEmulator", targetPath.c_str());
				return false;
			}
			extractedPath = targetPath;
			return true;
		}

		void AppendUniquePath(std::vector<std::wstring>& paths, const std::wstring& path)
		{
			if (path.empty())
			{
				return;
			}
			for (const std::wstring& existing : paths)
			{
				if (_wcsicmp(existing.c_str(), path.c_str()) == 0)
				{
					return;
				}
			}
			paths.push_back(path);
		}

		void CleanupPreparedRuntimeFiles(const std::vector<std::wstring>& paths)
		{
			std::wstring firstDir;
			for (const std::wstring& path : paths)
			{
				if (firstDir.empty())
				{
					firstDir = PathRemoveFileName(path);
				}
				SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(path.c_str());
			}
			if (!firstDir.empty())
			{
				RemoveDirectoryTreeIfEmpty(firstDir.c_str(), 4);
			}
		}

		bool PrepareLocaleEmulatorDependency(
			const LocaleEmulatorLoaderOptions& options,
			const wchar_t* fileName,
			std::wstring& preparedPath,
			bool& fromCustomPak,
			bool& foundCandidate)
		{
			preparedPath.clear();
			fromCustomPak = false;
			foundCandidate = false;
			if (!fileName || fileName[0] == L'\0')
			{
				return false;
			}

			bool patchFolderCandidateFound = false;
			std::wstring patchFolderPath;
			if (TryResolveLocaleEmulatorDependencyFromPatchFolders(options, fileName, patchFolderPath, patchFolderCandidateFound))
			{
				foundCandidate = true;
				if (!options.tempDir.empty())
				{
					if (CopyFileToRuntimeTemp(patchFolderPath, options.tempDir, options.logPrefix, preparedPath))
					{
						return true;
					}
					LogMessage(LogLevel::Error, L"%s extraction failed: runtime copy failed for %s",
						options.logPrefix ? options.logPrefix : L"LocaleEmulator", patchFolderPath.c_str());
					return false;
				}
				preparedPath = patchFolderPath;
				return true;
			}

			std::vector<std::wstring> candidates = BuildLocaleEmulatorDependencyCandidates(fileName);
			for (const std::wstring& candidate : candidates)
			{
				bool currentFoundCandidate = false;
				if (ExtractCustomPakFileToTemp(candidate, options.tempDir, options.logPrefix, preparedPath, currentFoundCandidate))
				{
					fromCustomPak = true;
					foundCandidate = true;
					return true;
				}
				foundCandidate = foundCandidate || currentFoundCandidate;
			}
			foundCandidate = foundCandidate || patchFolderCandidateFound;
			if (foundCandidate && options.tempDir.empty())
			{
				LogMessage(LogLevel::Error, L"%s extraction failed: runtime cache dir unavailable for %s",
					options.logPrefix ? options.logPrefix : L"LocaleEmulator", fileName);
			}
			return false;
		}

		HMODULE LoadLocaleEmulatorLoaderModule(
			const LocaleEmulatorLoaderOptions& options,
			bool& usedConfiguredOverride,
			bool& usedCustomPak,
			bool& foundConfiguredCandidate,
			std::vector<std::wstring>& preparedPaths,
			std::vector<std::wstring>* runtimeSearchDirs)
		{
			usedConfiguredOverride = false;
			usedCustomPak = false;
			foundConfiguredCandidate = false;
			preparedPaths.clear();

			std::wstring localeDllPath;
			bool localeFromCustomPak = false;
			bool localeCandidateFound = false;
			if (PrepareLocaleEmulatorDependency(options, L"LocaleEmulator.dll", localeDllPath, localeFromCustomPak, localeCandidateFound))
			{
				if (runtimeSearchDirs)
				{
					AppendUniquePath(*runtimeSearchDirs, GetDirectoryPath(localeDllPath));
				}
				if (localeFromCustomPak)
				{
					AppendUniquePath(preparedPaths, localeDllPath);
					usedCustomPak = true;
				}
				else if (IsSamePath(GetDirectoryPath(localeDllPath), options.tempDir))
				{
					AppendUniquePath(preparedPaths, localeDllPath);
				}
				else
				{
					usedConfiguredOverride = true;
				}
			}

			std::wstring loaderPath;
			bool loaderFromCustomPak = false;
			bool loaderCandidateFound = false;
			if (PrepareLocaleEmulatorDependency(options, L"LoaderDll.dll", loaderPath, loaderFromCustomPak, loaderCandidateFound))
			{
				if (runtimeSearchDirs)
				{
					AppendUniquePath(*runtimeSearchDirs, GetDirectoryPath(loaderPath));
				}
				if (loaderFromCustomPak)
				{
					AppendUniquePath(preparedPaths, loaderPath);
					usedCustomPak = true;
				}
				else if (IsSamePath(GetDirectoryPath(loaderPath), options.tempDir))
				{
					AppendUniquePath(preparedPaths, loaderPath);
				}
				else
				{
					usedConfiguredOverride = true;
				}
				foundConfiguredCandidate = localeCandidateFound || loaderCandidateFound;
				HMODULE module = LoadLibraryExW(loaderPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
				if (!module)
				{
					CleanupPreparedRuntimeFiles(preparedPaths);
					preparedPaths.clear();
				}
				return module;
			}

			foundConfiguredCandidate = localeCandidateFound || loaderCandidateFound;
			HMODULE loaderDll = LoadLibraryW(L"LoaderDll.dll");
			if (loaderDll)
			{
				if (runtimeSearchDirs)
				{
					wchar_t loaderPath[MAX_PATH] = {};
					if (GetModuleFileNameW(loaderDll, loaderPath, MAX_PATH))
					{
						AppendUniquePath(*runtimeSearchDirs, GetDirectoryPath(loaderPath));
					}
				}
				return loaderDll;
			}

			CleanupPreparedRuntimeFiles(preparedPaths);
			preparedPaths.clear();
			return nullptr;
		}
	}
}
