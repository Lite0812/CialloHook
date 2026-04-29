#include <Windows.h>
#include <shellapi.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "../RuntimeCore/io/File.h"
#include "../RuntimeCore/hook/Hook_API.h"
#include "config/LauncherConfig.h"
#include "runtime/LauncherPaths.h"
#include "runtime/LauncherProcess.h"

using namespace Rut::FileX;
using namespace Rut::HookX;
using namespace CialloLauncher;

namespace
{
	std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty())
		{
			return L"";
		}
		int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
		if (count <= 0)
		{
			return L"";
		}
		std::wstring wide(count - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], count);
		return wide;
	}

	bool IsSamePathIgnoreCase(const std::wstring& left, const std::wstring& right)
	{
		return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	std::wstring NormalizeDirectoryPath(std::wstring path)
	{
		while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
		{
			path.pop_back();
		}
		return path;
	}

	std::wstring TryGetShortPath(const std::wstring& path)
	{
		if (path.empty())
		{
			return L"";
		}
		DWORD required = GetShortPathNameW(path.c_str(), nullptr, 0);
		if (required == 0)
		{
			return L"";
		}
		std::wstring shortPath(required, L'\0');
		DWORD written = GetShortPathNameW(path.c_str(), &shortPath[0], required);
		if (written == 0 || written >= required)
		{
			return L"";
		}
		shortPath.resize(written);
		return shortPath;
	}

	std::string WideToAnsiPath(const std::wstring& wide)
	{
		if (wide.empty())
		{
			return "";
		}
		int count = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (count <= 0)
		{
			return "";
		}
		std::string ansi(count, '\0');
		WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &ansi[0], count, nullptr, nullptr);
		if (!ansi.empty() && ansi.back() == '\0')
		{
			ansi.pop_back();
		}
		return ansi;
	}

	std::string BuildDetoursDllPath(const std::wstring& dllPath, const std::wstring& exeDir)
	{
		if (dllPath.empty())
		{
			return "";
		}

		const std::wstring normalizedExeDir = NormalizeDirectoryPath(exeDir);
		const std::wstring normalizedDllDir = NormalizeDirectoryPath(GetDirectoryPath(dllPath));
		const std::wstring fileName = PathGetFileName(dllPath);
		if (!fileName.empty() && IsSamePathIgnoreCase(normalizedDllDir, normalizedExeDir))
		{
			std::string localName = WideToAnsiPath(fileName);
			if (!localName.empty())
			{
				return localName;
			}
		}

		std::wstring shortPath = TryGetShortPath(dllPath);
		if (!shortPath.empty())
		{
			std::string ansiShortPath = WideToAnsiPath(shortPath);
			if (!ansiShortPath.empty())
			{
				return ansiShortPath;
			}
		}

		return WideToAnsiPath(dllPath);
	}

	bool BuildInjectionDllList(const LauncherConfig& config, const std::wstring& exeDir, std::vector<std::string>& dllList)
	{
		dllList.clear();
		dllList.reserve(config.targetDllNames.size() + 1);

		for (const std::wstring& configuredDllName : config.targetDllNames)
		{
			if (configuredDllName.empty())
			{
				continue;
			}

			bool fromCustomPak = false;
			bool customPakHadCandidate = false;
			std::wstring injectPath = ResolveInjectDllPath(configuredDllName, exeDir, config.customPakEnable, fromCustomPak, customPakHadCandidate);
			if (injectPath.empty())
			{
				std::wstring message = L"找不到需要注入的 DLL：\n" + configuredDllName;
				if (customPakHadCandidate)
				{
					message += L"\n\nCustomPak 中存在候选文件，但提取到运行时缓存失败。";
				}
				MessageBoxW(nullptr, message.c_str(), L"CialloHook - Error", MB_OK | MB_ICONERROR);
				LogMessage(LogLevel::Error, L"Inject DLL missing: %s", configuredDllName.c_str());
				return false;
			}

			std::string detoursPath = BuildDetoursDllPath(injectPath, exeDir);
			if (detoursPath.empty())
			{
				std::wstring message = L"无法为 Detours 编码 DLL 路径：\n" + injectPath;
				MessageBoxW(nullptr, message.c_str(), L"CialloHook - Error", MB_OK | MB_ICONERROR);
				LogMessage(LogLevel::Error, L"Detours DLL path encoding failed: %s", injectPath.c_str());
				return false;
			}
			dllList.emplace_back(detoursPath);
			LogMessage(LogLevel::Info, L"Inject DLL prepared: %s%s", injectPath.c_str(), fromCustomPak ? L" [CustomPak]" : L"");
		}

		bool defaultHookFromCustomPak = false;
		std::wstring defaultHookDll = ResolveDefaultHookDllPath(exeDir, config.customPakEnable, defaultHookFromCustomPak);
		if (defaultHookDll.empty())
		{
			MessageBoxW(nullptr, L"找不到 `CialloHook.dll`，无法继续启动。", L"CialloHook - Error", MB_OK | MB_ICONERROR);
			LogMessage(LogLevel::Error, L"CialloHook.dll not found");
			return false;
		}

		std::string defaultDetoursPath = BuildDetoursDllPath(defaultHookDll, exeDir);
		if (defaultDetoursPath.empty())
		{
			std::wstring message = L"无法为 Detours 编码默认 Hook DLL 路径：\n" + defaultHookDll;
			MessageBoxW(nullptr, message.c_str(), L"CialloHook - Error", MB_OK | MB_ICONERROR);
			LogMessage(LogLevel::Error, L"Default hook DLL path encoding failed: %s", defaultHookDll.c_str());
			return false;
		}
		dllList.emplace_back(defaultDetoursPath);
		LogMessage(LogLevel::Info, L"Default hook DLL prepared: %s%s", defaultHookDll.c_str(), defaultHookFromCustomPak ? L" [CustomPak]" : L"");
		return true;
	}
}

