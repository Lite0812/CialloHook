#include "hook_manager.h"

#include <string>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <vector>

#include "../../RuntimeCore/io/File.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/hook/LocaleEmulatorSupport.h"
#include "../config/config_manager.h"
#include "../hooks/hook_modules.h"

using namespace Rut::FileX;
using namespace Rut::HookX;

namespace CialloHook
{
	namespace
	{
		const wchar_t* kLocaleEmulatorStagedFilesEnvVar = L"CIALLOHOOK_LE_STAGED_FILES";
		std::vector<std::wstring> sg_localeEmulatorStagedCleanupPaths;
		volatile LONG sg_startupMessageHandledEarly = 0;
	}

	#pragma pack(push, 1)
	struct TIME_FIELDS
	{
		uint16_t Year;
		uint16_t Month;
		uint16_t Day;
		uint16_t Hour;
		uint16_t Minute;
		uint16_t Second;
		uint16_t Milliseconds;
		uint16_t Weekday;
	};

	struct RTL_TIME_ZONE_INFORMATION
	{
		int32_t Bias;
		wchar_t StandardName[32];
		TIME_FIELDS StandardDate;
		int32_t StandardBias;
		wchar_t DaylightName[32];
		TIME_FIELDS DaylightDate;
		int32_t DaylightBias;
	};

	struct LEB
	{
		uint32_t AnsiCodePage;
		uint32_t OemCodePage;
		uint32_t LocaleID;
		uint32_t DefaultCharset;
		uint32_t HookUILanguageAPI;
		wchar_t DefaultFaceName[LF_FACESIZE];
		RTL_TIME_ZONE_INFORMATION Timezone;
		uint64_t NumberOfRegistryRedirectionEntries;
	};
	#pragma pack(pop)

	struct ML_PROCESS_INFORMATION : PROCESS_INFORMATION
	{
		void* FirstCallLdrLoadDll;
	};

	struct REG_TZI_FORMAT
	{
		int32_t Bias;
		int32_t StandardBias;
		int32_t DaylightBias;
		SYSTEMTIME StandardDate;
		SYSTEMTIME DaylightDate;
	};

	using PFN_LeCreateProcess = DWORD(WINAPI*)(
		LEB* leb,
		const wchar_t* applicationName,
		const wchar_t* commandLine,
		const wchar_t* currentDirectory,
		uint32_t creationFlags,
		STARTUPINFOW* startupInfo,
		ML_PROCESS_INFORMATION* processInfo,
		LPSECURITY_ATTRIBUTES processAttributes,
		LPSECURITY_ATTRIBUTES threadAttributes,
		void* environment,
		HANDLE token);

