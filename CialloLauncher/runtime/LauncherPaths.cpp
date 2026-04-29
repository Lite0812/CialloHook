#include "LauncherPaths.h"

#include "../../RuntimeCore/io/File.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/hook/LocaleEmulatorSupport.h"

#include <shellapi.h>

using namespace Rut::FileX;
using namespace Rut::HookX;

namespace
{
	std::wstring GetLowerFileName(const std::wstring& path)
	{
		size_t pos = path.find_last_of(L"\\/");
		std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
		for (wchar_t& c : name)
		{
			c = static_cast<wchar_t>(towlower(c));
		}
		return name;
	}

	std::wstring CanonicalizeInjectDllPath(const std::wstring& rawDllName, const std::wstring& exeDir)
	{
		if (rawDllName.empty())
		{
			return rawDllName;
		}

		std::wstring dllName = rawDllName;
		std::wstring lowerName = GetLowerFileName(dllName);
		if (lowerName == L"version.dll" || lowerName == L"winmm.dll")
		{
			std::wstring cialloHookDll = CialloLauncher::JoinPath(exeDir, L"CialloHook.dll");
			if (CialloLauncher::FileExists(cialloHookDll))
			{
				return cialloHookDll;
			}
		}

		if (CialloLauncher::IsAbsolutePath(dllName))
		{
			return dllName;
		}

		std::wstring dllInExeDir = CialloLauncher::JoinPath(exeDir, dllName);
		if (CialloLauncher::FileExists(dllInExeDir))
		{
			return dllInExeDir;
		}
		return dllName;
	}

	std::wstring ResolveDefaultHookDllPathLocal(const std::wstring& exeDir)
	{
		std::wstring cialloHookDll = CialloLauncher::JoinPath(exeDir, L"CialloHook.dll");
		if (CialloLauncher::FileExists(cialloHookDll))
		{
			return cialloHookDll;
		}
		wchar_t currentDirBuffer[MAX_PATH] = {};
		GetCurrentDirectoryW(MAX_PATH, currentDirBuffer);
		std::wstring currentDirDll = CialloLauncher::JoinPath(currentDirBuffer, L"CialloHook.dll");
		if (CialloLauncher::FileExists(currentDirDll))
		{
			return currentDirDll;
		}
		return L"";
	}

	std::wstring QuoteCommandLineArg(const std::wstring& value)
	{
		std::wstring quoted = L"\"";
		for (wchar_t ch : value)
		{
			if (ch == L'"')
			{
				quoted += L"\\\"";
			}
			else
			{
				quoted += ch;
			}
		}
		quoted += L"\"";
		return quoted;
	}

	std::wstring& GetCustomPakSessionRootStorage()
	{
		static std::wstring sessionRoot;
		return sessionRoot;
	}

	bool TryPrepareInjectDllFromCustomPak(const std::wstring& rawDllName, std::wstring& preparedPath, bool& foundCandidate)
	{
		preparedPath.clear();
		foundCandidate = false;
		if (rawDllName.empty())
		{
			return false;
		}

		std::wstring lookupPath = rawDllName;
		std::wstring lowerName = GetLowerFileName(lookupPath);
		if (lowerName == L"version.dll" || lowerName == L"winmm.dll")
		{
			lookupPath = L"CialloHook.dll";
		}

		std::wstring fileName = PathGetFileName(lookupPath);
		std::vector<std::wstring> candidates;
		candidates.emplace_back(lookupPath);
		if (!fileName.empty() && fileName != lookupPath)
		{
			candidates.emplace_back(fileName);
		}
		if (!fileName.empty() && !CialloLauncher::IsAbsolutePath(lookupPath))
		{
			candidates.emplace_back(std::wstring(L"dll\\") + fileName);
			candidates.emplace_back(std::wstring(L"dlls\\") + fileName);
			candidates.emplace_back(std::wstring(L"plugins\\") + fileName);
		}

		std::wstring tempDir = CialloLauncher::GetCustomPakCacheDir(L"InjectDll");
		for (const std::wstring& candidate : candidates)
		{
			bool currentFoundCandidate = false;
			if (ExtractCustomPakFileToTemp(candidate, tempDir, L"Launcher", preparedPath, currentFoundCandidate))
			{
				return true;
			}
			foundCandidate = foundCandidate || currentFoundCandidate;
		}
		if (foundCandidate && tempDir.empty())
		{
			LogMessage(LogLevel::Error, L"InjectDll extraction failed: runtime cache dir unavailable for %s", rawDllName.c_str());
		}
		return false;
	}
}