INT APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow)
{
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv && argc >= 4 && _wcsicmp(argv[1], L"--cleanup-cache") == 0)
	{
		DWORD processId = static_cast<DWORD>(wcstoul(argv[2], nullptr, 10));
		std::wstring cacheDir = argv[3];
		LocalFree(argv);
		CleanupCustomPakCacheAfterProcessExit(processId, cacheDir);
		return 0;
	}
	if (argv)
	{
		LocalFree(argv);
	}

	wchar_t exe_path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
	std::wstring selfPath = exe_path;
	std::wstring exeDir = GetDirectoryPath(selfPath);
	std::wstring exeNameNoExt = PathGetFileName(selfPath);
	exeNameNoExt = PathRemoveExtension(exeNameNoExt);

	try
	{
		LauncherConfig config;
		if (!LoadLauncherConfig(exeDir, exeNameNoExt, config))
		{
			return -1;
		}

		if (config.customPakEnable)
		{
			ConfigureLauncherCustomPakVfs(config.customPakFiles, config.debugMode);
		}

		if (config.debugMode)
		{
			wchar_t msg[768];
			swprintf_s(msg, 768, L"[Debug] Ready to launch\nTarget: %s\nInject DLL count: %u\nLocaleEmulator: %s\nCustomPak: %s",
				config.targetExe.c_str(),
				config.targetDllCount + 1,
				config.enableLocaleEmulator ? L"ON" : L"OFF",
				config.customPakEnable ? L"ON" : L"OFF");
			MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		std::vector<std::string> dllList;
		if (!BuildInjectionDllList(config, exeDir, dllList))
		{
			return -1;
		}

		bool cleanupPending = false;
		bool launchSucceeded = config.enableLocaleEmulator
			? LaunchWithLocaleEmulator(config, selfPath, dllList, cleanupPending)
			: LaunchWithDetours(config, selfPath, exeDir, dllList, cleanupPending);

		if (launchSucceeded)
		{
			LogMessage(LogLevel::Info, L"Launcher finished successfully");
		}
		else
		{
			LogMessage(LogLevel::Error, L"Launcher finished with failure");
		}

		if (!cleanupPending)
		{
			std::wstring cacheRoot = PeekCustomPakSessionRoot();
			if (!cacheRoot.empty())
			{
				DeleteDirectoryRecursively(cacheRoot);
			}
		}
	}
	catch (const std::exception& e)
	{
		std::wstring message = L"发生异常：\n";
		message += Utf8ToWide(e.what());
		LogMessage(LogLevel::Error, L"Exception: %s", message.c_str());
		MessageBoxW(nullptr, message.c_str(), L"CialloHook - Error", MB_OK | MB_ICONERROR);
		return -1;
	}
	catch (...)
	{
		LogMessage(LogLevel::Error, L"Unknown exception");
		MessageBoxW(nullptr, L"发生未知异常", L"CialloHook - Error", MB_OK | MB_ICONERROR);
		return -1;
	}

	return 0;
}
