#include <Windows.h>
#include <shellapi.h>

#include <cwctype>
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
	const wchar_t* kStartupMessageAcceptedEnvVar = L"CIALLOHOOK_STARTUP_MESSAGE_ACCEPTED";

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

	std::wstring GetEnvironmentVariableString(const wchar_t* name, bool& exists)
	{
		exists = false;
		if (!name || name[0] == L'\0')
		{
			return L"";
		}

		DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
		if (required == 0)
		{
			return L"";
		}

		std::wstring value(required - 1, L'\0');
		if (GetEnvironmentVariableW(name, &value[0], required) == 0)
		{
			return L"";
		}

		exists = true;
		return value;
	}

	class ScopedEnvironmentVariableOverride
	{
	public:
		ScopedEnvironmentVariableOverride() = default;

		~ScopedEnvironmentVariableOverride()
		{
			if (!name_.empty())
			{
				SetEnvironmentVariableW(name_.c_str(), hadOriginalValue_ ? originalValue_.c_str() : nullptr);
			}
		}

		void Set(const wchar_t* name, const wchar_t* value)
		{
			if (!name || name[0] == L'\0')
			{
				return;
			}

			name_ = name;
			originalValue_ = GetEnvironmentVariableString(name, hadOriginalValue_);
			SetEnvironmentVariableW(name, value);
		}

	private:
		std::wstring name_;
		std::wstring originalValue_;
		bool hadOriginalValue_ = false;
	};

	uint64_t HashWideStringNoCase(const std::wstring& value)
	{
		uint64_t hash = 14695981039346656037ull;
		for (wchar_t ch : value)
		{
			const uint16_t lower = static_cast<uint16_t>(towlower(static_cast<wint_t>(ch)));
			hash ^= static_cast<uint8_t>(lower & 0xFF);
			hash *= 1099511628211ull;
			hash ^= static_cast<uint8_t>((lower >> 8) & 0xFF);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	std::wstring ResolveTargetExePathForLauncher(const std::wstring& exeDir, const std::wstring& targetExe)
	{
		if (targetExe.empty() || CialloLauncher::IsAbsolutePath(targetExe))
		{
			return targetExe;
		}
		return CialloLauncher::JoinPath(exeDir, targetExe);
	}

	std::wstring BuildLaunchMutexName(const std::wstring& targetExePath)
	{
		const std::wstring key = targetExePath.empty() ? L"CialloLauncher" : targetExePath;
		wchar_t mutexName[96] = {};
		swprintf_s(mutexName, L"Local\\CialloLauncher.Launch.%016llX",
			static_cast<unsigned long long>(HashWideStringNoCase(key)));
		return mutexName;
	}

	class ScopedLaunchMutex
	{
	public:
		explicit ScopedLaunchMutex(const std::wstring& name)
		{
			if (name.empty())
			{
				acquired_ = true;
				return;
			}

			handle_ = CreateMutexW(nullptr, TRUE, name.c_str());
			if (!handle_)
			{
				acquired_ = true;
				return;
			}

			if (GetLastError() == ERROR_ALREADY_EXISTS)
			{
				CloseHandle(handle_);
				handle_ = nullptr;
				acquired_ = false;
				return;
			}

			acquired_ = true;
		}

		~ScopedLaunchMutex()
		{
			if (handle_)
			{
				ReleaseMutex(handle_);
				CloseHandle(handle_);
			}
		}

		bool IsAcquired() const
		{
			return acquired_;
		}

	private:
		HANDLE handle_ = nullptr;
		bool acquired_ = false;
	};

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
	if (argv && argc >= 4 && _wcsicmp(argv[1], L"--startup-consent") == 0)
	{
		std::wstring title = argv[2];
		std::wstring body = argv[3];
		LocalFree(argv);
		const int consentResult = ShowExternalStartupConsentDialog(title.c_str(), body.c_str());
		return consentResult == IDYES ? 0 : 1;
	}
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

		const std::wstring resolvedTargetExe = ResolveTargetExePathForLauncher(exeDir, config.targetExe);
		ScopedLaunchMutex launchMutex(BuildLaunchMutexName(resolvedTargetExe));
		if (!launchMutex.IsAcquired())
		{
			LogMessage(LogLevel::Info, L"Launch already in progress, ignore duplicate click: %s",
				resolvedTargetExe.empty() ? config.targetExe.c_str() : resolvedTargetExe.c_str());
			return 0;
		}

		ScopedEnvironmentVariableOverride startupMessageAcceptedOverride;
		if (config.startupMessage.enable && !config.startupMessage.body.empty())
		{
			const int consentResult = ShowExternalStartupConsentDialog(
				config.startupMessage.title.c_str(),
				config.startupMessage.body.c_str());
			if (consentResult != IDYES)
			{
				LogMessage(LogLevel::Info, L"Launcher startup consent declined");
				return 0;
			}
			startupMessageAcceptedOverride.Set(kStartupMessageAcceptedEnvVar, L"1");
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
