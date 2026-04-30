		//*********START File Hot-Patch*********
		static std::vector<std::wstring> sg_vecPatchFolders = { L"patch" };
		static std::vector<std::wstring> sg_vecSpoofFiles;
		static std::vector<std::wstring> sg_vecSpoofDirectories;
		struct DirectoryRedirectRule
		{
			std::wstring sourceDirectory;
			std::wstring sourceDirectoryLower;
			std::wstring targetDirectory;
		};
		static std::vector<DirectoryRedirectRule> sg_vecDirectoryRedirectRules;
		static std::wstring sg_wsGameDir;
		static std::wstring sg_wsGameDirLower;
		static bool sg_bEnableLog = false;
		static bool sg_bEnableSpoofLog = false;
		static bool sg_bEnableDirectoryRedirectLog = false;
		static bool sg_bCustomPakLog = false;
		static int sg_customPakReadMode = 1;
		static std::wstring sg_customPakCacheDir;
		static bool sg_customPakCacheCleanupDone = false;
		static pCreateFileA_File rawCreateFileA_Patch = CreateFileA;
		static pCreateFileW_File rawCreateFileW_Patch = CreateFileW;
		static bool BuildRelativePath(const wchar_t* originalPath, std::wstring& relativePath);
		static bool ShouldBypassFileHook()
		{
			return IsHookRuntimeShuttingDown();
		}
		static bool PreferDiskBackedCustomPak()
		{
			if (IsHookRuntimeShuttingDown())
			{
				return true;
			}
			return sg_customPakReadMode == 0;
		}

		static std::wstring NormalizeFileHookSlashes(std::wstring value)
		{
			for (wchar_t& c : value)
			{
				if (c == L'/')
				{
					c = L'\\';
				}
			}
			return value;
		}

		static std::wstring ToLowerCopy(std::wstring value)
		{
			for (wchar_t& c : value)
			{
				c = (wchar_t)towlower(c);
			}
			return value;
		}

		static bool EndsWithInsensitive(const std::wstring& value, const wchar_t* suffix)
		{
			if (!suffix)
			{
				return false;
			}
			size_t suffixLen = wcslen(suffix);
			if (value.size() < suffixLen)
			{
				return false;
			}
			return _wcsicmp(value.c_str() + value.size() - suffixLen, suffix) == 0;
		}

		static std::wstring NormalizeRelativePath(std::wstring value)
		{
			value = NormalizeFileHookSlashes(value);
			while (!value.empty() && (value[0] == L'.' || value[0] == L'\\'))
			{
				value.erase(value.begin());
			}
			while (!value.empty() && (value.back() == L'\\'))
			{
				value.pop_back();
			}
			return ToLowerCopy(value);
		}

		static std::wstring GetDirectoryPathNoSlash(const std::wstring& value)
		{
			if (value.empty())
			{
				return L"";
			}
			size_t pos = value.find_last_of(L"\\/");
			if (pos == std::wstring::npos)
			{
				return L"";
			}
			return value.substr(0, pos);
		}

		static std::wstring NormalizePatchFolder(std::wstring value)
		{
			value = NormalizeFileHookSlashes(value);
			while (!value.empty() && (value[0] == L'.' || value[0] == L'\\'))
			{
				value.erase(value.begin());
			}
			while (!value.empty() && value.back() == L'\\')
			{
				value.pop_back();
			}
			return value;
		}

		static std::wstring TrimTrailingPathSlashes(std::wstring value)
		{
			value = NormalizeFileHookSlashes(value);
			while (value.size() > 1 && value.back() == L'\\')
			{
				if (value.size() == 3 && value[1] == L':')
				{
					break;
				}
				value.pop_back();
			}
			return value;
		}

		static bool StartsWithNoCase(const std::wstring& textLower, const std::wstring& prefixLower)
		{
			if (prefixLower.empty() || textLower.size() < prefixLower.size())
			{
				return false;
			}
			return _wcsnicmp(textLower.c_str(), prefixLower.c_str(), prefixLower.size()) == 0;
		}

		static void RefreshGameDir()
		{
			wchar_t exePath[MAX_PATH] = {};
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			sg_wsGameDir = exePath;
			size_t pos = sg_wsGameDir.find_last_of(L"\\");
			if (pos != std::wstring::npos)
			{
				sg_wsGameDir = sg_wsGameDir.substr(0, pos + 1);
			}
			sg_wsGameDir = NormalizeFileHookSlashes(sg_wsGameDir);
			sg_wsGameDirLower = ToLowerCopy(sg_wsGameDir);
		}

		static bool BuildRelativePath(const wchar_t* originalPath, std::wstring& relativePath)
		{
			relativePath.clear();
			if (!originalPath)
			{
				return false;
			}

			std::wstring source = NormalizeFileHookSlashes(originalPath);
			if (source.empty())
			{
				return false;
			}

			bool isAbs = false;
			if (source.size() >= 2 && source[1] == L':')
			{
				isAbs = true;
			}
			else if (source.size() >= 2 && source[0] == L'\\' && source[1] == L'\\')
			{
				isAbs = true;
			}

			if (!isAbs)
			{
				relativePath = source;
			}
			else
			{
				std::wstring sourceLower = ToLowerCopy(source);
				if (!StartsWithNoCase(sourceLower, sg_wsGameDirLower))
				{
					return false;
				}
				relativePath = source.substr(sg_wsGameDir.size());
			}

			while (!relativePath.empty() && (relativePath[0] == L'.' || relativePath[0] == L'\\'))
			{
				relativePath.erase(relativePath.begin());
			}
			return !relativePath.empty();
		}

		static std::vector<std::wstring> BuildPatchCandidates(const wchar_t* originalPath)
		{
			std::vector<std::wstring> result;
			std::wstring relativePath;
			if (!BuildRelativePath(originalPath, relativePath))
			{
				return result;
			}
			std::wstring relativeLower = ToLowerCopy(NormalizeFileHookSlashes(relativePath));

			for (size_t i = sg_vecPatchFolders.size(); i > 0; --i)
			{
				const std::wstring& folder = sg_vecPatchFolders[i - 1];
				if (folder.empty())
				{
					continue;
				}
				std::wstring folderLower = ToLowerCopy(NormalizeFileHookSlashes(folder));
				std::wstring folderPrefix = folderLower + L"\\";
				if (relativeLower == folderLower || StartsWithNoCase(relativeLower, folderPrefix))
				{
					continue;
				}
				result.push_back(sg_wsGameDir + folder + L"\\" + relativePath);
			}
			return result;
		}

		static bool IsRegularFileExists(const std::wstring& path)
		{
			if (path.empty())
			{
				return false;
			}
			WIN32_FIND_DATAW findData = {};
			HANDLE hFind = FindFirstFileW(path.c_str(), &findData);
			if (hFind == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			FindClose(hFind);
			return (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		static bool IsAbsPath(const std::wstring& path)
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

		static bool TryGetEnvironmentVariableValue(const wchar_t* name, std::wstring& valueOut)
		{
			valueOut.clear();
			if (!name || name[0] == L'\0')
			{
				return false;
			}
			DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
			if (required == 0)
			{
				return false;
			}
			valueOut.resize(required - 1);
			return GetEnvironmentVariableW(name, &valueOut[0], required) != 0;
		}

		static bool TryGetKnownFolderValue(const KNOWNFOLDERID& folderId, std::wstring& valueOut)
		{
			valueOut.clear();
			PWSTR path = nullptr;
			HRESULT hr = SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &path);
			if (FAILED(hr) || !path || path[0] == L'\0')
			{
				if (path)
				{
					CoTaskMemFree(path);
				}
				return false;
			}
			valueOut.assign(path);
			CoTaskMemFree(path);
			return true;
		}

		static bool TryGetDirectoryRedirectVariableValue(const std::wstring& variableName, std::wstring& valueOut)
		{
			std::wstring upperName = variableName;
			for (wchar_t& c : upperName)
			{
				c = (wchar_t)towupper(c);
			}
			if (upperName == L"SAVEDGAMES")
			{
				return TryGetKnownFolderValue(FOLDERID_SavedGames, valueOut);
			}
			if (upperName == L"DOCUMENTS")
			{
				return TryGetKnownFolderValue(FOLDERID_Documents, valueOut);
			}
			return TryGetEnvironmentVariableValue(upperName.c_str(), valueOut);
		}

		static std::wstring ExpandDirectoryRedirectPath(std::wstring value)
		{
			if (value.empty())
			{
				return value;
			}

			size_t start = 0;
			while (start < value.size())
			{
				size_t open = value.find(L'%', start);
				if (open == std::wstring::npos)
				{
					break;
				}
				size_t close = value.find(L'%', open + 1);
				if (close == std::wstring::npos)
				{
					break;
				}

				std::wstring variableName = value.substr(open + 1, close - open - 1);
				std::wstring variableValue;
				if (TryGetDirectoryRedirectVariableValue(variableName, variableValue))
				{
					value.replace(open, close - open + 1, variableValue);
					start = open + variableValue.size();
				}
				else
				{
					start = close + 1;
				}
			}

			DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
			if (required != 0)
			{
				std::wstring expanded(required - 1, L'\0');
				if (ExpandEnvironmentStringsW(value.c_str(), &expanded[0], required) != 0)
				{
					value = std::move(expanded);
				}
			}
			return value;
		}

		static std::wstring NormalizeDirectoryRedirectDirectory(std::wstring value)
		{
			value = ExpandDirectoryRedirectPath(value);
			value = TrimTrailingPathSlashes(value);
			if (!IsAbsPath(value))
			{
				value = TrimTrailingPathSlashes(sg_wsGameDir + value);
			}
			return NormalizeFileHookSlashes(value);
		}

		static bool EnsureDirectoryChainForRedirect(const std::wstring& path)
		{
			std::wstring normalized = TrimTrailingPathSlashes(path);
			if (normalized.empty())
			{
				return false;
			}
			DWORD attr = GetFileAttributesW(normalized.c_str());
			if (attr != INVALID_FILE_ATTRIBUTES)
			{
				return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
			}

			size_t pos = normalized.find_last_of(L'\\');
			if (pos != std::wstring::npos && pos > 0)
			{
				std::wstring parent = normalized.substr(0, pos);
				if (!parent.empty() && parent != normalized && !EnsureDirectoryChainForRedirect(parent))
				{
					return false;
				}
			}

			if (CreateDirectoryW(normalized.c_str(), nullptr))
			{
				return true;
			}
			if (GetLastError() != ERROR_ALREADY_EXISTS)
			{
				return false;
			}
			attr = GetFileAttributesW(normalized.c_str());
			return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		static void EnsureRedirectTargetParentDirectory(const std::wstring& filePath)
		{
			size_t pos = filePath.find_last_of(L'\\');
			if (pos == std::wstring::npos || pos == 0)
			{
				return;
			}
			EnsureDirectoryChainForRedirect(filePath.substr(0, pos));
		}

		static bool ResolveDirectoryRedirectPath(const wchar_t* originalPath, std::wstring& redirectedPath)
		{
			redirectedPath.clear();
			if (!originalPath || originalPath[0] == L'\0' || sg_vecDirectoryRedirectRules.empty())
			{
				return false;
			}

			std::wstring normalizedOriginal = NormalizeFileHookSlashes(originalPath);
			if (normalizedOriginal.empty())
			{
				return false;
			}

			std::wstring absoluteOriginal = IsAbsPath(normalizedOriginal) ? normalizedOriginal : (sg_wsGameDir + normalizedOriginal);
			absoluteOriginal = NormalizeFileHookSlashes(absoluteOriginal);
			std::wstring absoluteOriginalLower = ToLowerCopy(absoluteOriginal);

			for (const DirectoryRedirectRule& rule : sg_vecDirectoryRedirectRules)
			{
				if (rule.sourceDirectoryLower.empty() || rule.targetDirectory.empty())
				{
					continue;
				}

				bool matched = false;
				std::wstring suffix;
				if (absoluteOriginalLower == rule.sourceDirectoryLower)
				{
					matched = true;
				}
				else if (absoluteOriginalLower.size() > rule.sourceDirectoryLower.size()
					&& _wcsnicmp(absoluteOriginalLower.c_str(), rule.sourceDirectoryLower.c_str(), rule.sourceDirectoryLower.size()) == 0
					&& absoluteOriginalLower[rule.sourceDirectoryLower.size()] == L'\\')
				{
					matched = true;
					suffix = absoluteOriginal.substr(rule.sourceDirectory.size());
				}

				if (!matched)
				{
					continue;
				}

				redirectedPath = rule.targetDirectory;
				if (!suffix.empty())
				{
					if (!redirectedPath.empty() && redirectedPath.back() != L'\\')
					{
						redirectedPath += L'\\';
					}
					if (!suffix.empty() && suffix.front() == L'\\')
					{
						redirectedPath += suffix.substr(1);
					}
					else
					{
						redirectedPath += suffix;
					}
				}

				if (sg_bEnableDirectoryRedirectLog)
				{
					LogMessage(LogLevel::Info, L"[DirectoryRedirect] source=%s redirected=%s", absoluteOriginal.c_str(), redirectedPath.c_str());
				}
				return true;
			}
			return false;
		}

		static bool HasExistingPatchOverride(const wchar_t* originalPath)
		{
			if (!originalPath || originalPath[0] == L'\0')
			{
				return false;
			}

			std::wstring relativePath;
			if (!BuildRelativePath(originalPath, relativePath))
			{
				return false;
			}

			for (const std::wstring& folder : sg_vecPatchFolders)
			{
				if (folder.empty())
				{
					continue;
				}
				std::wstring candidate = sg_wsGameDir + folder + L"\\" + relativePath;
				if (IsRegularFileExists(candidate))
				{
					return true;
				}
			}

			std::wstring source = NormalizeFileHookSlashes(originalPath);
			if (source.empty())
			{
				return false;
			}
			std::wstring absolutePath = IsAbsPath(source) ? source : (sg_wsGameDir + source);
			if (!IsRegularFileExists(absolutePath))
			{
				return false;
			}
			std::wstring absoluteLower = ToLowerCopy(absolutePath);
			for (const std::wstring& folder : sg_vecPatchFolders)
			{
				if (folder.empty())
				{
					continue;
				}
				std::wstring folderPrefix = ToLowerCopy(sg_wsGameDir + folder + L"\\");
				if (StartsWithNoCase(absoluteLower, folderPrefix))
				{
					return true;
				}
			}
			return false;
		}

		static bool IsPathInsidePatchFolder(const wchar_t* originalPath)
		{
			if (!originalPath || originalPath[0] == L'\0')
			{
				return false;
			}
			std::wstring relativePath;
			if (!BuildRelativePath(originalPath, relativePath))
			{
				return false;
			}
			std::wstring relativeLower = ToLowerCopy(NormalizeFileHookSlashes(relativePath));
			for (const std::wstring& folder : sg_vecPatchFolders)
			{
				if (folder.empty())
				{
					continue;
				}
				std::wstring folderLower = ToLowerCopy(NormalizeFileHookSlashes(folder));
				std::wstring folderPrefix = folderLower + L"\\";
				if (relativeLower == folderLower || StartsWithNoCase(relativeLower, folderPrefix))
				{
					return true;
				}
			}
			return false;
		}

		static std::string WideToAnsi(const std::wstring& text)
		{
			if (text.empty())
			{
				return "";
			}
			int len = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, NULL, 0, NULL, NULL);
			if (len <= 0)
			{
				return "";
			}
			std::string result(len - 1, '\0');
			WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, &result[0], len, NULL, NULL);
			return result;
		}

		static bool IsSpoofTarget(const wchar_t* originalPath)
		{
			if (sg_vecSpoofFiles.empty() && sg_vecSpoofDirectories.empty())
			{
				return false;
			}

			std::wstring relativePath;
			if (!BuildRelativePath(originalPath, relativePath))
			{
				return false;
			}

			std::wstring normalized = NormalizeRelativePath(relativePath);
			if (normalized.empty())
			{
				return false;
			}

			for (const std::wstring& spoofFile : sg_vecSpoofFiles)
			{
				if (normalized == spoofFile)
				{
					return true;
				}
			}

			for (const std::wstring& spoofDir : sg_vecSpoofDirectories)
			{
				if (normalized == spoofDir)
				{
					return true;
				}
				if (normalized.size() > spoofDir.size()
					&& _wcsnicmp(normalized.c_str(), spoofDir.c_str(), spoofDir.size()) == 0
					&& normalized[spoofDir.size()] == L'\\')
				{
					return true;
				}
			}
			return false;
		}

		void SetPatchFolder(const wchar_t* folderPath, bool enableLog)
		{
			const wchar_t* singleFolder[1] = { folderPath };
			SetPatchFolders(singleFolder, 1, enableLog);
		}

		void SetPatchFolders(const wchar_t* const* folderPaths, size_t folderCount, bool enableLog)
		{
			RefreshGameDir();
			sg_vecPatchFolders.clear();
			for (size_t i = 0; i < folderCount; ++i)
			{
				const wchar_t* raw = folderPaths ? folderPaths[i] : nullptr;
				if (!raw || raw[0] == L'\0')
				{
					continue;
				}
				std::wstring normalized = NormalizePatchFolder(raw);
				if (!normalized.empty())
				{
					sg_vecPatchFolders.push_back(normalized);
				}
			}

			sg_bEnableLog = enableLog;
			if (sg_bEnableLog)
			{
				LogMessage(LogLevel::Info, L"File patch enabled, game dir: %s", sg_wsGameDir.c_str());
				for (size_t i = 0; i < sg_vecPatchFolders.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"File patch folder[%u]: %s", (uint32_t)i, sg_vecPatchFolders[i].c_str());
				}
			}
		}

		void SetSpoofRules(const wchar_t* const* filePaths, size_t fileCount, const wchar_t* const* directoryPaths, size_t directoryCount, bool enableLog)
		{
			if (sg_wsGameDir.empty())
			{
				RefreshGameDir();
			}

			sg_vecSpoofFiles.clear();
			for (size_t i = 0; i < fileCount; ++i)
			{
				const wchar_t* raw = filePaths ? filePaths[i] : nullptr;
				if (!raw || raw[0] == L'\0')
				{
					continue;
				}
				std::wstring normalized = NormalizeRelativePath(raw);
				if (!normalized.empty())
				{
					sg_vecSpoofFiles.push_back(normalized);
				}
			}

			sg_vecSpoofDirectories.clear();
			for (size_t i = 0; i < directoryCount; ++i)
			{
				const wchar_t* raw = directoryPaths ? directoryPaths[i] : nullptr;
				if (!raw || raw[0] == L'\0')
				{
					continue;
				}
				std::wstring normalized = NormalizeRelativePath(raw);
				if (!normalized.empty())
				{
					sg_vecSpoofDirectories.push_back(normalized);
				}
			}

			sg_bEnableSpoofLog = enableLog;
			if (sg_bEnableSpoofLog)
			{
				LogMessage(LogLevel::Info, L"File spoof enabled, files=%u, directories=%u",
					(uint32_t)sg_vecSpoofFiles.size(), (uint32_t)sg_vecSpoofDirectories.size());
			}
		}

		void SetDirectoryRedirectRules(const wchar_t* const* sourceDirectories, const wchar_t* const* targetDirectories, size_t ruleCount, bool enableLog)
		{
			if (sg_wsGameDir.empty())
			{
				RefreshGameDir();
			}

			sg_vecDirectoryRedirectRules.clear();
			for (size_t i = 0; i < ruleCount; ++i)
			{
				const wchar_t* rawSource = sourceDirectories ? sourceDirectories[i] : nullptr;
				const wchar_t* rawTarget = targetDirectories ? targetDirectories[i] : nullptr;
				if (!rawSource || !rawTarget || rawSource[0] == L'\0' || rawTarget[0] == L'\0')
				{
					continue;
				}

				DirectoryRedirectRule rule = {};
				rule.sourceDirectory = NormalizeDirectoryRedirectDirectory(rawSource);
				rule.targetDirectory = NormalizeDirectoryRedirectDirectory(rawTarget);
				rule.sourceDirectoryLower = ToLowerCopy(rule.sourceDirectory);
				if (!rule.sourceDirectory.empty() && !rule.targetDirectory.empty())
				{
					sg_vecDirectoryRedirectRules.push_back(std::move(rule));
				}
			}

			sg_bEnableDirectoryRedirectLog = enableLog;
			if (sg_bEnableDirectoryRedirectLog)
			{
				LogMessage(LogLevel::Info, L"Directory redirect enabled, rules=%u", (uint32_t)sg_vecDirectoryRedirectRules.size());
				for (size_t i = 0; i < sg_vecDirectoryRedirectRules.size(); ++i)
				{
					LogMessage(LogLevel::Info, L"Directory redirect rule[%u]: %s -> %s",
						(uint32_t)i,
						sg_vecDirectoryRedirectRules[i].sourceDirectory.c_str(),
						sg_vecDirectoryRedirectRules[i].targetDirectory.c_str());
				}
			}
		}

		void SetCustomPakVFS(bool enable, const wchar_t* const* pakPaths, size_t pakCount, bool enableLog)
		{
			sg_bCustomPakLog = enableLog;
			ConfigureCustomPakVFS(enable, pakPaths, pakCount, enableLog);
		}

		void SetCustomPakReadMode(int mode)
		{
			sg_customPakReadMode = (mode == 0) ? 0 : 1;
			if (sg_bCustomPakLog || sg_bEnableLog)
			{
				LogMessage(LogLevel::Info, L"CustomPAK read mode set to %d (%s), effective=%s",
					sg_customPakReadMode,
					sg_customPakReadMode == 0 ? L"disk" : L"memory",
					PreferDiskBackedCustomPak() ? L"disk" : L"memory");
			}
		}

		static uint64_t HashPath64(const std::wstring& text)
		{
			uint64_t hash = 1469598103934665603ull;
			for (wchar_t c : text)
			{
				hash ^= static_cast<uint64_t>(towlower(c));
				hash *= 1099511628211ull;
			}
			return hash;
		}

		struct CacheFileEntry
		{
			std::wstring path;
			uint64_t size = 0;
			uint64_t lastWrite = 0;
		};

		static uint64_t FileTimeToU64(const FILETIME& ft)
		{
			ULARGE_INTEGER v = {};
			v.LowPart = ft.dwLowDateTime;
			v.HighPart = ft.dwHighDateTime;
			return v.QuadPart;
		}

		static uint64_t GetCurrentFileTimeU64()
		{
			FILETIME ft = {};
			GetSystemTimeAsFileTime(&ft);
			return FileTimeToU64(ft);
		}

		static bool DeleteCustomPakCacheFileNow(const std::wstring& filePath)
		{
			if (filePath.empty())
			{
				return false;
			}
			SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
			if (DeleteFileW(filePath.c_str()) != FALSE)
			{
				return true;
			}
			return MoveFileExW(filePath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
		}

		void CleanupCustomPakCacheOnShutdown()
		{
			if (sg_customPakCacheDir.empty())
			{
				return;
			}

			std::wstring cacheDir = sg_customPakCacheDir;
			if (!IsManagedRuntimeCachePath(cacheDir.c_str()))
			{
				if (sg_bCustomPakLog || sg_bEnableLog)
				{
					LogMessage(LogLevel::Warn, L"Skip cache cleanup on shutdown for unsafe path: %s", cacheDir.c_str());
				}
				return;
			}

			sg_customPakCacheDir.clear();
			sg_customPakCacheCleanupDone = false;
			bool removed = false;
			uint32_t fileCount = 0;
			uint32_t removedCount = 0;
			std::wstring pattern = cacheDir;
			if (!pattern.empty() && pattern.back() != L'\\')
			{
				pattern += L"\\";
			}
			pattern += L"*";

			WIN32_FIND_DATAW fd = {};
			HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
			if (hFind != INVALID_HANDLE_VALUE)
			{
				do
				{
					if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					{
						continue;
					}
					std::wstring filePath = cacheDir;
					if (!filePath.empty() && filePath.back() != L'\\')
					{
						filePath += L"\\";
					}
					filePath += fd.cFileName;
					++fileCount;
					bool deleted = DeleteCustomPakCacheFileNow(filePath);
					removed |= deleted;
					if (deleted)
					{
						++removedCount;
					}
				} while (FindNextFileW(hFind, &fd));
				FindClose(hFind);
			}
			SetFileAttributesW(cacheDir.c_str(), FILE_ATTRIBUTE_NORMAL);
			BOOL removedDir = RemoveDirectoryW(cacheDir.c_str());
			RemoveDirectoryTreeIfEmpty(cacheDir.c_str(), 4);
			LogMessage(LogLevel::Info,
				L"CustomPAK cleanup on shutdown: path=%s files=%u removed=%u removed_any=%u dir_removed=%u",
				cacheDir.c_str(), fileCount, removedCount, removed ? 1u : 0u, removedDir ? 1u : 0u);
		}

		static void CleanupCustomPakCacheDirectory(const std::wstring& cacheDir)
		{
			if (cacheDir.empty())
			{
				return;
			}

			const uint64_t retention = 7ull * 24ull * 60ull * 60ull * 10000000ull;
			const uint64_t maxTotalSize = 1024ull * 1024ull * 1024ull;
			const uint64_t targetTotalSize = 768ull * 1024ull * 1024ull;
			uint64_t now = GetCurrentFileTimeU64();
			std::vector<CacheFileEntry> files;
			uint64_t totalSize = 0;

			std::wstring pattern = cacheDir;
			if (!pattern.empty() && pattern.back() != L'\\')
			{
				pattern += L"\\";
			}
			pattern += L"*";

			WIN32_FIND_DATAW fd = {};
			HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
			if (hFind == INVALID_HANDLE_VALUE)
			{
				return;
			}

			do
			{
				if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					continue;
				}
				std::wstring filePath = cacheDir;
				if (!filePath.empty() && filePath.back() != L'\\')
				{
					filePath += L"\\";
				}
				filePath += fd.cFileName;
				uint64_t size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
				uint64_t lastWrite = FileTimeToU64(fd.ftLastWriteTime);
				if (lastWrite > 0 && now > lastWrite && now - lastWrite > retention)
				{
					DeleteFileW(filePath.c_str());
					continue;
				}
				CacheFileEntry entry = {};
				entry.path = std::move(filePath);
				entry.size = size;
				entry.lastWrite = lastWrite;
				totalSize += size;
				files.push_back(std::move(entry));
			} while (FindNextFileW(hFind, &fd));
			FindClose(hFind);

			if (totalSize <= maxTotalSize)
			{
				return;
			}

			std::sort(files.begin(), files.end(), [](const CacheFileEntry& a, const CacheFileEntry& b)
				{
					return a.lastWrite < b.lastWrite;
				});

			for (const CacheFileEntry& entry : files)
			{
				if (totalSize <= targetTotalSize)
				{
					break;
				}
				if (DeleteFileW(entry.path.c_str()))
				{
					if (entry.size <= totalSize)
					{
						totalSize -= entry.size;
					}
					else
					{
						totalSize = 0;
					}
				}
			}
		}

		static std::wstring BuildCustomPakCacheFilePath(const wchar_t* sourcePathW)
		{
			if (sg_customPakCacheDir.empty())
			{
				if (sg_wsGameDir.empty())
				{
					RefreshGameDir();
				}
				std::wstring fallbackBaseDir = sg_wsGameDir;
				while (!fallbackBaseDir.empty() && (fallbackBaseDir.back() == L'\\' || fallbackBaseDir.back() == L'/'))
				{
					fallbackBaseDir.pop_back();
				}
				std::wstring cacheRoot = GetRuntimeCacheDir(fallbackBaseDir.empty() ? nullptr : fallbackBaseDir.c_str(), L"VFSCache");
				if (cacheRoot.empty())
				{
					LogMessage(LogLevel::Error, L"CustomPAK disk cache unavailable: failed to create runtime cache root");
					return L"";
				}
				wchar_t suffix[32] = {};
				_ui64tow_s(HashPath64(sg_wsGameDir), suffix, _countof(suffix), 16);
				sg_customPakCacheDir = cacheRoot + L"\\" + suffix;
			}
			DWORD attr = GetFileAttributesW(sg_customPakCacheDir.c_str());
			if (attr == INVALID_FILE_ATTRIBUTES)
			{
				if (!CreateDirectoryW(sg_customPakCacheDir.c_str(), nullptr))
				{
					attr = GetFileAttributesW(sg_customPakCacheDir.c_str());
					if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
					{
						LogMessage(LogLevel::Error, L"CustomPAK disk cache unavailable: failed to create %s", sg_customPakCacheDir.c_str());
						return L"";
					}
				}
			}
			else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				LogMessage(LogLevel::Error, L"CustomPAK disk cache unavailable: %s is not a directory", sg_customPakCacheDir.c_str());
				return L"";
			}
			if (!sg_customPakCacheCleanupDone)
			{
				CleanupCustomPakCacheDirectory(sg_customPakCacheDir);
				sg_customPakCacheCleanupDone = true;
			}

			std::wstring relativePath;
			if (!BuildRelativePath(sourcePathW, relativePath))
			{
				relativePath = sourcePathW ? sourcePathW : L"";
			}
			relativePath = NormalizeFileHookSlashes(relativePath);
			for (wchar_t& c : relativePath)
			{
				if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
				{
					c = L'_';
				}
			}
			if (relativePath.empty())
			{
				relativePath = L"unnamed";
			}
			if (relativePath.size() > 96)
			{
				relativePath = relativePath.substr(relativePath.size() - 96);
			}
			return sg_customPakCacheDir + L"\\" + relativePath;
		}

		static bool QueryFileSize(const std::wstring& path, uint64_t& sizeOut)
		{
			sizeOut = 0;
			WIN32_FIND_DATAW data = {};
			HANDLE hFind = FindFirstFileW(path.c_str(), &data);
			if (hFind == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			FindClose(hFind);
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				return false;
			}
			sizeOut = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
			return true;
		}

		static bool EnsureCustomPakCacheFile(const wchar_t* sourcePathW, const std::shared_ptr<const std::vector<uint8_t>>& data, std::wstring& cachePathOut)
		{
			cachePathOut.clear();
			if (!data)
			{
				return false;
			}
			std::wstring cachePath = BuildCustomPakCacheFilePath(sourcePathW);
			uint64_t oldSize = 0;
			if (QueryFileSize(cachePath, oldSize) && oldSize == data->size())
			{
				HANDLE hTouch = rawCreateFileW_Patch(cachePath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hTouch != INVALID_HANDLE_VALUE)
				{
					SYSTEMTIME st;
					GetSystemTime(&st);
					FILETIME ft;
					SystemTimeToFileTime(&st, &ft);
					SetFileTime(hTouch, &ft, &ft, &ft);
					CloseHandle(hTouch);
				}
				cachePathOut = cachePath;
				return true;
			}

			HANDLE hFile = rawCreateFileW_Patch(cachePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			bool ok = true;
			if (!data->empty())
			{
				DWORD written = 0;
				ok = WriteFile(hFile, data->data(), static_cast<DWORD>(data->size()), &written, nullptr) && written == data->size();
			}
			SYSTEMTIME st;
			GetSystemTime(&st);
			FILETIME ft;
			SystemTimeToFileTime(&st, &ft);
			SetFileTime(hFile, &ft, &ft, &ft);
			CloseHandle(hFile);
			if (!ok)
			{
				return false;
			}
			cachePathOut = cachePath;
			return true;
		}

		struct MemoryFileState
		{
			std::shared_ptr<const std::vector<uint8_t>> data;
			std::wstring sourcePath;
			uint64_t position;
		};

		static CRITICAL_SECTION sg_memoryFileLock;
		static bool sg_memoryFileLockInitialized = false;
		static std::unordered_map<HANDLE, MemoryFileState> sg_memoryFiles;
		static std::unordered_map<HANDLE, std::wstring> sg_diskCacheFileHandles;
		static uint64_t sg_memoryHandleSeed = 1;

		static bool TryEnsureMemoryFileLock()
		{
			if (sg_memoryFileLockInitialized)
			{
				return true;
			}
			if (IsHookRuntimeShuttingDown())
			{
				return false;
			}
			InitializeCriticalSection(&sg_memoryFileLock);
			sg_memoryFileLockInitialized = true;
			return true;
		}

		static bool TryEnterMemoryFileLock()
		{
			if (!TryEnsureMemoryFileLock())
			{
				return false;
			}
			EnterCriticalSection(&sg_memoryFileLock);
			return true;
		}

		static HANDLE AllocateMemoryFileHandle(const wchar_t* sourcePath, const std::shared_ptr<const std::vector<uint8_t>>& data)
		{
			if (!TryEnterMemoryFileLock())
			{
				SetLastError(ERROR_OPERATION_ABORTED);
				return INVALID_HANDLE_VALUE;
			}
			uint64_t next = sg_memoryHandleSeed++;
			uintptr_t handleValue = (uintptr_t)((next << 1) | 1ull);
			HANDLE handle = reinterpret_cast<HANDLE>(handleValue);
			MemoryFileState state = {};
			state.data = data;
			state.sourcePath = sourcePath ? sourcePath : L"";
			state.position = 0;
			sg_memoryFiles[handle] = state;
			LeaveCriticalSection(&sg_memoryFileLock);
			return handle;
		}

		static bool GetMemoryFileState(HANDLE handle, MemoryFileState& stateOut)
		{
			if (!TryEnterMemoryFileLock())
			{
				return false;
			}
			auto it = sg_memoryFiles.find(handle);
			if (it == sg_memoryFiles.end())
			{
				LeaveCriticalSection(&sg_memoryFileLock);
				return false;
			}
			stateOut = it->second;
			LeaveCriticalSection(&sg_memoryFileLock);
			return true;
		}

		static bool UpdateMemoryFilePosition(HANDLE handle, uint64_t newPosition)
		{
			if (!TryEnterMemoryFileLock())
			{
				return false;
			}
			auto it = sg_memoryFiles.find(handle);
			if (it == sg_memoryFiles.end())
			{
				LeaveCriticalSection(&sg_memoryFileLock);
				return false;
			}
			it->second.position = newPosition;
			LeaveCriticalSection(&sg_memoryFileLock);
			return true;
		}

		static bool RemoveMemoryFileHandle(HANDLE handle)
		{
			if (!TryEnterMemoryFileLock())
			{
				return false;
			}
			size_t erased = sg_memoryFiles.erase(handle);
			LeaveCriticalSection(&sg_memoryFileLock);
			return erased > 0;
		}

		static void RegisterDiskCacheFileHandle(HANDLE handle, const std::wstring& cachePath)
		{
			if (!TryEnterMemoryFileLock())
			{
				return;
			}
			sg_diskCacheFileHandles[handle] = cachePath;
			LeaveCriticalSection(&sg_memoryFileLock);
		}

		static bool PopDiskCacheFileHandle(HANDLE handle, std::wstring& cachePathOut)
		{
			cachePathOut.clear();
			if (!TryEnterMemoryFileLock())
			{
				return false;
			}
			auto it = sg_diskCacheFileHandles.find(handle);
			if (it == sg_diskCacheFileHandles.end())
			{
				LeaveCriticalSection(&sg_memoryFileLock);
				return false;
			}
			cachePathOut = it->second;
			sg_diskCacheFileHandles.erase(it);
			LeaveCriticalSection(&sg_memoryFileLock);
			return true;
		}

		static const wchar_t* GetMoveMethodName(DWORD dwMoveMethod)
		{
			switch (dwMoveMethod)
			{
			case FILE_BEGIN:
				return L"FILE_BEGIN";
			case FILE_CURRENT:
				return L"FILE_CURRENT";
			case FILE_END:
				return L"FILE_END";
			default:
				return L"UNKNOWN";
			}
		}

		static bool TryResolveCustomPakMemory(const wchar_t* sourcePathW, std::shared_ptr<const std::vector<uint8_t>>& dataOut)
		{
			dataOut.reset();
			if (!ResolveCustomPakVFSData(sourcePathW, dataOut))
			{
				return false;
			}
			return dataOut && !dataOut->empty();
		}

		static bool TryResolveCustomPakFileSize(const wchar_t* sourcePathW, uint64_t& outSize)
		{
			outSize = 0;
			return ResolveCustomPakVFSFileSize(sourcePathW, outSize);
		}

		bool TryGetCustomPakDiskCachePath(const wchar_t* sourcePathW, std::wstring& cachePathOut)
		{
			cachePathOut.clear();
			std::shared_ptr<const std::vector<uint8_t>> customPakData;
			if (!TryResolveCustomPakMemory(sourcePathW, customPakData))
			{
				return false;
			}
			return EnsureCustomPakCacheFile(sourcePathW, customPakData, cachePathOut);
		}

		static bool ShouldBlockDirectCustomPakArchive(const wchar_t* sourcePathW)
		{
			if (!sourcePathW || sourcePathW[0] == L'\0')
			{
				return false;
			}
			if (IsCustomPakInternalIoActive())
			{
				return false;
			}
			std::wstring normalizedPath = NormalizeFileHookSlashes(sourcePathW);
			// KrkrPatch archive redirects hand .xp3 back to the engine, so they must stay physically visible.
			if (EndsWithInsensitive(normalizedPath, L".xp3"))
			{
				return false;
			}
			return IsCustomPakArchivePath(normalizedPath.c_str());
		}

		static void FillMemoryFileAttributesData(const std::shared_ptr<const std::vector<uint8_t>>& data, WIN32_FILE_ATTRIBUTE_DATA* outData)
		{
			ZeroMemory(outData, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
			outData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
			uint64_t size = data ? data->size() : 0;
			outData->nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFull);
			outData->nFileSizeHigh = static_cast<DWORD>((size >> 32) & 0xFFFFFFFFull);
		}

		// Hook CreateFileA
		
		HANDLE WINAPI newCreateFileA_Patch(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, 
			LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, 
			DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
		{
			if (ShouldBypassFileHook())
			{
				return rawCreateFileA_Patch(lpFileName, dwDesiredAccess, dwShareMode,
					lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			}
			std::wstring sourcePath = AnsiToWide(lpFileName ? lpFileName : "");
			if (sg_bEnableLog)
			{
				LogMessage(LogLevel::Debug, L"[CreateFileA] Original source=%s desiredAccess=0x%08X shareMode=0x%08X creationDisposition=0x%08X flags=0x%08X",
					sourcePath.c_str(), dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes);
			}
			if (IsCustomPakInternalIoActive())
			{
				return rawCreateFileA_Patch(lpFileName, dwDesiredAccess, dwShareMode,
					lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			}
			if (ShouldBlockDirectCustomPakArchive(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[CreateFileA] Block direct custom pak archive access: %s", sourcePath.c_str());
				return INVALID_HANDLE_VALUE;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePath.c_str(), redirectedPathW))
			{
				if (dwCreationDisposition != OPEN_EXISTING)
				{
					EnsureRedirectTargetParentDirectory(redirectedPathW);
				}
				std::string redirectedPathA = WideToAnsi(redirectedPathW);
				if (!redirectedPathA.empty())
				{
					return rawCreateFileA_Patch(redirectedPathA.c_str(), dwDesiredAccess, dwShareMode,
						lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
				}
			}
			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePath.c_str());
			for (const std::wstring& patchPathW : patchCandidates)
			{
				std::string patchPathA = WideToAnsi(patchPathW);
				if (patchPathA.empty())
				{
					continue;
				}
				HANDLE hFile = rawCreateFileA_Patch(patchPathA.c_str(), dwDesiredAccess, dwShareMode, 
					lpSecurityAttributes, OPEN_EXISTING, dwFlagsAndAttributes, hTemplateFile);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					LogMessage(LogLevel::Info, L"[CreateFileA] Hooked source=%s redirected=Patch:%s", sourcePath.c_str(), patchPathW.c_str());
					return hFile;
				}
			}

			const DWORD customPakWriteMask = GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA
				| FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER;
			bool requestWriteAccess = (dwDesiredAccess & customPakWriteMask) != 0;
			bool canTryCustomPak = true;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePath.c_str());
			if (canTryCustomPak && allowCustomPak)
			{
				std::shared_ptr<const std::vector<uint8_t>> customPakData;
				bool resolvedCustomPak = TryResolveCustomPakMemory(sourcePath.c_str(), customPakData);
				if (resolvedCustomPak)
				{
					bool useDiskMode = PreferDiskBackedCustomPak() || requestWriteAccess;
					if (useDiskMode)
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePath.c_str(), customPakData, cachePathW))
						{
							std::string cachePathA = WideToAnsi(cachePathW);
							if (!cachePathA.empty())
							{
								HANDLE hFile = rawCreateFileA_Patch(cachePathA.c_str(), dwDesiredAccess, dwShareMode | FILE_SHARE_DELETE,
									lpSecurityAttributes, OPEN_EXISTING, dwFlagsAndAttributes, hTemplateFile);
								if (hFile != INVALID_HANDLE_VALUE)
								{
									RegisterDiskCacheFileHandle(hFile, cachePathW);
									LogMessage(LogLevel::Info, L"[CreateFileA] Hooked source=%s redirected=CustomPAK:disk path=%s size=%u write=%u", sourcePath.c_str(), cachePathW.c_str(), (uint32_t)customPakData->size(), requestWriteAccess ? 1u : 0u);
									return hFile;
								}
							}
						}
					}
					else
					{
						HANDLE hMem = AllocateMemoryFileHandle(sourcePath.c_str(), customPakData);
						if (hMem == INVALID_HANDLE_VALUE)
						{
							return INVALID_HANDLE_VALUE;
						}
						LogMessage(LogLevel::Info, L"[CreateFileA] Hooked source=%s redirected=CustomPAK:memory size=%u", sourcePath.c_str(), (uint32_t)customPakData->size());
						return hMem;
					}
				}
			}

			if (IsSpoofTarget(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[CreateFileA] Spoofed: %s", sourcePath.c_str());
				return INVALID_HANDLE_VALUE;
			}

			return rawCreateFileA_Patch(lpFileName, dwDesiredAccess, dwShareMode, 
				lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
		}

		// Hook CreateFileW
		
		HANDLE WINAPI newCreateFileW_Patch(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, 
			LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, 
			DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
		{
			if (ShouldBypassFileHook())
			{
				return rawCreateFileW_Patch(lpFileName, dwDesiredAccess, dwShareMode,
					lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			}
			const wchar_t* sourcePathW = lpFileName ? lpFileName : L"";
			if (sg_bEnableLog)
			{
				LogMessage(LogLevel::Debug, L"[CreateFileW] Original source=%s desiredAccess=0x%08X shareMode=0x%08X creationDisposition=0x%08X flags=0x%08X",
					sourcePathW, dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes);
			}
			if (IsCustomPakInternalIoActive())
			{
				return rawCreateFileW_Patch(lpFileName, dwDesiredAccess, dwShareMode,
					lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			}
			if (ShouldBlockDirectCustomPakArchive(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[CreateFileW] Block direct custom pak archive access: %s", sourcePathW);
				return INVALID_HANDLE_VALUE;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePathW, redirectedPathW))
			{
				if (dwCreationDisposition != OPEN_EXISTING)
				{
					EnsureRedirectTargetParentDirectory(redirectedPathW);
				}
				return rawCreateFileW_Patch(redirectedPathW.c_str(), dwDesiredAccess, dwShareMode,
					lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
			}

			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePathW);
			for (const std::wstring& patchPath : patchCandidates)
			{
				HANDLE hFile = rawCreateFileW_Patch(patchPath.c_str(), dwDesiredAccess, dwShareMode, 
					lpSecurityAttributes, OPEN_EXISTING, dwFlagsAndAttributes, hTemplateFile);
				if (hFile != INVALID_HANDLE_VALUE)
				{
					LogMessage(LogLevel::Info, L"[CreateFileW] Hooked source=%s redirected=Patch:%s desiredAccess=0x%08X shareMode=0x%08X creationDisposition=0x%08X flags=0x%08X",
						sourcePathW, patchPath.c_str(), dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes);
					return hFile;
				}
			}

			const DWORD customPakWriteMask = GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA
				| FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | DELETE | WRITE_DAC | WRITE_OWNER;
			bool requestWriteAccess = (dwDesiredAccess & customPakWriteMask) != 0;
			bool canTryCustomPak = true;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePathW);
			if (canTryCustomPak && allowCustomPak)
			{
				std::shared_ptr<const std::vector<uint8_t>> customPakData;
				bool resolvedCustomPak = TryResolveCustomPakMemory(sourcePathW, customPakData);
				if (resolvedCustomPak)
				{
					bool useDiskMode = PreferDiskBackedCustomPak() || requestWriteAccess;
					if (useDiskMode)
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePathW, customPakData, cachePathW))
						{
							HANDLE hFile = rawCreateFileW_Patch(cachePathW.c_str(), dwDesiredAccess, dwShareMode | FILE_SHARE_DELETE,
								lpSecurityAttributes, OPEN_EXISTING, dwFlagsAndAttributes, hTemplateFile);
							if (hFile != INVALID_HANDLE_VALUE)
							{
								RegisterDiskCacheFileHandle(hFile, cachePathW);
								LogMessage(LogLevel::Info, L"[CreateFileW] Hooked source=%s redirected=CustomPAK:disk path=%s size=%u desiredAccess=0x%08X shareMode=0x%08X creationDisposition=0x%08X flags=0x%08X write=%u",
									sourcePathW, cachePathW.c_str(), (uint32_t)customPakData->size(),
									dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes, requestWriteAccess ? 1u : 0u);
								return hFile;
							}
						}
					}
					else
					{
						HANDLE hMem = AllocateMemoryFileHandle(sourcePathW, customPakData);
						if (hMem == INVALID_HANDLE_VALUE)
						{
							return INVALID_HANDLE_VALUE;
						}
						LogMessage(LogLevel::Info, L"[CreateFileW] Hooked source=%s redirected=CustomPAK:memory size=%u desiredAccess=0x%08X shareMode=0x%08X creationDisposition=0x%08X flags=0x%08X",
							sourcePathW, (uint32_t)customPakData->size(),
							dwDesiredAccess, dwShareMode, dwCreationDisposition, dwFlagsAndAttributes);
						return hMem;
					}
				}
			}

			if (IsSpoofTarget(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[CreateFileW] Spoofed: %s", sourcePathW);
				return INVALID_HANDLE_VALUE;
			}

			return rawCreateFileW_Patch(lpFileName, dwDesiredAccess, dwShareMode, 
				lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
		}

		// Hook GetFileAttributesA
		static pGetFileAttributesA_File rawGetFileAttributesA_Patch = GetFileAttributesA;
		
		DWORD WINAPI newGetFileAttributesA_Patch(LPCSTR lpFileName)
		{
			if (ShouldBypassFileHook())
			{
				return rawGetFileAttributesA_Patch(lpFileName);
			}
			std::wstring sourcePath = AnsiToWide(lpFileName ? lpFileName : "");
			if (ShouldBlockDirectCustomPakArchive(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesA] Block direct custom pak archive access: %s", sourcePath.c_str());
				return INVALID_FILE_ATTRIBUTES;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePath.c_str(), redirectedPathW))
			{
				std::string redirectedPathA = WideToAnsi(redirectedPathW);
				if (!redirectedPathA.empty())
				{
					return rawGetFileAttributesA_Patch(redirectedPathA.c_str());
				}
			}
			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePath.c_str());
			for (const std::wstring& patchPathW : patchCandidates)
			{
				std::string patchPathA = WideToAnsi(patchPathW);
				if (patchPathA.empty())
				{
					continue;
				}
				DWORD attr = rawGetFileAttributesA_Patch(patchPathA.c_str());
				if (attr != INVALID_FILE_ATTRIBUTES)
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesA] Hooked source=%s redirected=Patch:%s", sourcePath.c_str(), patchPathW.c_str());
					return attr;
				}
			}

			uint64_t fileSize = 0;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePath.c_str()) && !HasExistingPatchOverride(sourcePath.c_str());
			if (allowCustomPak && TryResolveCustomPakFileSize(sourcePath.c_str(), fileSize))
			{
				if (PreferDiskBackedCustomPak())
				{
					std::shared_ptr<const std::vector<uint8_t>> customPakData;
					if (TryResolveCustomPakMemory(sourcePath.c_str(), customPakData))
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePath.c_str(), customPakData, cachePathW))
						{
							std::string cachePathA = WideToAnsi(cachePathW);
							if (!cachePathA.empty())
							{
								DWORD attr = rawGetFileAttributesA_Patch(cachePathA.c_str());
								if (attr != INVALID_FILE_ATTRIBUTES)
								{
									LogMessage(LogLevel::Info, L"[GetFileAttributesA] Hooked source=%s redirected=CustomPAK:disk path=%s", sourcePath.c_str(), cachePathW.c_str());
									return attr;
								}
							}
						}
					}
				}
				else
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesA] Hooked source=%s redirected=CustomPAK:size_only size=%llu", sourcePath.c_str(), fileSize);
					return FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
				}
			}

			if (IsSpoofTarget(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesA] Spoofed: %s", sourcePath.c_str());
				return INVALID_FILE_ATTRIBUTES;
			}

			return rawGetFileAttributesA_Patch(lpFileName);
		}

		// Hook GetFileAttributesW
		static pGetFileAttributesW_File rawGetFileAttributesW_Patch = GetFileAttributesW;
		
		DWORD WINAPI newGetFileAttributesW_Patch(LPCWSTR lpFileName)
		{
			if (ShouldBypassFileHook())
			{
				return rawGetFileAttributesW_Patch(lpFileName);
			}
			const wchar_t* sourcePathW = lpFileName ? lpFileName : L"";
			if (ShouldBlockDirectCustomPakArchive(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesW] Block direct custom pak archive access: %s", sourcePathW);
				return INVALID_FILE_ATTRIBUTES;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePathW, redirectedPathW))
			{
				return rawGetFileAttributesW_Patch(redirectedPathW.c_str());
			}
			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePathW);
			for (const std::wstring& patchPath : patchCandidates)
			{
				DWORD attr = rawGetFileAttributesW_Patch(patchPath.c_str());
				if (attr != INVALID_FILE_ATTRIBUTES)
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesW] Hooked source=%s redirected=Patch:%s", sourcePathW, patchPath.c_str());
					return attr;
				}
			}

			uint64_t fileSize = 0;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePathW) && !HasExistingPatchOverride(sourcePathW);
			if (allowCustomPak && TryResolveCustomPakFileSize(sourcePathW, fileSize))
			{
				if (PreferDiskBackedCustomPak())
				{
					std::shared_ptr<const std::vector<uint8_t>> customPakData;
					if (TryResolveCustomPakMemory(sourcePathW, customPakData))
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePathW, customPakData, cachePathW))
						{
							DWORD attr = rawGetFileAttributesW_Patch(cachePathW.c_str());
							if (attr != INVALID_FILE_ATTRIBUTES)
							{
								LogMessage(LogLevel::Info, L"[GetFileAttributesW] Hooked source=%s redirected=CustomPAK:disk path=%s", sourcePathW, cachePathW.c_str());
								return attr;
							}
						}
					}
				}
				else
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesW] Hooked source=%s redirected=CustomPAK:size_only size=%llu", sourcePathW, fileSize);
					return FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
				}
			}

			if (IsSpoofTarget(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesW] Spoofed: %s", sourcePathW);
				return INVALID_FILE_ATTRIBUTES;
			}

			return rawGetFileAttributesW_Patch(lpFileName);
		}

		// Hook GetFileAttributesExA
		static pGetFileAttributesExA_File rawGetFileAttributesExA_Patch = GetFileAttributesExA;
		
		BOOL WINAPI newGetFileAttributesExA_Patch(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
		{
			if (ShouldBypassFileHook())
			{
				return rawGetFileAttributesExA_Patch(lpFileName, fInfoLevelId, lpFileInformation);
			}
			std::wstring sourcePath = AnsiToWide(lpFileName ? lpFileName : "");
			if (ShouldBlockDirectCustomPakArchive(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesExA] Block direct custom pak archive access: %s", sourcePath.c_str());
				return FALSE;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePath.c_str(), redirectedPathW))
			{
				std::string redirectedPathA = WideToAnsi(redirectedPathW);
				if (!redirectedPathA.empty())
				{
					return rawGetFileAttributesExA_Patch(redirectedPathA.c_str(), fInfoLevelId, lpFileInformation);
				}
			}
			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePath.c_str());
			for (const std::wstring& patchPathW : patchCandidates)
			{
				std::string patchPathA = WideToAnsi(patchPathW);
				if (patchPathA.empty())
				{
					continue;
				}
				DWORD attr = rawGetFileAttributesA_Patch(patchPathA.c_str());
				if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesExA] Hooked source=%s redirected=Patch:%s infoLevel=%u", sourcePath.c_str(), patchPathW.c_str(), static_cast<unsigned>(fInfoLevelId));
					return rawGetFileAttributesExA_Patch(patchPathA.c_str(), fInfoLevelId, lpFileInformation);
				}
			}

			uint64_t fileSize = 0;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePath.c_str()) && !HasExistingPatchOverride(sourcePath.c_str());
			if (allowCustomPak && TryResolveCustomPakFileSize(sourcePath.c_str(), fileSize))
			{
				if (PreferDiskBackedCustomPak())
				{
					std::shared_ptr<const std::vector<uint8_t>> customPakData;
					if (TryResolveCustomPakMemory(sourcePath.c_str(), customPakData))
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePath.c_str(), customPakData, cachePathW))
						{
							std::string cachePathA = WideToAnsi(cachePathW);
							if (!cachePathA.empty() && rawGetFileAttributesExA_Patch(cachePathA.c_str(), fInfoLevelId, lpFileInformation))
							{
								LogMessage(LogLevel::Info, L"[GetFileAttributesExA] Hooked source=%s redirected=CustomPAK:disk path=%s infoLevel=%u", sourcePath.c_str(), cachePathW.c_str(), static_cast<unsigned>(fInfoLevelId));
								return TRUE;
							}
						}
					}
				}
				else if (fInfoLevelId == GetFileExInfoStandard && lpFileInformation)
				{
					WIN32_FILE_ATTRIBUTE_DATA* attrData = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation);
					ZeroMemory(attrData, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
					attrData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
					attrData->nFileSizeLow = static_cast<DWORD>(fileSize & 0xFFFFFFFFull);
					attrData->nFileSizeHigh = static_cast<DWORD>((fileSize >> 32) & 0xFFFFFFFFull);
					SYSTEMTIME st;
					GetSystemTime(&st);
					FILETIME ft;
					SystemTimeToFileTime(&st, &ft);
					attrData->ftCreationTime = ft;
					attrData->ftLastAccessTime = ft;
					attrData->ftLastWriteTime = ft;
					LogMessage(LogLevel::Info, L"[GetFileAttributesExA] Hooked source=%s redirected=CustomPAK:size_only size=%llu infoLevel=%u", sourcePath.c_str(), fileSize, static_cast<unsigned>(fInfoLevelId));
					return TRUE;
				}
			}

			if (IsSpoofTarget(sourcePath.c_str()))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesExA] Spoofed: %s", sourcePath.c_str());
				return FALSE;
			}

			return rawGetFileAttributesExA_Patch(lpFileName, fInfoLevelId, lpFileInformation);
		}

		// Hook GetFileAttributesExW
		static pGetFileAttributesExW_File rawGetFileAttributesExW_Patch = GetFileAttributesExW;
		
		BOOL WINAPI newGetFileAttributesExW_Patch(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
		{
			if (ShouldBypassFileHook())
			{
				return rawGetFileAttributesExW_Patch(lpFileName, fInfoLevelId, lpFileInformation);
			}
			const wchar_t* sourcePathW = lpFileName ? lpFileName : L"";
			if (ShouldBlockDirectCustomPakArchive(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesExW] Block direct custom pak archive access: %s", sourcePathW);
				return FALSE;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(sourcePathW, redirectedPathW))
			{
				return rawGetFileAttributesExW_Patch(redirectedPathW.c_str(), fInfoLevelId, lpFileInformation);
			}
			std::vector<std::wstring> patchCandidates = BuildPatchCandidates(sourcePathW);
			for (const std::wstring& patchPath : patchCandidates)
			{
				DWORD attr = rawGetFileAttributesW_Patch(patchPath.c_str());
				if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
				{
					LogMessage(LogLevel::Info, L"[GetFileAttributesExW] Hooked source=%s redirected=Patch:%s infoLevel=%u", sourcePathW, patchPath.c_str(), static_cast<unsigned>(fInfoLevelId));
					return rawGetFileAttributesExW_Patch(patchPath.c_str(), fInfoLevelId, lpFileInformation);
				}
			}

			uint64_t fileSize = 0;
			bool allowCustomPak = !IsPathInsidePatchFolder(sourcePathW) && !HasExistingPatchOverride(sourcePathW);
			if (allowCustomPak && TryResolveCustomPakFileSize(sourcePathW, fileSize))
			{
				if (PreferDiskBackedCustomPak())
				{
					std::shared_ptr<const std::vector<uint8_t>> customPakData;
					if (TryResolveCustomPakMemory(sourcePathW, customPakData))
					{
						std::wstring cachePathW;
						if (EnsureCustomPakCacheFile(sourcePathW, customPakData, cachePathW)
							&& rawGetFileAttributesExW_Patch(cachePathW.c_str(), fInfoLevelId, lpFileInformation))
						{
							LogMessage(LogLevel::Info, L"[GetFileAttributesExW] Hooked source=%s redirected=CustomPAK:disk path=%s infoLevel=%u", sourcePathW, cachePathW.c_str(), static_cast<unsigned>(fInfoLevelId));
							return TRUE;
						}
					}
				}
				else if (fInfoLevelId == GetFileExInfoStandard && lpFileInformation)
				{
					WIN32_FILE_ATTRIBUTE_DATA* attrData = reinterpret_cast<WIN32_FILE_ATTRIBUTE_DATA*>(lpFileInformation);
					ZeroMemory(attrData, sizeof(WIN32_FILE_ATTRIBUTE_DATA));
					attrData->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY;
					attrData->nFileSizeLow = static_cast<DWORD>(fileSize & 0xFFFFFFFFull);
					attrData->nFileSizeHigh = static_cast<DWORD>((fileSize >> 32) & 0xFFFFFFFFull);
					SYSTEMTIME st;
					GetSystemTime(&st);
					FILETIME ft;
					SystemTimeToFileTime(&st, &ft);
					attrData->ftCreationTime = ft;
					attrData->ftLastAccessTime = ft;
					attrData->ftLastWriteTime = ft;
					LogMessage(LogLevel::Info, L"[GetFileAttributesExW] Hooked source=%s redirected=CustomPAK:size_only size=%llu infoLevel=%u", sourcePathW, fileSize, static_cast<unsigned>(fInfoLevelId));
					return TRUE;
				}
			}

			if (IsSpoofTarget(sourcePathW))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[GetFileAttributesExW] Spoofed: %s", sourcePathW);
				return FALSE;
			}

			return rawGetFileAttributesExW_Patch(lpFileName, fInfoLevelId, lpFileInformation);
		}

		static pReadFile_File rawReadFile_Patch = ReadFile;
		static pSetFilePointer_File rawSetFilePointer_Patch = SetFilePointer;
		static pSetFilePointerEx_File rawSetFilePointerEx_Patch = SetFilePointerEx;
		static pGetFileSize_File rawGetFileSize_Patch = GetFileSize;
		static pGetFileSizeEx_File rawGetFileSizeEx_Patch = GetFileSizeEx;
		static pCloseHandle_File rawCloseHandle_Patch = CloseHandle;
		static pGetFileInformationByHandle_File rawGetFileInformationByHandle_Patch = GetFileInformationByHandle;
		static pGetFileType_File rawGetFileType_Patch = GetFileType;
		static pCreateFileMappingW_File rawCreateFileMappingW_Patch = CreateFileMappingW;
		static pCreateFileMappingA_File rawCreateFileMappingA_Patch = CreateFileMappingA;
		static pFindFirstFileA_File rawFindFirstFileA_Patch = FindFirstFileA;
		static pFindFirstFileW_File rawFindFirstFileW_Patch = FindFirstFileW;
		static pFindNextFileA_File rawFindNextFileA_Patch = FindNextFileA;
		static pFindNextFileW_File rawFindNextFileW_Patch = FindNextFileW;
		static pFindClose_File rawFindClose_Patch = FindClose;

		static HANDLE TryCreateCustomPakDiskMappingW(const MemoryFileState& state,
			LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
			DWORD flProtect,
			DWORD dwMaximumSizeHigh,
			DWORD dwMaximumSizeLow,
			LPCWSTR lpName)
		{
			if (!state.data || state.sourcePath.empty())
			{
				return nullptr;
			}

			std::wstring cachePath;
			if (!EnsureCustomPakCacheFile(state.sourcePath.c_str(), state.data, cachePath))
			{
				return nullptr;
			}

			HANDLE hFile = rawCreateFileW_Patch(
				cachePath.c_str(),
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				return nullptr;
			}

			HANDLE hMap = rawCreateFileMappingW_Patch(
				hFile,
				lpFileMappingAttributes,
				flProtect,
				dwMaximumSizeHigh,
				dwMaximumSizeLow,
				lpName);
			rawCloseHandle_Patch(hFile);
			return hMap;
		}

		static HANDLE TryCreateCustomPakDiskMappingA(const MemoryFileState& state,
			LPSECURITY_ATTRIBUTES lpFileMappingAttributes,
			DWORD flProtect,
			DWORD dwMaximumSizeHigh,
			DWORD dwMaximumSizeLow,
			LPCSTR lpName)
		{
			if (!state.data || state.sourcePath.empty())
			{
				return nullptr;
			}

			std::wstring cachePath;
			if (!EnsureCustomPakCacheFile(state.sourcePath.c_str(), state.data, cachePath))
			{
				return nullptr;
			}

			HANDLE hFile = rawCreateFileW_Patch(
				cachePath.c_str(),
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (hFile == INVALID_HANDLE_VALUE)
			{
				return nullptr;
			}

			HANDLE hMap = rawCreateFileMappingA_Patch(
				hFile,
				lpFileMappingAttributes,
				flProtect,
				dwMaximumSizeHigh,
				dwMaximumSizeLow,
				lpName);
			rawCloseHandle_Patch(hFile);
			return hMap;
		}

		BOOL WINAPI newReadFile_Patch(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawReadFile_Patch(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
			}
			if (lpNumberOfBytesRead)
			{
				*lpNumberOfBytesRead = 0;
			}
			if (!lpBuffer || !state.data)
			{
				SetLastError(ERROR_INVALID_PARAMETER);
				return FALSE;
			}
			uint64_t offset = state.position;
			if (lpOverlapped)
			{
				offset = (static_cast<uint64_t>(lpOverlapped->OffsetHigh) << 32) | lpOverlapped->Offset;
			}
			const uint64_t totalSize = static_cast<uint64_t>(state.data->size());
			if (offset >= totalSize)
			{
				if (sg_bCustomPakLog)
				{
					LogMessage(LogLevel::Debug, L"[ReadFile] Hooked memory handle=%p request=%u offset=%llu read=0 eof=1 overlapped=%u",
						hFile, nNumberOfBytesToRead, offset, lpOverlapped ? 1u : 0u);
				}
				SetLastError(ERROR_HANDLE_EOF);
				return TRUE;
			}
			uint64_t remain = totalSize - offset;
			DWORD readBytes = remain > nNumberOfBytesToRead ? nNumberOfBytesToRead : static_cast<DWORD>(remain);
			if (readBytes > 0)
			{
				memcpy(lpBuffer, state.data->data() + static_cast<size_t>(offset), readBytes);
			}
			if (lpNumberOfBytesRead)
			{
				*lpNumberOfBytesRead = readBytes;
			}
			if (!lpOverlapped)
			{
				UpdateMemoryFilePosition(hFile, offset + readBytes);
			}
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[ReadFile] Hooked memory handle=%p request=%u offset=%llu read=%u newPosition=%llu overlapped=%u",
					hFile, nNumberOfBytesToRead, offset, readBytes, lpOverlapped ? state.position : (offset + readBytes), lpOverlapped ? 1u : 0u);
			}
			SetLastError(ERROR_SUCCESS);
			return TRUE;
		}

		DWORD WINAPI newSetFilePointer_Patch(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawSetFilePointer_Patch(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
			}
			LONGLONG move = static_cast<LONGLONG>(lDistanceToMove);
			if (lpDistanceToMoveHigh)
			{
				move |= (static_cast<LONGLONG>(*lpDistanceToMoveHigh) << 32);
			}
			LONGLONG base = 0;
			if (dwMoveMethod == FILE_BEGIN)
			{
				base = 0;
			}
			else if (dwMoveMethod == FILE_CURRENT)
			{
				base = static_cast<LONGLONG>(state.position);
			}
			else if (dwMoveMethod == FILE_END)
			{
				base = static_cast<LONGLONG>(state.data ? state.data->size() : 0);
			}
			else
			{
				SetLastError(ERROR_INVALID_PARAMETER);
				return INVALID_SET_FILE_POINTER;
			}
			LONGLONG result = base + move;
			if (result < 0)
			{
				SetLastError(ERROR_NEGATIVE_SEEK);
				return INVALID_SET_FILE_POINTER;
			}
			uint64_t newPos = static_cast<uint64_t>(result);
			UpdateMemoryFilePosition(hFile, newPos);
			if (lpDistanceToMoveHigh)
			{
				*lpDistanceToMoveHigh = static_cast<LONG>((newPos >> 32) & 0xFFFFFFFFull);
			}
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[SetFilePointer] Hooked memory handle=%p moveLow=%ld moveHigh=%ld moveMethod=%s(%u) result=%llu",
					hFile, lDistanceToMove, lpDistanceToMoveHigh ? *lpDistanceToMoveHigh : 0, GetMoveMethodName(dwMoveMethod), dwMoveMethod, newPos);
			}
			SetLastError(ERROR_SUCCESS);
			return static_cast<DWORD>(newPos & 0xFFFFFFFFull);
		}

		BOOL WINAPI newSetFilePointerEx_Patch(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawSetFilePointerEx_Patch(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
			}
			LONGLONG base = 0;
			if (dwMoveMethod == FILE_BEGIN)
			{
				base = 0;
			}
			else if (dwMoveMethod == FILE_CURRENT)
			{
				base = static_cast<LONGLONG>(state.position);
			}
			else if (dwMoveMethod == FILE_END)
			{
				base = static_cast<LONGLONG>(state.data ? state.data->size() : 0);
			}
			else
			{
				SetLastError(ERROR_INVALID_PARAMETER);
				return FALSE;
			}
			LONGLONG result = base + liDistanceToMove.QuadPart;
			if (result < 0)
			{
				SetLastError(ERROR_NEGATIVE_SEEK);
				return FALSE;
			}
			uint64_t newPos = static_cast<uint64_t>(result);
			UpdateMemoryFilePosition(hFile, newPos);
			if (lpNewFilePointer)
			{
				lpNewFilePointer->QuadPart = static_cast<LONGLONG>(newPos);
			}
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[SetFilePointerEx] Hooked memory handle=%p move=%lld moveMethod=%s(%u) result=%llu",
					hFile, liDistanceToMove.QuadPart, GetMoveMethodName(dwMoveMethod), dwMoveMethod, newPos);
			}
			SetLastError(ERROR_SUCCESS);
			return TRUE;
		}

		DWORD WINAPI newGetFileSize_Patch(HANDLE hFile, LPDWORD lpFileSizeHigh)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawGetFileSize_Patch(hFile, lpFileSizeHigh);
			}
			uint64_t size = state.data ? state.data->size() : 0;
			if (lpFileSizeHigh)
			{
				*lpFileSizeHigh = static_cast<DWORD>((size >> 32) & 0xFFFFFFFFull);
			}
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[GetFileSize] Hooked memory handle=%p sizeLow=%u sizeHigh=%u",
					hFile, static_cast<DWORD>(size & 0xFFFFFFFFull), static_cast<DWORD>((size >> 32) & 0xFFFFFFFFull));
			}
			return static_cast<DWORD>(size & 0xFFFFFFFFull);
		}

		BOOL WINAPI newGetFileSizeEx_Patch(HANDLE hFile, PLARGE_INTEGER lpFileSize)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawGetFileSizeEx_Patch(hFile, lpFileSize);
			}
			if (!lpFileSize)
			{
				SetLastError(ERROR_INVALID_PARAMETER);
				return FALSE;
			}
			lpFileSize->QuadPart = static_cast<LONGLONG>(state.data ? state.data->size() : 0);
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[GetFileSizeEx] Hooked memory handle=%p size=%llu", hFile, static_cast<uint64_t>(lpFileSize->QuadPart));
			}
			SetLastError(ERROR_SUCCESS);
			return TRUE;
		}

		BOOL WINAPI newGetFileInformationByHandle_Patch(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawGetFileInformationByHandle_Patch(hFile, lpFileInformation);
			}
			ZeroMemory(lpFileInformation, sizeof(BY_HANDLE_FILE_INFORMATION));
			lpFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_READONLY;
			uint64_t size = state.data ? state.data->size() : 0;
			lpFileInformation->nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFFull);
			lpFileInformation->nFileSizeHigh = static_cast<DWORD>((size >> 32) & 0xFFFFFFFFull);
			lpFileInformation->nNumberOfLinks = 1;
			SYSTEMTIME st;
			GetSystemTime(&st);
			FILETIME ft;
			SystemTimeToFileTime(&st, &ft);
			lpFileInformation->ftCreationTime = ft;
			lpFileInformation->ftLastAccessTime = ft;
			lpFileInformation->ftLastWriteTime = ft;
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[GetFileInformationByHandle] Hooked memory handle=%p size=%llu attrs=0x%08X",
					hFile, size, lpFileInformation->dwFileAttributes);
			}
			SetLastError(ERROR_SUCCESS);
			return TRUE;
		}

		DWORD WINAPI newGetFileType_Patch(HANDLE hFile)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawGetFileType_Patch(hFile);
			}
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[GetFileType] Hooked memory handle=%p type=%u", hFile, FILE_TYPE_DISK);
			}
			return FILE_TYPE_DISK;
		}

		BOOL WINAPI newCloseHandle_Patch(HANDLE hObject)
		{
			if (RemoveMemoryFileHandle(hObject))
			{
				if (sg_bCustomPakLog)
				{
					LogMessage(LogLevel::Debug, L"[CloseHandle] Hooked memory handle=%p", hObject);
				}
				SetLastError(ERROR_SUCCESS);
				return TRUE;
			}
			std::wstring cachePath;
			bool isDiskCacheHandle = PopDiskCacheFileHandle(hObject, cachePath);
			if (ShouldBypassFileHook() && !isDiskCacheHandle)
			{
				return rawCloseHandle_Patch(hObject);
			}
			BOOL result = rawCloseHandle_Patch(hObject);
			if (!result && isDiskCacheHandle)
			{
				RegisterDiskCacheFileHandle(hObject, cachePath);
			}
			if (result && isDiskCacheHandle)
			{
				bool deleted = DeleteCustomPakCacheFileNow(cachePath);
				LogMessage(deleted ? LogLevel::Info : LogLevel::Warn,
					L"[CloseHandle] CustomPAK disk cache cleanup path=%s deleted=%u",
					cachePath.c_str(), deleted ? 1u : 0u);
			}
			return result;
		}

		HANDLE WINAPI newCreateFileMappingW_Patch(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawCreateFileMappingW_Patch(hFile, lpFileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
			}
			HANDLE hDiskMap = TryCreateCustomPakDiskMappingW(state, lpFileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
			if (hDiskMap)
			{
				if (sg_bCustomPakLog)
				{
					LogMessage(LogLevel::Debug, L"[CreateFileMappingW] Hooked memory handle=%p upgraded=disk_cache source=%s name=%s",
						hFile, state.sourcePath.c_str(), lpName ? lpName : L"(null)");
				}
				return hDiskMap;
			}
			uint64_t requestedSize = (static_cast<uint64_t>(dwMaximumSizeHigh) << 32) | dwMaximumSizeLow;
			uint64_t dataSize = state.data ? state.data->size() : 0;
			uint64_t mapSize = (requestedSize == 0 || (dataSize != 0 && requestedSize < dataSize)) ? dataSize : requestedSize;
			if (mapSize == 0)
			{
				mapSize = 1;
			}
			DWORD secFlags = flProtect & 0xFFF00000u;
			DWORD baseProtect = (flProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
				? PAGE_EXECUTE_READWRITE
				: PAGE_READWRITE;
			DWORD mapProtect = secFlags | baseProtect;
			if (sg_bCustomPakLog)
			{
				LogMessage(LogLevel::Debug, L"[CreateFileMappingW] Hooked memory handle=%p protect=0x%08X requestedSize=%llu mapSize=%llu name=%s",
					hFile, flProtect, requestedSize, mapSize, lpName ? lpName : L"(null)");
			}
			HANDLE hMap = rawCreateFileMappingW_Patch(INVALID_HANDLE_VALUE, lpFileMappingAttributes, mapProtect,
				static_cast<DWORD>((mapSize >> 32) & 0xFFFFFFFFull), static_cast<DWORD>(mapSize & 0xFFFFFFFFull), lpName);
			if (!hMap)
			{
				return hMap;
			}
			void* view = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(mapSize));
			if (!view)
			{
				rawCloseHandle_Patch(hMap);
				return nullptr;
			}
			memset(view, 0, static_cast<size_t>(mapSize));
			if (state.data && !state.data->empty())
			{
				size_t copySize = state.data->size() < mapSize ? state.data->size() : static_cast<size_t>(mapSize);
				memcpy(view, state.data->data(), copySize);
			}
			UnmapViewOfFile(view);
			return hMap;
		}

		HANDLE WINAPI newCreateFileMappingA_Patch(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName)
		{
			MemoryFileState state = {};
			if (!GetMemoryFileState(hFile, state))
			{
				return rawCreateFileMappingA_Patch(hFile, lpFileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
			}
			HANDLE hDiskMap = TryCreateCustomPakDiskMappingA(state, lpFileMappingAttributes, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
			if (hDiskMap)
			{
				if (sg_bCustomPakLog)
				{
					std::wstring nameW = lpName ? AnsiToWide(lpName) : L"";
					LogMessage(LogLevel::Debug, L"[CreateFileMappingA] Hooked memory handle=%p upgraded=disk_cache source=%s name=%s",
						hFile, state.sourcePath.c_str(), lpName ? nameW.c_str() : L"(null)");
				}
				return hDiskMap;
			}
			uint64_t requestedSize = (static_cast<uint64_t>(dwMaximumSizeHigh) << 32) | dwMaximumSizeLow;
			uint64_t dataSize = state.data ? state.data->size() : 0;
			uint64_t mapSize = (requestedSize == 0 || (dataSize != 0 && requestedSize < dataSize)) ? dataSize : requestedSize;
			if (mapSize == 0)
			{
				mapSize = 1;
			}
			DWORD secFlags = flProtect & 0xFFF00000u;
			DWORD baseProtect = (flProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
				? PAGE_EXECUTE_READWRITE
				: PAGE_READWRITE;
			DWORD mapProtect = secFlags | baseProtect;
			if (sg_bCustomPakLog)
			{
				std::wstring nameW = lpName ? AnsiToWide(lpName) : L"";
				LogMessage(LogLevel::Debug, L"[CreateFileMappingA] Hooked memory handle=%p protect=0x%08X requestedSize=%llu mapSize=%llu name=%s",
					hFile, flProtect, requestedSize, mapSize, lpName ? nameW.c_str() : L"(null)");
			}
			HANDLE hMap = rawCreateFileMappingA_Patch(INVALID_HANDLE_VALUE, lpFileMappingAttributes, mapProtect,
				static_cast<DWORD>((mapSize >> 32) & 0xFFFFFFFFull), static_cast<DWORD>(mapSize & 0xFFFFFFFFull), lpName);
			if (!hMap)
			{
				return hMap;
			}
			void* view = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(mapSize));
			if (!view)
			{
				rawCloseHandle_Patch(hMap);
				return nullptr;
			}
			memset(view, 0, static_cast<size_t>(mapSize));
			if (state.data && !state.data->empty())
			{
				size_t copySize = state.data->size() < mapSize ? state.data->size() : static_cast<size_t>(mapSize);
				memcpy(view, state.data->data(), copySize);
			}
			UnmapViewOfFile(view);
			return hMap;
		}

		HANDLE WINAPI newFindFirstFileA_Patch(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
		{
			if (ShouldBypassFileHook())
			{
				return rawFindFirstFileA_Patch(lpFileName, lpFindFileData);
			}
			if (lpFileName)
			{
				std::wstring sourcePathW = AnsiToWide(lpFileName);
				std::wstring redirectedPathW;
				if (ResolveDirectoryRedirectPath(sourcePathW.c_str(), redirectedPathW))
				{
					std::string redirectedPathA = WideToAnsi(redirectedPathW);
					if (!redirectedPathA.empty())
					{
						return rawFindFirstFileA_Patch(redirectedPathA.c_str(), lpFindFileData);
					}
				}
				if (IsSpoofTarget(sourcePathW.c_str()))
				{
					SetLastError(ERROR_FILE_NOT_FOUND);
					LogMessage(LogLevel::Info, L"[FindFirstFileA] Spoofed: %s", sourcePathW.c_str());
					return INVALID_HANDLE_VALUE;
				}
			}
			return rawFindFirstFileA_Patch(lpFileName, lpFindFileData);
		}

		HANDLE WINAPI newFindFirstFileW_Patch(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
		{
			if (ShouldBypassFileHook())
			{
				return rawFindFirstFileW_Patch(lpFileName, lpFindFileData);
			}
			if (lpFileName && IsSpoofTarget(lpFileName))
			{
				SetLastError(ERROR_FILE_NOT_FOUND);
				LogMessage(LogLevel::Info, L"[FindFirstFileW] Spoofed: %s", lpFileName);
				return INVALID_HANDLE_VALUE;
			}
			std::wstring redirectedPathW;
			if (ResolveDirectoryRedirectPath(lpFileName ? lpFileName : L"", redirectedPathW))
			{
				return rawFindFirstFileW_Patch(redirectedPathW.c_str(), lpFindFileData);
			}
			return rawFindFirstFileW_Patch(lpFileName, lpFindFileData);
		}

		BOOL WINAPI newFindNextFileA_Patch(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
		{
			if (ShouldBypassFileHook())
			{
				return rawFindNextFileA_Patch(hFindFile, lpFindFileData);
			}
			return rawFindNextFileA_Patch(hFindFile, lpFindFileData);
		}

		BOOL WINAPI newFindNextFileW_Patch(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData)
		{
			if (ShouldBypassFileHook())
			{
				return rawFindNextFileW_Patch(hFindFile, lpFindFileData);
			}
			return rawFindNextFileW_Patch(hFindFile, lpFindFileData);
		}

		BOOL WINAPI newFindClose_Patch(HANDLE hFindFile)
		{
			if (ShouldBypassFileHook())
			{
				return rawFindClose_Patch(hFindFile);
			}
			return rawFindClose_Patch(hFindFile);
		}

		// 启用文件 Hook
		bool HookFileAPIs()
		{
			bool hasFailed = false;
			bool failedCreateFileA = !TryDetourAttach(&rawCreateFileA_Patch, newCreateFileA_Patch);
			bool failedCreateFileW = !TryDetourAttach(&rawCreateFileW_Patch, newCreateFileW_Patch);
			bool failedGetFileAttributesA = !TryDetourAttach(&rawGetFileAttributesA_Patch, newGetFileAttributesA_Patch);
			bool failedGetFileAttributesW = !TryDetourAttach(&rawGetFileAttributesW_Patch, newGetFileAttributesW_Patch);
			bool failedGetFileAttributesExA = !TryDetourAttach(&rawGetFileAttributesExA_Patch, newGetFileAttributesExA_Patch);
			bool failedGetFileAttributesExW = !TryDetourAttach(&rawGetFileAttributesExW_Patch, newGetFileAttributesExW_Patch);
			bool failedReadFile = !TryDetourAttach(&rawReadFile_Patch, newReadFile_Patch);
			bool failedSetFilePointer = !TryDetourAttach(&rawSetFilePointer_Patch, newSetFilePointer_Patch);
			bool failedSetFilePointerEx = !TryDetourAttach(&rawSetFilePointerEx_Patch, newSetFilePointerEx_Patch);
			bool failedGetFileSize = !TryDetourAttach(&rawGetFileSize_Patch, newGetFileSize_Patch);
			bool failedGetFileSizeEx = !TryDetourAttach(&rawGetFileSizeEx_Patch, newGetFileSizeEx_Patch);
			bool failedGetFileInformationByHandle = !TryDetourAttach(&rawGetFileInformationByHandle_Patch, newGetFileInformationByHandle_Patch);
			bool failedGetFileType = !TryDetourAttach(&rawGetFileType_Patch, newGetFileType_Patch);
			bool failedCloseHandle = !TryDetourAttach(&rawCloseHandle_Patch, newCloseHandle_Patch);
			bool failedCreateFileMappingW = !TryDetourAttach(&rawCreateFileMappingW_Patch, newCreateFileMappingW_Patch);
			bool failedCreateFileMappingA = !TryDetourAttach(&rawCreateFileMappingA_Patch, newCreateFileMappingA_Patch);
			bool failedFindFirstFileA = !TryDetourAttach(&rawFindFirstFileA_Patch, newFindFirstFileA_Patch);
			bool failedFindFirstFileW = !TryDetourAttach(&rawFindFirstFileW_Patch, newFindFirstFileW_Patch);
			bool failedFindNextFileA = !TryDetourAttach(&rawFindNextFileA_Patch, newFindNextFileA_Patch);
			bool failedFindNextFileW = !TryDetourAttach(&rawFindNextFileW_Patch, newFindNextFileW_Patch);
			bool failedFindClose = !TryDetourAttach(&rawFindClose_Patch, newFindClose_Patch);
			hasFailed |= failedCreateFileA;
			hasFailed |= failedCreateFileW;
			hasFailed |= failedGetFileAttributesA;
			hasFailed |= failedGetFileAttributesW;
			hasFailed |= failedGetFileAttributesExA;
			hasFailed |= failedGetFileAttributesExW;
			hasFailed |= failedReadFile;
			hasFailed |= failedSetFilePointer;
			hasFailed |= failedSetFilePointerEx;
			hasFailed |= failedGetFileSize;
			hasFailed |= failedGetFileSizeEx;
			hasFailed |= failedGetFileInformationByHandle;
			hasFailed |= failedGetFileType;
			hasFailed |= failedCloseHandle;
			hasFailed |= failedCreateFileMappingW;
			hasFailed |= failedCreateFileMappingA;
			hasFailed |= failedFindFirstFileA;
			hasFailed |= failedFindFirstFileW;
			hasFailed |= failedFindNextFileA;
			hasFailed |= failedFindNextFileW;
			hasFailed |= failedFindClose;
			if (sg_bEnableLog || sg_bCustomPakLog || sg_bEnableSpoofLog)
			{
				LogMessage(LogLevel::Info, L"[FileHook] API Attach CreateFileA=%s CreateFileW=%s GetFileAttributesA=%s GetFileAttributesW=%s GetFileAttributesExA=%s GetFileAttributesExW=%s ReadFile=%s SetFilePointer=%s SetFilePointerEx=%s GetFileSize=%s GetFileSizeEx=%s GetFileInformationByHandle=%s GetFileType=%s CloseHandle=%s CreateFileMappingW=%s CreateFileMappingA=%s FindFirstFileA=%s FindFirstFileW=%s FindNextFileA=%s FindNextFileW=%s FindClose=%s",
					failedCreateFileA ? L"failed" : L"ok",
					failedCreateFileW ? L"failed" : L"ok",
					failedGetFileAttributesA ? L"failed" : L"ok",
					failedGetFileAttributesW ? L"failed" : L"ok",
					failedGetFileAttributesExA ? L"failed" : L"ok",
					failedGetFileAttributesExW ? L"failed" : L"ok",
					failedReadFile ? L"failed" : L"ok",
					failedSetFilePointer ? L"failed" : L"ok",
					failedSetFilePointerEx ? L"failed" : L"ok",
					failedGetFileSize ? L"failed" : L"ok",
					failedGetFileSizeEx ? L"failed" : L"ok",
					failedGetFileInformationByHandle ? L"failed" : L"ok",
					failedGetFileType ? L"failed" : L"ok",
					failedCloseHandle ? L"failed" : L"ok",
					failedCreateFileMappingW ? L"failed" : L"ok",
					failedCreateFileMappingA ? L"failed" : L"ok",
					failedFindFirstFileA ? L"failed" : L"ok",
					failedFindFirstFileW ? L"failed" : L"ok",
					failedFindNextFileA ? L"failed" : L"ok",
					failedFindNextFileW ? L"failed" : L"ok",
					failedFindClose ? L"failed" : L"ok");
			}
			return !hasFailed;
		}

		bool UnhookFileAPIs()
		{
			bool hasFailed = false;
			bool failedCreateFileA = rawCreateFileA_Patch == CreateFileA ? false : !TryDetourDetach(&rawCreateFileA_Patch, newCreateFileA_Patch);
			bool failedCreateFileW = rawCreateFileW_Patch == CreateFileW ? false : !TryDetourDetach(&rawCreateFileW_Patch, newCreateFileW_Patch);
			bool failedGetFileAttributesA = rawGetFileAttributesA_Patch == GetFileAttributesA ? false : !TryDetourDetach(&rawGetFileAttributesA_Patch, newGetFileAttributesA_Patch);
			bool failedGetFileAttributesW = rawGetFileAttributesW_Patch == GetFileAttributesW ? false : !TryDetourDetach(&rawGetFileAttributesW_Patch, newGetFileAttributesW_Patch);
			bool failedGetFileAttributesExA = rawGetFileAttributesExA_Patch == GetFileAttributesExA ? false : !TryDetourDetach(&rawGetFileAttributesExA_Patch, newGetFileAttributesExA_Patch);
			bool failedGetFileAttributesExW = rawGetFileAttributesExW_Patch == GetFileAttributesExW ? false : !TryDetourDetach(&rawGetFileAttributesExW_Patch, newGetFileAttributesExW_Patch);
			bool failedReadFile = rawReadFile_Patch == ReadFile ? false : !TryDetourDetach(&rawReadFile_Patch, newReadFile_Patch);
			bool failedSetFilePointer = rawSetFilePointer_Patch == SetFilePointer ? false : !TryDetourDetach(&rawSetFilePointer_Patch, newSetFilePointer_Patch);
			bool failedSetFilePointerEx = rawSetFilePointerEx_Patch == SetFilePointerEx ? false : !TryDetourDetach(&rawSetFilePointerEx_Patch, newSetFilePointerEx_Patch);
			bool failedGetFileSize = rawGetFileSize_Patch == GetFileSize ? false : !TryDetourDetach(&rawGetFileSize_Patch, newGetFileSize_Patch);
			bool failedGetFileSizeEx = rawGetFileSizeEx_Patch == GetFileSizeEx ? false : !TryDetourDetach(&rawGetFileSizeEx_Patch, newGetFileSizeEx_Patch);
			bool failedGetFileInformationByHandle = rawGetFileInformationByHandle_Patch == GetFileInformationByHandle ? false : !TryDetourDetach(&rawGetFileInformationByHandle_Patch, newGetFileInformationByHandle_Patch);
			bool failedGetFileType = rawGetFileType_Patch == GetFileType ? false : !TryDetourDetach(&rawGetFileType_Patch, newGetFileType_Patch);
			bool failedCloseHandle = rawCloseHandle_Patch == CloseHandle ? false : !TryDetourDetach(&rawCloseHandle_Patch, newCloseHandle_Patch);
			bool failedCreateFileMappingW = rawCreateFileMappingW_Patch == CreateFileMappingW ? false : !TryDetourDetach(&rawCreateFileMappingW_Patch, newCreateFileMappingW_Patch);
			bool failedCreateFileMappingA = rawCreateFileMappingA_Patch == CreateFileMappingA ? false : !TryDetourDetach(&rawCreateFileMappingA_Patch, newCreateFileMappingA_Patch);
			bool failedFindFirstFileA = rawFindFirstFileA_Patch == FindFirstFileA ? false : !TryDetourDetach(&rawFindFirstFileA_Patch, newFindFirstFileA_Patch);
			bool failedFindFirstFileW = rawFindFirstFileW_Patch == FindFirstFileW ? false : !TryDetourDetach(&rawFindFirstFileW_Patch, newFindFirstFileW_Patch);
			bool failedFindNextFileA = rawFindNextFileA_Patch == FindNextFileA ? false : !TryDetourDetach(&rawFindNextFileA_Patch, newFindNextFileA_Patch);
			bool failedFindNextFileW = rawFindNextFileW_Patch == FindNextFileW ? false : !TryDetourDetach(&rawFindNextFileW_Patch, newFindNextFileW_Patch);
			bool failedFindClose = rawFindClose_Patch == FindClose ? false : !TryDetourDetach(&rawFindClose_Patch, newFindClose_Patch);
			hasFailed |= failedCreateFileA;
			hasFailed |= failedCreateFileW;
			hasFailed |= failedGetFileAttributesA;
			hasFailed |= failedGetFileAttributesW;
			hasFailed |= failedGetFileAttributesExA;
			hasFailed |= failedGetFileAttributesExW;
			hasFailed |= failedReadFile;
			hasFailed |= failedSetFilePointer;
			hasFailed |= failedSetFilePointerEx;
			hasFailed |= failedGetFileSize;
			hasFailed |= failedGetFileSizeEx;
			hasFailed |= failedGetFileInformationByHandle;
			hasFailed |= failedGetFileType;
			hasFailed |= failedCloseHandle;
			hasFailed |= failedCreateFileMappingW;
			hasFailed |= failedCreateFileMappingA;
			hasFailed |= failedFindFirstFileA;
			hasFailed |= failedFindFirstFileW;
			hasFailed |= failedFindNextFileA;
			hasFailed |= failedFindNextFileW;
			hasFailed |= failedFindClose;
			if (sg_bEnableLog || sg_bCustomPakLog || sg_bEnableSpoofLog)
			{
				LogMessage(LogLevel::Info, L"[FileHook] API Detach CreateFileA=%s CreateFileW=%s GetFileAttributesA=%s GetFileAttributesW=%s GetFileAttributesExA=%s GetFileAttributesExW=%s ReadFile=%s SetFilePointer=%s SetFilePointerEx=%s GetFileSize=%s GetFileSizeEx=%s GetFileInformationByHandle=%s GetFileType=%s CloseHandle=%s CreateFileMappingW=%s CreateFileMappingA=%s FindFirstFileA=%s FindFirstFileW=%s FindNextFileA=%s FindNextFileW=%s FindClose=%s",
					failedCreateFileA ? L"failed" : L"ok",
					failedCreateFileW ? L"failed" : L"ok",
					failedGetFileAttributesA ? L"failed" : L"ok",
					failedGetFileAttributesW ? L"failed" : L"ok",
					failedGetFileAttributesExA ? L"failed" : L"ok",
					failedGetFileAttributesExW ? L"failed" : L"ok",
					failedReadFile ? L"failed" : L"ok",
					failedSetFilePointer ? L"failed" : L"ok",
					failedSetFilePointerEx ? L"failed" : L"ok",
					failedGetFileSize ? L"failed" : L"ok",
					failedGetFileSizeEx ? L"failed" : L"ok",
					failedGetFileInformationByHandle ? L"failed" : L"ok",
					failedGetFileType ? L"failed" : L"ok",
					failedCloseHandle ? L"failed" : L"ok",
					failedCreateFileMappingW ? L"failed" : L"ok",
					failedCreateFileMappingA ? L"failed" : L"ok",
					failedFindFirstFileA ? L"failed" : L"ok",
					failedFindFirstFileW ? L"failed" : L"ok",
					failedFindNextFileA ? L"failed" : L"ok",
					failedFindNextFileW ? L"failed" : L"ok",
					failedFindClose ? L"failed" : L"ok");
			}
			return !hasFailed;
		}
		//*********END File Hot-Patch*********