namespace CialloLauncher
{
	bool FileExists(const std::wstring& path)
	{
		DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	std::wstring GetDirectoryPath(const std::wstring& path)
	{
		size_t pos = path.find_last_of(L"\\/");
		if (pos == std::wstring::npos)
		{
			return L"";
		}
		return path.substr(0, pos + 1);
	}

	std::wstring JoinPath(const std::wstring& dir, const std::wstring& file)
	{
		if (dir.empty())
		{
			return file;
		}
		wchar_t last = dir.back();
		if (last == L'\\' || last == L'/')
		{
			return dir + file;
		}
		return dir + L"\\" + file;
	}

	bool IsAbsolutePath(const std::wstring& path)
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

	std::wstring ResolveLauncherIniPath(const std::wstring& exeDir, const std::wstring& exeNameNoExt)
	{
		std::wstring sameNameIni = JoinPath(exeDir, exeNameNoExt + L".ini");
		if (FileExists(sameNameIni))
		{
			return sameNameIni;
		}
		return JoinPath(exeDir, L"CialloHook.ini");
	}

	std::wstring ResolveInjectDllPath(const std::wstring& rawDllName, const std::wstring& exeDir, bool customPakEnable, bool& fromCustomPak, bool& customPakHadCandidate)
	{
		fromCustomPak = false;
		customPakHadCandidate = false;
		std::wstring canonicalDll = CanonicalizeInjectDllPath(rawDllName, exeDir);
		if (FileExists(canonicalDll))
		{
			return canonicalDll;
		}
		if (customPakEnable)
		{
			std::wstring extractedDll;
			if (TryPrepareInjectDllFromCustomPak(rawDllName, extractedDll, customPakHadCandidate))
			{
				fromCustomPak = true;
				return extractedDll;
			}
		}
		return FileExists(canonicalDll) ? canonicalDll : L"";
	}

	std::wstring ResolveDefaultHookDllPath(const std::wstring& exeDir, bool customPakEnable, bool& fromCustomPak)
	{
		fromCustomPak = false;
		std::wstring localDll = ResolveDefaultHookDllPathLocal(exeDir);
		if (!localDll.empty())
		{
			return localDll;
		}
		if (customPakEnable)
		{
			std::wstring extractedDll;
			bool customPakHadCandidate = false;
			if (TryPrepareInjectDllFromCustomPak(L"CialloHook.dll", extractedDll, customPakHadCandidate))
			{
				fromCustomPak = true;
				return extractedDll;
			}
		}
		return L"";
	}

	void ConfigureLauncherCustomPakVfs(const std::vector<std::wstring>& customPakFiles, bool debugMode)
	{
		if (customPakFiles.empty())
		{
			return;
		}
		std::vector<const wchar_t*> pakPaths;
		pakPaths.reserve(customPakFiles.size());
		for (const std::wstring& pakPath : customPakFiles)
		{
			pakPaths.push_back(pakPath.c_str());
		}
		ConfigureCustomPakVFS(true, pakPaths.data(), pakPaths.size(), debugMode);
		LogMessage(LogLevel::Info, L"CustomPak configured for launcher: count=%u", static_cast<uint32_t>(customPakFiles.size()));
	}

	std::wstring PeekCustomPakSessionRoot()
	{
		return GetCustomPakSessionRootStorage();
	}

	std::wstring GetCustomPakCacheDir(const wchar_t* subDirName)
	{
		std::wstring& sessionRoot = GetCustomPakSessionRootStorage();
		if (sessionRoot.empty())
		{
			wchar_t exePath[MAX_PATH] = {};
			if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
			{
				return L"";
			}
			std::wstring fallbackBaseDir = PathRemoveFileName(exePath);
			std::wstring cacheRoot = GetRuntimeCacheDir(fallbackBaseDir.empty() ? nullptr : fallbackBaseDir.c_str(), L"CustomPak");
			if (cacheRoot.empty())
			{
				return L"";
			}
			wchar_t sessionName[64] = {};
			swprintf_s(sessionName, L"%08X_%08X", GetCurrentProcessId(), GetTickCount());
			sessionRoot = JoinPath(cacheRoot, sessionName);
			CreateDirectoryW(sessionRoot.c_str(), nullptr);
		}

		if (!subDirName || subDirName[0] == L'\0')
		{
			return sessionRoot;
		}
		std::wstring cacheDir = JoinPath(sessionRoot, subDirName);
		CreateDirectoryW(cacheDir.c_str(), nullptr);
		return cacheDir;
	}

	bool DeleteDirectoryRecursively(const std::wstring& dirPath)
	{
		if (dirPath.empty())
		{
			return true;
		}
		DWORD attributes = GetFileAttributesW(dirPath.c_str());
		if (attributes == INVALID_FILE_ATTRIBUTES)
		{
			return true;
		}
		std::wstring from = dirPath;
		from.push_back(L'\0');
		SHFILEOPSTRUCTW fileOp = {};
		fileOp.wFunc = FO_DELETE;
		fileOp.pFrom = from.c_str();
		fileOp.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
		return SHFileOperationW(&fileOp) == 0;
	}

	void CleanupCustomPakCacheAfterProcessExit(DWORD processId, const std::wstring& cacheDir)
	{
		if (cacheDir.empty())
		{
			return;
		}
		HANDLE processHandle = OpenProcess(SYNCHRONIZE, FALSE, processId);
		if (processHandle)
		{
			WaitForSingleObject(processHandle, INFINITE);
			CloseHandle(processHandle);
		}
		for (int attempt = 0; attempt < 8; ++attempt)
		{
			if (DeleteDirectoryRecursively(cacheDir))
			{
				break;
			}
			Sleep(500);
		}
	}

	bool LaunchCustomPakCacheCleaner(const std::wstring& selfPath, DWORD processId, const std::wstring& cacheDir)
	{
		if (selfPath.empty() || cacheDir.empty() || processId == 0)
		{
			return false;
		}
		std::wstring commandLine = QuoteCommandLineArg(selfPath) + L" --cleanup-cache " + std::to_wstring(processId) + L" " + QuoteCommandLineArg(cacheDir);
		std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
		commandLineBuffer.push_back(L'\0');
		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo = {};
		BOOL created = CreateProcessW(selfPath.c_str(), commandLineBuffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
		if (!created)
		{
			return false;
		}
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return true;
	}
}