	static std::wstring Utf8ToWide(const std::string& text)
	{
		if (text.empty())
		{
			return L"";
		}

		int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
		if (len > 0)
		{
			std::wstring result(len - 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &result[0], len);
			return result;
		}

		len = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
		if (len <= 0)
		{
			return L"配置读取失败";
		}

		std::wstring result(len - 1, L'\0');
		MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, &result[0], len);
		return result;
	}

	static std::wstring GetModulePath(HMODULE module)
	{
		wchar_t path[MAX_PATH] = {};
		if (!GetModuleFileNameW(module, path, MAX_PATH))
		{
			return L"";
		}
		return path;
	}

	static std::wstring GetModuleNameNoExtension(HMODULE module)
	{
		std::wstring modulePath = GetModulePath(module);
		if (modulePath.empty())
		{
			return L"CialloHook";
		}
		return PathRemoveExtension(PathGetFileName(modulePath));
	}

	static std::wstring JoinPath(const std::wstring& dir, const std::wstring& fileName)
	{
		if (dir.empty())
		{
			return fileName;
		}
		wchar_t last = dir.back();
		if (last == L'\\' || last == L'/')
		{
			return dir + fileName;
		}
		return dir + L"\\" + fileName;
	}

	static bool IsExistingFilePath(const std::wstring& path)
	{
		if (path.empty())
		{
			return false;
		}
		const DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	static void AppendUniqueString(std::vector<std::wstring>& values, const std::wstring& value)
	{
		if (value.empty())
		{
			return;
		}
		for (const std::wstring& existing : values)
		{
			if (_wcsicmp(existing.c_str(), value.c_str()) == 0)
			{
				return;
			}
		}
		values.push_back(value);
	}

	static std::wstring BuildDelimitedEnvironmentValue(const std::vector<std::wstring>& paths)
	{
		std::wstring value;
		for (const std::wstring& path : paths)
		{
			if (path.empty())
			{
				continue;
			}
			if (!value.empty())
			{
				value += L"|";
			}
			value += path;
		}
		return value;
	}

	static std::vector<std::wstring> ParseDelimitedEnvironmentValue(const std::wstring& value)
	{
		std::vector<std::wstring> paths;
		size_t start = 0;
		while (start <= value.size())
		{
			size_t end = value.find(L'|', start);
			std::wstring current = end == std::wstring::npos
				? value.substr(start)
				: value.substr(start, end - start);
			if (!current.empty())
			{
				AppendUniqueString(paths, current);
			}
			if (end == std::wstring::npos)
			{
				break;
			}
			start = end + 1;
		}
		return paths;
	}

	static bool IsSamePathIgnoreCase(const std::wstring& left, const std::wstring& right)
	{
		return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
	}

	static void StageLocaleEmulatorFilesNextToExe(
		const std::vector<std::wstring>& sourcePaths,
		const std::wstring& exeDir,
		std::vector<std::wstring>& stagedPaths)
	{
		stagedPaths.clear();
		if (exeDir.empty())
		{
			return;
		}

		for (const std::wstring& sourcePath : sourcePaths)
		{
			if (sourcePath.empty())
			{
				continue;
			}

			const std::wstring fileName = PathGetFileName(sourcePath);
			if (fileName.empty())
			{
				continue;
			}

			const std::wstring targetPath = JoinPath(exeDir, fileName);
			if (IsSamePathIgnoreCase(sourcePath, targetPath))
			{
				continue;
			}
			if (IsExistingFilePath(targetPath))
			{
				LogMessage(LogLevel::Info, L"LocaleEmulator: skip staging %s because target already exists at %s",
					sourcePath.c_str(), targetPath.c_str());
				continue;
			}

			if (!CopyFileW(sourcePath.c_str(), targetPath.c_str(), FALSE))
			{
				LogMessage(LogLevel::Warn, L"LocaleEmulator: failed to stage %s next to exe at %s (error=%u)",
					sourcePath.c_str(), targetPath.c_str(), GetLastError());
				continue;
			}

			stagedPaths.push_back(targetPath);
		}
	}

	static std::wstring ToLowerCopy(const std::wstring& text)
	{
		std::wstring lowerText = text;
		for (wchar_t& c : lowerText)
		{
			c = static_cast<wchar_t>(towlower(c));
		}
		return lowerText;
	}

	static bool FileExists(const std::wstring& path)
	{
		DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	static std::wstring ResolveIniPath(HMODULE module, const std::wstring& dllNameNoExt)
	{
		std::wstring modulePath = GetModulePath(module);
		std::wstring moduleDir = PathRemoveFileName(modulePath);
		std::wstring dllIniName = dllNameNoExt + L".ini";

		std::wstring candidate = JoinPath(moduleDir, dllIniName);
		if (FileExists(candidate))
		{
			return candidate;
		}

		candidate = JoinPath(moduleDir, L"CialloHook.ini");
		if (FileExists(candidate))
		{
			return candidate;
		}

		std::wstring currentDir = GetCurrentDirW();
		candidate = JoinPath(currentDir, dllIniName);
		if (FileExists(candidate))
		{
			return candidate;
		}

		candidate = JoinPath(currentDir, L"CialloHook.ini");
		if (FileExists(candidate))
		{
			return candidate;
		}

		return JoinPath(moduleDir, dllIniName);
	}

	static bool IsLoaderMode(const std::wstring& mode)
	{
		return ToLowerCopy(mode) == L"loader";
	}

	static bool HasLocaleRelaunchGuard()
	{
		wchar_t value[8] = {};
		DWORD len = GetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", value, _countof(value));
		return len > 0 && len < _countof(value);
	}

	static std::wstring GetLocaleEmulatorTempDir()
	{
		wchar_t exePath[MAX_PATH] = {};
		if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		{
			return L"";
		}
		std::wstring fallbackBaseDir = PathRemoveFileName(exePath);
		std::wstring relativePath = std::wstring(L"LocaleEmulator\\") + std::to_wstring((uint64_t)GetCurrentProcessId());
		return GetRuntimeCacheDir(fallbackBaseDir.empty() ? nullptr : fallbackBaseDir.c_str(), relativePath.c_str());
	}

	static std::wstring GetLocaleEmulatorBaseDir()
	{
		wchar_t exePath[MAX_PATH] = {};
		if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		{
			return GetCurrentDirW();
		}
		return PathRemoveFileName(exePath);
	}

	static std::wstring GetEnvironmentVariableString(const wchar_t* name, bool& exists)
	{
		exists = false;
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

static std::wstring BuildPrependedPathValue(
	const std::vector<std::wstring>& directories,
	const std::wstring& originalPath)
{
	std::wstring newPath;
	for (const std::wstring& dir : directories)
	{
		if (dir.empty())
		{
			continue;
		}
		if (!newPath.empty())
		{
			newPath += L";";
		}
		newPath += dir;
	}
	if (newPath.empty())
	{
		return L"";
	}
	if (!originalPath.empty())
	{
		newPath += L";";
		newPath += originalPath;
	}
	return newPath;
}

static bool BuildEnvironmentBlockWithPath(const std::wstring& pathValue, std::vector<wchar_t>& environmentBlock)
{
	environmentBlock.clear();
	if (pathValue.empty())
	{
		return false;
	}

	LPWCH rawEnvironment = GetEnvironmentStringsW();
	if (!rawEnvironment)
	{
		return false;
	}

	bool pathInserted = false;
	for (const wchar_t* entry = rawEnvironment; *entry != L'\0'; entry += wcslen(entry) + 1)
	{
		const wchar_t* equals = wcschr(entry, L'=');
		bool isPathEntry = false;
		if (equals && equals == entry + 4 && _wcsnicmp(entry, L"PATH", 4) == 0)
		{
			isPathEntry = true;
		}

		const std::wstring value = isPathEntry
			? (std::wstring(L"PATH=") + pathValue)
			: std::wstring(entry);
		environmentBlock.insert(environmentBlock.end(), value.begin(), value.end());
		environmentBlock.push_back(L'\0');
		pathInserted = pathInserted || isPathEntry;
	}

	if (!pathInserted)
	{
		const std::wstring pathEntry = std::wstring(L"PATH=") + pathValue;
		environmentBlock.insert(environmentBlock.end(), pathEntry.begin(), pathEntry.end());
		environmentBlock.push_back(L'\0');
	}

	environmentBlock.push_back(L'\0');
	FreeEnvironmentStringsW(rawEnvironment);
	return true;
}

	static bool PrependDirectoriesToPath(
		const std::vector<std::wstring>& directories,
		std::wstring& originalPath,
		bool& originalPathExists)
	{
		originalPath = GetEnvironmentVariableString(L"PATH", originalPathExists);
	std::wstring newPath = BuildPrependedPathValue(directories, originalPath);
		if (newPath.empty())
		{
			return false;
		}
		return SetEnvironmentVariableW(L"PATH", newPath.c_str()) != FALSE;
	}

	static void RestorePathEnvironment(const std::wstring& originalPath, bool originalPathExists)
	{
		SetEnvironmentVariableW(L"PATH", originalPathExists ? originalPath.c_str() : nullptr);
	}

	static void CleanupMedFontCache()
	{
		const std::filesystem::path target = std::filesystem::current_path() / L"_FONTSET.MED";
		std::error_code ec;
		if (!std::filesystem::exists(target, ec))
		{
			return;
		}
		if (ec)
		{
			LogMessage(LogLevel::Error, L"MED cleanup: exists check failed: %hs", ec.message().c_str());
			return;
		}
		if (std::filesystem::remove(target, ec))
		{
			LogMessage(LogLevel::Info, L"MED cleanup: deleted %s", target.wstring().c_str());
			return;
		}
		if (ec)
		{
			LogMessage(LogLevel::Error, L"MED cleanup: delete failed %s, error=%hs", target.wstring().c_str(), ec.message().c_str());
			return;
		}
		LogMessage(LogLevel::Debug, L"MED cleanup: target not deleted %s", target.wstring().c_str());
	}

	static void CleanupMajiroFontCache()
	{
		const std::filesystem::path savedataDir = std::filesystem::current_path() / L"savedata";
		std::error_code ec;
		if (!std::filesystem::exists(savedataDir, ec) || !std::filesystem::is_directory(savedataDir, ec))
		{
			return;
		}
		if (ec)
		{
			LogMessage(LogLevel::Error, L"MAJIRO cleanup: savedata check failed: %hs", ec.message().c_str());
			return;
		}

		size_t removedCount = 0;
		for (const auto& entry : std::filesystem::directory_iterator(savedataDir, ec))
		{
			if (ec)
			{
				LogMessage(LogLevel::Error, L"MAJIRO cleanup: iterate failed: %hs", ec.message().c_str());
				break;
			}
			if (!entry.is_regular_file())
			{
				continue;
			}
			std::wstring ext = ToLowerCopy(entry.path().extension().wstring());
			if (ext != L".fcd")
			{
				continue;
			}
			std::error_code removeEc;
			if (std::filesystem::remove(entry.path(), removeEc))
			{
				++removedCount;
			}
			else if (removeEc)
			{
				LogMessage(LogLevel::Error, L"MAJIRO cleanup: delete failed %s, error=%hs",
					entry.path().wstring().c_str(), removeEc.message().c_str());
			}
		}

		LogMessage(LogLevel::Info, L"MAJIRO cleanup: removed %zu .fcd files", removedCount);
	}

	static void RunEngineCacheCleanup(const EngineCacheSettings& settings)
	{
		if (settings.med)
		{
			CleanupMedFontCache();
		}
		if (settings.majiro)
		{
			CleanupMajiroFontCache();
		}
	}

	static std::wstring TrimWideCopy(std::wstring value)
	{
		size_t begin = 0;
		while (begin < value.size() && iswspace(static_cast<wint_t>(value[begin])) != 0)
		{
			++begin;
		}
		size_t end = value.size();
		while (end > begin && iswspace(static_cast<wint_t>(value[end - 1])) != 0)
		{
			--end;
		}
		return value.substr(begin, end - begin);
	}

	static std::wstring BuildStartupMessageBody(const StartupMessageSettings& settings)
	{
		std::wstring text = TrimWideCopy(settings.text);
		if (settings.style == 1)
		{
			return text;
		}

		std::wstring body;
		std::wstring author = TrimWideCopy(settings.author);
		if (!author.empty())
		{
			body += L"【补丁作者】\r\n - ";
			body += author;
		}
		if (!text.empty())
		{
			if (!body.empty())
			{
				body += L"\r\n\r\n";
			}
			body += L"【补丁声明】\r\n";
			body += text;
		}
		return body;
	}

	static bool ShowStartupMessage(const StartupMessageSettings& settings)
	{
		if (!settings.enable)
		{
			return true;
		}

		std::wstring body = BuildStartupMessageBody(settings);
		if (body.empty())
		{
			return true;
		}

		LogMessage(LogLevel::Info, L"StartupMessage: showing external consent dialog, title=%s", settings.title.c_str());
		int dialogResult = ShowExternalStartupConsentDialog(settings.title.c_str(), body.c_str());
		LogMessage(LogLevel::Info, L"StartupMessage: dialog result=%d", dialogResult);
		return dialogResult == IDYES;
	}

	static void FillTimeZone(const std::wstring& timezone, RTL_TIME_ZONE_INFORMATION& tzi)
	{
		HKEY hTimeZone = nullptr;
		std::wstring keyPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\";
		keyPath += timezone;
		if (RegOpenKeyW(HKEY_LOCAL_MACHINE, keyPath.c_str(), &hTimeZone) != ERROR_SUCCESS)
		{
			wcsncpy_s(tzi.StandardName, timezone.c_str(), _TRUNCATE);
			wcsncpy_s(tzi.DaylightName, timezone.c_str(), _TRUNCATE);
			return;
		}

		DWORD bufferSize = sizeof(tzi.StandardName);
		RegGetValueW(hTimeZone, nullptr, L"Std", RRF_RT_REG_SZ, nullptr, tzi.StandardName, &bufferSize);
		bufferSize = sizeof(tzi.DaylightName);
		RegGetValueW(hTimeZone, nullptr, L"Dlt", RRF_RT_REG_SZ, nullptr, tzi.DaylightName, &bufferSize);

		REG_TZI_FORMAT regTzi = {};
		bufferSize = sizeof(regTzi);
		if (RegGetValueW(hTimeZone, nullptr, L"TZI", RRF_RT_REG_BINARY, nullptr, &regTzi, &bufferSize) == ERROR_SUCCESS)
		{
			tzi.Bias = regTzi.Bias;
			tzi.StandardBias = regTzi.StandardBias;
			tzi.DaylightBias = regTzi.DaylightBias;
		}

		RegCloseKey(hTimeZone);
	}

	static bool TryRelaunchWithLocaleEmulator(const AppSettings& settings)
	{
		if (!settings.localeEmulator.enable)
		{
			LogMessage(LogLevel::Debug, L"LocaleEmulator skipped: disabled");
			return false;
		}
		if (IsLoaderMode(settings.loadMode.mode))
		{
			LogMessage(LogLevel::Debug, L"LocaleEmulator skipped: loader mode");
			return false;
		}
		if (HasLocaleRelaunchGuard())
		{
			LogMessage(LogLevel::Debug, L"LocaleEmulator skipped: relaunch guard already set");
			return false;
		}
		if (GetACP() == settings.localeEmulator.ansiCodePage)
		{
			LogMessage(LogLevel::Debug, L"LocaleEmulator skipped: ACP already %u", settings.localeEmulator.ansiCodePage);
			return false;
		}

		LogMessage(LogLevel::Info, L"LocaleEmulator relaunch required: ACP=%u -> %u",
			GetACP(), settings.localeEmulator.ansiCodePage);
		SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", L"1");

		if (settings.filePatch.customPakEnable && !settings.filePatch.customPakFiles.empty())
		{
			std::vector<const wchar_t*> pakPaths;
			pakPaths.reserve(settings.filePatch.customPakFiles.size());
			for (const std::wstring& pakPath : settings.filePatch.customPakFiles)
			{
				pakPaths.push_back(pakPath.c_str());
			}
			ConfigureCustomPakVFS(true, pakPaths.data(), pakPaths.size(), settings.debug.enable);
		}

		bool loaderFromConfiguredOverride = false;
		bool loaderFromCustomPak = false;
		bool loaderConfiguredCandidateFound = false;
		std::vector<std::wstring> preparedPaths;
		std::vector<std::wstring> runtimeSearchDirs;
		LocaleEmulatorLoaderOptions loaderOptions;
		loaderOptions.tempDir = GetLocaleEmulatorTempDir();
		loaderOptions.patchBaseDir = GetLocaleEmulatorBaseDir();
		loaderOptions.patchFolders = &settings.filePatch.patchFolders;
		loaderOptions.preferLocalLoader = true;
		HMODULE loaderDll = LoadLocaleEmulatorLoaderModule(
			loaderOptions,
			loaderFromConfiguredOverride,
			loaderFromCustomPak,
			loaderConfiguredCandidateFound,
			preparedPaths,
			&runtimeSearchDirs);
		if (!loaderDll)
		{
			DWORD errorCode = GetLastError();
			if (loaderConfiguredCandidateFound)
			{
				LogMessage(LogLevel::Error, L"LocaleEmulator: LoaderDll.dll or LocaleEmulator.dll found in configured FilePatch targets but loading failed (GetLastError=%u)", errorCode);
				MessageBoxW(NULL, L"LoaderDll.dll / LocaleEmulator.dll 已在 FilePatch 配置的目录或封包中找到，但加载失败", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
			}
			else
			{
				LogMessage(LogLevel::Error, L"LocaleEmulator: LoaderDll.dll missing locally and in configured FilePatch targets (GetLastError=%u)", errorCode);
				MessageBoxW(NULL, L"LoaderDll.dll missing", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
			}
			SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
			return false;
		}
		if (loaderFromCustomPak)
		{
			LogMessage(LogLevel::Info, L"LocaleEmulator: LoaderDll.dll loaded from configured CustomPak cache");
		}
		else if (loaderFromConfiguredOverride)
		{
			LogMessage(LogLevel::Info, L"LocaleEmulator: LoaderDll.dll loaded from configured patch path");
		}

		PFN_LeCreateProcess leCreateProcess = (PFN_LeCreateProcess)GetProcAddress(loaderDll, "LeCreateProcess");
		if (!leCreateProcess)
		{
			LogMessage(LogLevel::Error, L"LocaleEmulator: LeCreateProcess not found");
			MessageBoxW(NULL, L"LeCreateProcess not found in LoaderDll.dll", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
			FreeLibrary(loaderDll);
			CleanupPreparedRuntimeFiles(preparedPaths);
			SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
			return false;
		}

		LEB leb = {};
		leb.AnsiCodePage = settings.localeEmulator.ansiCodePage;
		leb.OemCodePage = settings.localeEmulator.oemCodePage;
		leb.LocaleID = settings.localeEmulator.localeID;
		leb.DefaultCharset = settings.localeEmulator.defaultCharset;
		leb.HookUILanguageAPI = settings.localeEmulator.hookUILanguageAPI;
		FillTimeZone(settings.localeEmulator.timezone, leb.Timezone);

		wchar_t exePath[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);

		wchar_t currentDirectory[MAX_PATH] = {};
		GetCurrentDirectoryW(MAX_PATH, currentDirectory);

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		ML_PROCESS_INFORMATION processInfo = {};
		std::vector<std::wstring> stagedPaths;
		const std::wstring exeDir = GetLocaleEmulatorBaseDir();
		std::wstring originalStagedFilesValue;
		bool originalStagedFilesValueExists = false;
		if (!preparedPaths.empty())
		{
			StageLocaleEmulatorFilesNextToExe(preparedPaths, exeDir, stagedPaths);
			if (!stagedPaths.empty())
			{
				AppendUniquePath(runtimeSearchDirs, exeDir);
				const std::wstring stagedValue = BuildDelimitedEnvironmentValue(stagedPaths);
				if (!stagedValue.empty())
				{
					originalStagedFilesValue = GetEnvironmentVariableString(kLocaleEmulatorStagedFilesEnvVar, originalStagedFilesValueExists);
					SetEnvironmentVariableW(kLocaleEmulatorStagedFilesEnvVar, stagedValue.c_str());
				}
				LogMessage(LogLevel::Info, L"LocaleEmulator: staged %u runtime file(s) next to exe for proxy relaunch",
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
			LogMessage(LogLevel::Warn, L"LocaleEmulator: failed to prepend runtime search dirs to PATH");
		}
	if (!runtimeSearchDirs.empty() && !environmentBlockReady)
	{
		LogMessage(LogLevel::Warn, L"LocaleEmulator: failed to build environment block with runtime search dirs");
	}

		const uint32_t creationFlags = environmentBlockReady ? CREATE_UNICODE_ENVIRONMENT : 0;
		DWORD result = leCreateProcess(
			&leb,
			exePath,
			GetCommandLineW(),
			currentDirectory,
			creationFlags,
			&startupInfo,
			&processInfo,
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

		FreeLibrary(loaderDll);
		CleanupPreparedRuntimeFiles(preparedPaths);
		if (result != ERROR_SUCCESS)
		{
			CleanupPreparedRuntimeFiles(stagedPaths);
		}

		if (result == ERROR_SUCCESS)
		{
			LogMessage(LogLevel::Info, L"LocaleEmulator relaunch succeeded");
			ExitProcess(0);
			return true;
		}

		LogMessage(LogLevel::Error, L"LocaleEmulator relaunch failed: 0x%08X", result);
		wchar_t errorText[256] = {};
		swprintf_s(errorText, L"LeCreateProcess failed: 0x%08X", result);
		MessageBoxW(NULL, errorText, L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
		SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
		return false;
	}

	bool HookManager::TryEarlyLocaleEmulatorRelaunch(HMODULE dllModule)
	{
		try
		{
			std::wstring dllNameNoExt = GetModuleNameNoExtension(dllModule);
			std::wstring iniPath = ResolveIniPath(dllModule, dllNameNoExt);

			AppSettings settings;
			std::string errorMessage;
			if (!ConfigManager::Load(iniPath, settings, errorMessage, nullptr))
			{
				return false;
			}

			return TryRelaunchWithLocaleEmulator(settings);
		}
		catch (...)
		{
			return false;
		}
	}

	bool HookManager::ShouldSuspendProcessForStartupMessage(HMODULE dllModule)
	{
		try
		{
			std::wstring dllNameNoExt = GetModuleNameNoExtension(dllModule);
			std::wstring iniPath = ResolveIniPath(dllModule, dllNameNoExt);

			AppSettings settings;
			std::string errorMessage;
			if (!ConfigManager::Load(iniPath, settings, errorMessage, nullptr))
			{
				return false;
			}

			return settings.startupMessage.enable;
		}
		catch (...)
		{
			return false;
		}
	}

	bool HookManager::TryHandleStartupMessageBeforeInitialization(HMODULE dllModule)
	{
		try
		{
			std::wstring dllNameNoExt = GetModuleNameNoExtension(dllModule);
			std::wstring iniPath = ResolveIniPath(dllModule, dllNameNoExt);

			AppSettings settings;
			std::string errorMessage;
			if (!ConfigManager::Load(iniPath, settings, errorMessage, nullptr))
			{
				return true;
			}
			if (!settings.startupMessage.enable)
			{
				return true;
			}

			if (!ShowStartupMessage(settings.startupMessage))
			{
				ExitProcess(0);
				return false;
			}

			InterlockedExchange(&sg_startupMessageHandledEarly, 1);
			return true;
		}
		catch (...)
		{
			return true;
		}
	}

	void HookManager::RegisterLocaleEmulatorStagedFilesFromEnvironment()
	{
		bool exists = false;
		const std::wstring rawValue = GetEnvironmentVariableString(kLocaleEmulatorStagedFilesEnvVar, exists);
		if (!exists || rawValue.empty())
		{
			return;
		}

		std::vector<std::wstring> paths = ParseDelimitedEnvironmentValue(rawValue);
		sg_localeEmulatorStagedCleanupPaths.clear();
		for (const std::wstring& path : paths)
		{
			if (IsExistingFilePath(path))
			{
				AppendUniqueString(sg_localeEmulatorStagedCleanupPaths, path);
			}
		}

		SetEnvironmentVariableW(kLocaleEmulatorStagedFilesEnvVar, nullptr);
	}

	void HookManager::CleanupLocaleEmulatorStagedFilesOnShutdown()
	{
		for (const std::wstring& path : sg_localeEmulatorStagedCleanupPaths)
		{
			SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(path.c_str());
		}
		sg_localeEmulatorStagedCleanupPaths.clear();
	}

	void HookManager::Initialize(HMODULE dllModule)
	{
		std::wstring dllNameNoExt = GetModuleNameNoExtension(dllModule);
		const bool startupMessageHandledEarly = InterlockedExchange(&sg_startupMessageHandledEarly, 0) != 0;
		try
		{
			std::wstring modulePath = GetModulePath(dllModule);
			std::wstring iniPath = ResolveIniPath(dllModule, dllNameNoExt);

			InitLogger(dllNameNoExt.c_str(), true, false, false);
			LogMessage(LogLevel::Info, L"Initialize start: module=%s", modulePath.empty() ? L"(unknown)" : modulePath.c_str());
			LogMessage(LogLevel::Info, L"Initialize context: PID=%lu TID=%lu", GetCurrentProcessId(), GetCurrentThreadId());
			LogMessage(LogLevel::Info, L"Config path: %s", iniPath.c_str());

			AppSettings settings;
			std::string errorMessage;
			std::string warningMessage;
			if (!ConfigManager::Load(iniPath, settings, errorMessage, &warningMessage))
			{
				std::wstring wideError = Utf8ToWide(errorMessage);
				LogMessage(LogLevel::Error, L"Config load failed: %s", wideError.c_str());
				MessageBoxW(NULL, wideError.c_str(), L"CialloHook", MB_OK | MB_ICONERROR);
				return;
			}

			bool effectiveDebugEnable = settings.debug.enable;
			InitLogger(dllNameNoExt.c_str(), effectiveDebugEnable, settings.debug.logToFile, settings.debug.logToConsole);
			LogMessage(LogLevel::Info, L"Config loaded: %s", iniPath.c_str());
			LogMessage(LogLevel::Info, L"Debug flags: Enable=%d File=%d Console=%d Effective=%d",
				settings.debug.enable ? 1 : 0,
				settings.debug.logToFile ? 1 : 0,
				settings.debug.logToConsole ? 1 : 0,
				effectiveDebugEnable ? 1 : 0);
			if (!warningMessage.empty())
			{
				std::wstring wideWarning = Utf8ToWide(warningMessage);
				LogMessage(LogLevel::Warn, L"%s", wideWarning.c_str());
				MessageBoxW(NULL, wideWarning.c_str(), L"CialloHook", MB_OK | MB_ICONWARNING);
			}
			bool hasFontHook = !settings.font.font.empty();
			LogMessage(LogLevel::Info, L"Feature flags: Font=%d Text=%d Title=%d FilePatch=%d CodePage=%d",
				hasFontHook ? 1 : 0, settings.textReplace.rules.empty() ? 0 : 1, settings.windowTitle.rules.empty() ? 0 : 1,
				settings.filePatch.enable ? 1 : 0, settings.codePage.enable ? 1 : 0);
			LogMessage(LogLevel::Info, L"Engine cache flags: MED=%d MAJIRO=%d",
				settings.engineCache.med ? 1 : 0, settings.engineCache.majiro ? 1 : 0);
			LogMessage(LogLevel::Info, L"Engine patch flags: KrkrPatch=%d(count=%u) WafflePatch=%d(GetTextCrash=%d)",
				settings.enginePatches.enableKrkrPatch ? 1 : 0,
				(uint32_t)settings.enginePatches.krkrPatchNames.size(),
				settings.enginePatches.enableWafflePatch ? 1 : 0,
				settings.enginePatches.waffleFixGetTextCrash ? 1 : 0);
			RunEngineCacheCleanup(settings.engineCache);

			std::wstring dllNameLower = ToLowerCopy(dllNameNoExt);
			bool isWinmmProxy = (dllNameLower == L"winmm");
			LogMessage(LogLevel::Info, L"Load mode: %s", isWinmmProxy ? L"proxy(winmm)" : L"dll");

			if (TryRelaunchWithLocaleEmulator(settings))
			{
				return;
			}

			if (!startupMessageHandledEarly)
			{
				HookModules::ApplyPreStartupHooks(settings);
				if (!ShowStartupMessage(settings.startupMessage))
				{
					ReleaseStartupWindowGate();
					LogMessage(LogLevel::Info, L"StartupMessage: user declined patch notice, exit process");
					ExitProcess(0);
					return;
				}
			}
			else if (settings.startupMessage.enable)
			{
				LogMessage(LogLevel::Info, L"StartupMessage: early consent already granted, skip duplicate dialog");
			}
			HookModules::ApplyPostStartupHooks(settings);
			if (settings.startupMessage.enable)
			{
				BringProcessMainWindowToFront(GetCurrentProcessId(), 10000);
			}
			LogMessage(LogLevel::Info, L"All hook modules applied");
		}
		catch (const std::exception& err)
		{
			std::wstring wideError = Utf8ToWide(err.what());
			InitLogger(dllNameNoExt.c_str(), true, false, false);
			LogMessage(LogLevel::Error, L"Initialize exception: %s", wideError.c_str());
			MessageBoxW(NULL, wideError.c_str(), L"CialloHook", MB_OK | MB_ICONERROR);
		}
	}
}
