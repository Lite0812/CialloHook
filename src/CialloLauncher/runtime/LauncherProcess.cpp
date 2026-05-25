#include "LauncherProcess.h"

#include "LauncherPaths.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/hook/LocaleEmulatorSupport.h"
#include "../../../third/detours/include/detours.h"

using namespace Rut::HookX;

namespace
{
	const wchar_t* kLocaleEmulatorStagedFilesEnvVar = L"CIALLOHOOK_LE_STAGED_FILES";

	void ShowDebugMessageIfNeeded(bool debugMode, const wchar_t* message)
	{
		if (debugMode)
		{
			MessageBoxW(nullptr, message, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}
	}

	void CloseProcessHandles(PROCESS_INFORMATION& processInfo)
	{
		if (processInfo.hThread)
		{
			CloseHandle(processInfo.hThread);
			processInfo.hThread = nullptr;
		}
		if (processInfo.hProcess)
		{
			CloseHandle(processInfo.hProcess);
			processInfo.hProcess = nullptr;
		}
	}

	bool StartCacheCleanerIfNeeded(const std::wstring& selfPath, DWORD processId, bool& cleanupPending)
	{
		std::wstring cacheRoot = CialloLauncher::PeekCustomPakSessionRoot();
		if (cacheRoot.empty())
		{
			return true;
		}
		if (!CialloLauncher::LaunchCustomPakCacheCleaner(selfPath, processId, cacheRoot))
		{
			LogMessage(LogLevel::Warn, L"Failed to launch cache cleaner for: %s", cacheRoot.c_str());
			return false;
		}
		cleanupPending = true;
		return true;
	}

	std::vector<LPCSTR> BuildDllPointerArray(const std::vector<std::string>& dllList)
	{
		std::vector<LPCSTR> dllArray;
		dllArray.reserve(dllList.size());
		for (const std::string& dllPath : dllList)
		{
			dllArray.push_back(dllPath.c_str());
		}
		return dllArray;
	}

	std::wstring ResolveTargetExePath(const std::wstring& selfPath, const std::wstring& targetExe)
	{
		if (targetExe.empty())
		{
			return targetExe;
		}
		if (CialloLauncher::IsAbsolutePath(targetExe))
		{
			return targetExe;
		}
		return CialloLauncher::JoinPath(CialloLauncher::GetDirectoryPath(selfPath), targetExe);
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

	bool IsExistingFilePath(const std::wstring& path)
	{
		if (path.empty())
		{
			return false;
		}
		DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	bool IsSamePathIgnoreCase(const std::wstring& left, const std::wstring& right)
	{
		return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	std::wstring FindLocalLocaleEmulatorDll(HMODULE loaderModule, const std::wstring& selfDir, const std::wstring& workDir)
	{
		std::wstring resolvedPath;
		wchar_t loaderPath[MAX_PATH] = {};
		if (loaderModule && GetModuleFileNameW(loaderModule, loaderPath, MAX_PATH))
		{
			if (TryResolveLocalLocaleEmulatorDependency(CialloLauncher::GetDirectoryPath(loaderPath), L"LocaleEmulator.dll", resolvedPath))
			{
				return resolvedPath;
			}
		}
		if (TryResolveLocalLocaleEmulatorDependency(selfDir, L"LocaleEmulator.dll", resolvedPath))
		{
			return resolvedPath;
		}
		if (!IsSamePathIgnoreCase(selfDir, workDir) && TryResolveLocalLocaleEmulatorDependency(workDir, L"LocaleEmulator.dll", resolvedPath))
		{
			return resolvedPath;
		}
		return L"";
	}
}

namespace CialloLauncher
{
	bool LaunchWithLocaleEmulator(
		const LauncherConfig& config,
		const std::wstring& selfPath,
		const std::vector<std::string>& dllList,
		bool& cleanupPending)
	{
		cleanupPending = false;
		if (config.debugMode)
		{
			MessageBoxW(nullptr, L"[Debug] Using LeCreateProcess mode\n\nWARNING: User DLLs (like version.dll) will NOT be injected!\nOnly LE will be active.", L"CialloHook - Debug", MB_OK | MB_ICONWARNING);
		}

		STARTUPINFOW si = {};
		ML_PROCESS_INFORMATION pi = {};
		si.cb = sizeof(si);
		std::wstring targetExePath = ResolveTargetExePath(selfPath, config.targetExe);
		if (!IsExistingFilePath(targetExePath))
		{
			std::wstring message = L"目标 EXE 不存在：\n" + targetExePath;
			LogMessage(LogLevel::Error, L"%s", message.c_str());
			MessageBoxW(nullptr, message.c_str(), L"CialloHook - Error", MB_OK | MB_ICONERROR);
			return false;
		}
		std::wstring workDir = GetDirectoryPath(targetExePath);
		if (workDir.empty())
		{
			workDir = GetDirectoryPath(selfPath);
		}
		std::wstring commandLine = QuoteCommandLineArg(targetExePath);

		bool loaderFromCustomPak = false;
		bool loaderCustomPakCandidateFound = false;
		bool localeCandidateFound = false;
		std::vector<std::wstring> preparedPaths;
		std::vector<std::wstring> runtimeSearchDirs;
		std::vector<std::wstring> stagedPaths;
		LocaleEmulatorLoaderOptions loaderOptions;
		loaderOptions.tempDir = GetCustomPakCacheDir(L"LocaleEmulator");
		loaderOptions.patchBaseDir = GetDirectoryPath(selfPath);
		loaderOptions.patchFolders = config.patchFolders.empty() ? nullptr : &config.patchFolders;
		loaderOptions.logPrefix = L"Launcher";
		loaderOptions.preferLocalLoader = true;

		HMODULE hLoaderDll = LoadLocaleEmulatorLoaderModule(
			loaderOptions,
			loaderFromCustomPak,
			loaderCustomPakCandidateFound,
			preparedPaths,
			&runtimeSearchDirs,
			&localeCandidateFound);
		if (!hLoaderDll)
		{
			DWORD err = GetLastError();
			LogMessage(LogLevel::Error, L"Cannot load LoaderDll.dll, error=0x%08X", err);
			wchar_t msg[512];
			if (loaderCustomPakCandidateFound)
			{
				swprintf_s(msg, 512, L"无法加载 LoaderDll.dll。\nError: 0x%08X\n\n已在 FilePatch / CustomPak 中找到候选 LE 依赖，但运行时加载失败。", err);
			}
			else
			{
				swprintf_s(msg, 512, L"无法加载 LoaderDll.dll。\nError: 0x%08X\n\n请检查游戏目录或补丁资源中是否提供了 LoaderDll.dll。", err);
			}
			MessageBoxW(nullptr, msg, L"CialloHook - Error", MB_OK | MB_ICONERROR);
			return false;
		}

		if (loaderFromCustomPak)
		{
			LogMessage(LogLevel::Info, L"LoaderDll.dll loaded from CustomPak cache");
		}
		ShowDebugMessageIfNeeded(config.debugMode, L"[Debug] LoaderDll.dll loaded successfully\nFinding LeCreateProcess function");

		PFN_LeCreateProcess leCreateProcess = reinterpret_cast<PFN_LeCreateProcess>(GetProcAddress(hLoaderDll, "LeCreateProcess"));
		if (!leCreateProcess)
		{
			LogMessage(LogLevel::Error, L"Cannot find LeCreateProcess in LoaderDll.dll");
			MessageBoxW(nullptr, L"LoaderDll.dll 中缺少 LeCreateProcess 导出，无法执行自动转区。", L"CialloHook - Error", MB_OK | MB_ICONERROR);
			FreeLibrary(hLoaderDll);
			CleanupPreparedRuntimeFiles(preparedPaths);
			return false;
		}

		const std::wstring selfDir = GetDirectoryPath(selfPath);
		const std::wstring localLocaleDllPath = FindLocalLocaleEmulatorDll(hLoaderDll, selfDir, workDir);
		if (!localeCandidateFound && localLocaleDllPath.empty())
		{
			LogMessage(LogLevel::Error, L"LocaleEmulator.dll is unavailable for LE launch");
			MessageBoxW(nullptr, L"未找到可用的 LocaleEmulator.dll，无法执行自动转区。", L"CialloHook - Error", MB_OK | MB_ICONERROR);
			FreeLibrary(hLoaderDll);
			CleanupPreparedRuntimeFiles(preparedPaths);
			return false;
		}

		struct
		{
			LEB leb;
			uint64_t numberOfRegistryEntries;
		} lebWithRegistry = {};
		lebWithRegistry.leb = config.localeEmulatorBlock;
		lebWithRegistry.numberOfRegistryEntries = 0;

		if (config.debugMode)
		{
			wchar_t msg[512];
			swprintf_s(msg, 512, L"[Debug] Calling LeCreateProcess\nTarget: %s\nLEB address: 0x%p\nRegistry entries: %llu",
				targetExePath.c_str(), &lebWithRegistry, lebWithRegistry.numberOfRegistryEntries);
			MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		std::wstring originalStagedFilesValue;
		bool originalStagedFilesValueExists = false;
		if (!preparedPaths.empty())
		{
			LocaleEmulatorFileStageOptions stageOptions;
			stageOptions.logPrefix = L"Launcher";
			stageOptions.logSkipIfTargetExists = true;
			StageLocaleEmulatorFilesNextToExe(preparedPaths, workDir, stageOptions, stagedPaths);
			if (!stagedPaths.empty())
			{
				AppendUniquePath(runtimeSearchDirs, workDir);
				const std::wstring stagedValue = BuildDelimitedEnvironmentValue(stagedPaths);
				if (!stagedValue.empty())
				{
					originalStagedFilesValue = GetEnvironmentVariableString(kLocaleEmulatorStagedFilesEnvVar, originalStagedFilesValueExists);
					SetEnvironmentVariableW(kLocaleEmulatorStagedFilesEnvVar, stagedValue.c_str());
				}
				LogMessage(LogLevel::Info, L"Launcher: staged %u runtime file(s) next to target exe for LE launch",
					static_cast<uint32_t>(stagedPaths.size()));
			}
		}

		std::wstring originalPath;
		bool originalPathExists = false;
		const std::wstring launchPath = BuildPrependedPathValue(runtimeSearchDirs, GetEnvironmentVariableString(L"PATH", originalPathExists));
		std::vector<wchar_t> environmentBlock;
		const bool environmentBlockReady = BuildEnvironmentBlockWithPath(launchPath, environmentBlock);
		const bool pathUpdated = PrependDirectoriesToPath(runtimeSearchDirs, originalPath, originalPathExists);
		if (!runtimeSearchDirs.empty() && !pathUpdated)
		{
			LogMessage(LogLevel::Warn, L"Failed to prepend LocaleEmulator runtime search dirs to PATH");
		}
		if (!runtimeSearchDirs.empty() && !environmentBlockReady)
		{
			LogMessage(LogLevel::Warn, L"Failed to build environment block with LocaleEmulator runtime search dirs");
		}

		const uint32_t creationFlags = CREATE_SUSPENDED | (environmentBlockReady ? CREATE_UNICODE_ENVIRONMENT : 0);
		uint32_t result = leCreateProcess(
			&lebWithRegistry,
			targetExePath.c_str(),
			commandLine.c_str(),
			workDir.c_str(),
			creationFlags,
			&si,
			&pi,
			nullptr,
			nullptr,
			environmentBlockReady ? environmentBlock.data() : nullptr,
			nullptr);

		if (pathUpdated)
		{
			RestorePathEnvironment(originalPath, originalPathExists);
		}
		SetEnvironmentVariableW(
			kLocaleEmulatorStagedFilesEnvVar,
			originalStagedFilesValueExists ? originalStagedFilesValue.c_str() : nullptr);

		if (config.debugMode)
		{
			wchar_t msg[512];
			swprintf_s(msg, 512, L"[Debug] LeCreateProcess returned\nResult: 0x%08X\nProcess: 0x%p\nThread: 0x%p\nPID: %d",
				result, pi.hProcess, pi.hThread, pi.dwProcessId);
			MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		bool success = false;
		if (result == 0)
		{
			success = true;
			LogMessage(LogLevel::Info, L"LeCreateProcess succeeded");

			if (config.debugMode)
			{
				MessageBoxW(nullptr, L"[Debug] LE injected successfully!\nProcess is SUSPENDED\nNow injecting user DLLs...", L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
			}

			if (!dllList.empty())
			{
				std::vector<LPCSTR> userDllArray = BuildDllPointerArray(dllList);
				if (config.debugMode)
				{
					wchar_t msg[256];
					swprintf_s(msg, 256, L"[Debug] Injecting %d user DLLs into suspended process...", static_cast<int>(userDllArray.size()));
					MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
				}

				if (!DetourUpdateProcessWithDll(pi.hProcess, userDllArray.data(), static_cast<DWORD>(userDllArray.size())))
				{
					DWORD err = GetLastError();
					LogMessage(LogLevel::Warn, L"Inject user DLL failed, error=0x%08X", err);
					if (config.debugMode)
					{
						wchar_t msg[512];
						swprintf_s(msg, 512, L"Failed to inject user DLLs!\nError: 0x%08X\n\nLE 已启动，但额外 DLL 注入失败。", err);
						MessageBoxW(nullptr, msg, L"CialloHook - Warning", MB_OK | MB_ICONWARNING);
					}
				}
				else if (config.debugMode)
				{
					wchar_t msg[256];
					swprintf_s(msg, 256, L"[Debug] Successfully injected %d user DLLs!", static_cast<int>(userDllArray.size()));
					MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
				}
				else
				{
					LogMessage(LogLevel::Info, L"Injected %u user DLLs", static_cast<uint32_t>(userDllArray.size()));
				}
			}
			else if (config.debugMode)
			{
				MessageBoxW(nullptr, L"[Debug] No user DLLs to inject\nResuming process...", L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
			}

			ResumeThread(pi.hThread);
			StartCacheCleanerIfNeeded(selfPath, pi.dwProcessId, cleanupPending);
			CloseProcessHandles(pi);

			ShowDebugMessageIfNeeded(config.debugMode, L"[Debug] Process resumed and running!\nLE + User DLLs active.");
		}
		else
		{
			LogMessage(LogLevel::Error, L"LeCreateProcess failed, result=0x%08X", result);
			wchar_t msg[512];
			swprintf_s(msg, 512, L"LeCreateProcess 启动失败。\nError Code: 0x%08X\n\n目标: %s\n依赖预检已通过，请检查 LE 运行环境或 DLL 是否损坏。", result, targetExePath.c_str());
			MessageBoxW(nullptr, msg, L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
		}

		FreeLibrary(hLoaderDll);
		CleanupPreparedRuntimeFiles(preparedPaths);
		if (!success)
		{
			CleanupPreparedRuntimeFiles(stagedPaths);
		}
		return success;
	}

	bool LaunchWithDetours(
		const LauncherConfig& config,
		const std::wstring& selfPath,
		const std::wstring& exeDir,
		const std::vector<std::string>& dllList,
		bool& cleanupPending)
	{
		cleanupPending = false;
		if (config.debugMode)
		{
			MessageBoxW(nullptr, L"[Debug] Using standard Detours mode\nUser DLLs will be injected", L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		STARTUPINFOW si = {};
		PROCESS_INFORMATION pi = {};
		si.cb = sizeof(si);

		std::wstring workDir = GetDirectoryPath(config.targetExe);
		if (workDir.empty())
		{
			workDir = exeDir;
		}

		if (!CreateProcessW(config.targetExe.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, workDir.c_str(), &si, &pi))
		{
			DWORD err = GetLastError();
			LogMessage(LogLevel::Error, L"CreateProcessW failed, error=0x%08X", err);
			wchar_t msg[512];
			swprintf_s(msg, 512, L"启动目标进程失败\nError: 0x%08X", err);
			MessageBoxW(nullptr, msg, L"CialloHook - Error", MB_OK | MB_ICONERROR);
			return false;
		}

		std::vector<LPCSTR> dllNameArray = BuildDllPointerArray(dllList);
		if (DetourUpdateProcessWithDll(pi.hProcess, dllNameArray.data(), static_cast<DWORD>(dllNameArray.size())))
		{
			LogMessage(LogLevel::Info, L"CreateProcessW + DetourUpdateProcessWithDll succeeded, DLLs=%u", static_cast<uint32_t>(dllNameArray.size()));
			ResumeThread(pi.hThread);
			StartCacheCleanerIfNeeded(selfPath, pi.dwProcessId, cleanupPending);
			CloseProcessHandles(pi);
			if (config.debugMode)
			{
				wchar_t msg[512];
				swprintf_s(msg, 512, L"[Debug] Process started successfully!\nDLLs injected: %d", static_cast<int>(dllNameArray.size()));
				MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
			}
			return true;
		}

		DWORD err = GetLastError();
		LogMessage(LogLevel::Error, L"DetourUpdateProcessWithDll failed, error=0x%08X", err);
		TerminateProcess(pi.hProcess, 0);
		CloseProcessHandles(pi);
		wchar_t msg[512];
		swprintf_s(msg, 512, L"注入失败（DetourUpdateProcessWithDll）\nError: 0x%08X", err);
		MessageBoxW(nullptr, msg, L"CialloHook - Error", MB_OK | MB_ICONERROR);
		return false;
	}
}
