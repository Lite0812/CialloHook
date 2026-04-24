#pragma once

#include <Windows.h>

#include <string>
#include <vector>

namespace CialloLauncher
{
	bool FileExists(const std::wstring& path);
	std::wstring GetDirectoryPath(const std::wstring& path);
	std::wstring JoinPath(const std::wstring& dir, const std::wstring& file);
	bool IsAbsolutePath(const std::wstring& path);
	std::wstring ResolveLauncherIniPath(const std::wstring& exeDir, const std::wstring& exeNameNoExt);
	std::wstring ResolveInjectDllPath(const std::wstring& rawDllName, const std::wstring& exeDir, bool customPakEnable, bool& fromCustomPak, bool& customPakHadCandidate);
	std::wstring ResolveDefaultHookDllPath(const std::wstring& exeDir, bool customPakEnable, bool& fromCustomPak);
	void ConfigureLauncherCustomPakVfs(const std::vector<std::wstring>& customPakFiles, bool debugMode);
	std::wstring PeekCustomPakSessionRoot();
	std::wstring GetCustomPakCacheDir(const wchar_t* subDirName);
	bool DeleteDirectoryRecursively(const std::wstring& dirPath);
	void CleanupCustomPakCacheAfterProcessExit(DWORD processId, const std::wstring& cacheDir);
	bool LaunchCustomPakCacheCleaner(const std::wstring& selfPath, DWORD processId, const std::wstring& cacheDir);
}
