#include "hook_manager.h"

#include <string>
#include <cwctype>
#include <cmath>
#include <memory>
#include <vector>
#include <exception>
#include <cstdint>

#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

#include <mmsystem.h>
/* 不用 #pragma comment(lib, "winmm.lib")
 * 因为 CialloHook 可能作为 winmm.dll 代理加载，静态链接会循环依赖。
 * MCI 函数在运行时从系统 winmm.dll 动态获取。 */

#include "../../RuntimeCore/io/File.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/hook/LocaleEmulatorSupport.h"
#include "../config/build_options.h"
#include "../config/config_manager.h"
#include "../hooks/hook_modules.h"

using namespace Rut::FileX;
using namespace Rut::HookX;

#if defined(NDEBUG)
#define CIALLOHOOK_VERBOSE_INFO_LOG(...) ((void)0)
#else
#define CIALLOHOOK_VERBOSE_INFO_LOG(...) LogMessage(LogLevel::Info, __VA_ARGS__)
#endif

namespace CialloHook
{
	namespace
	{
		const wchar_t* kLocaleEmulatorStagedFilesEnvVar = L"CIALLOHOOK_LE_STAGED_FILES";
		const wchar_t* kStartupMessageAcceptedEnvVar = L"CIALLOHOOK_STARTUP_MESSAGE_ACCEPTED";
		std::vector<std::wstring> sg_localeEmulatorStagedCleanupPaths;
		HANDLE sg_startupInitializationMutex = nullptr;
		volatile LONG sg_startupInitializationMutexOwned = 0;
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

	using PFN_LepCreateProcess = LONG(WINAPI*)(
		LEB* leb,
		const wchar_t* applicationName,
		wchar_t* commandLine,
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
				AppendUniquePath(paths, current);
			}
			if (end == std::wstring::npos)
			{
				break;
			}
			start = end + 1;
		}
		return paths;
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

	struct ModuleSettingsContext
	{
		std::wstring modulePath;
		std::wstring dllNameNoExt;
		std::wstring iniPath;
	};

	static ModuleSettingsContext BuildModuleSettingsContext(HMODULE module)
	{
		ModuleSettingsContext context;
		context.modulePath = GetModulePath(module);
		context.dllNameNoExt = context.modulePath.empty()
			? std::wstring(L"CialloHook")
			: PathRemoveExtension(PathGetFileName(context.modulePath));
		context.iniPath = ResolveIniPath(module, context.dllNameNoExt);
		return context;
	}

	static bool TryLoadModuleSettings(
		const ModuleSettingsContext& context,
		AppSettings& settings,
		std::string& errorMessage,
		std::string* warningMessage)
	{
		return ConfigManager::Load(context.iniPath, settings, errorMessage, warningMessage);
	}

	static bool IsTruthyEnvironmentVariable(const wchar_t* name)
	{
		if (!name || name[0] == L'\0')
		{
			return false;
		}

		wchar_t value[16] = {};
		DWORD len = GetEnvironmentVariableW(name, value, _countof(value));
		if (len == 0 || len >= _countof(value))
		{
			return false;
		}

		return lstrcmpiW(value, L"1") == 0
			|| lstrcmpiW(value, L"true") == 0
			|| lstrcmpiW(value, L"yes") == 0
			|| lstrcmpiW(value, L"on") == 0;
	}

	static bool ConsumeTruthyEnvironmentVariable(const wchar_t* name)
	{
		const bool enabled = IsTruthyEnvironmentVariable(name);
		if (enabled)
		{
			SetEnvironmentVariableW(name, nullptr);
		}
		return enabled;
	}

	static uint64_t HashWideStringNoCase(const std::wstring& value)
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

	static std::wstring BuildStartupInitializationMutexName()
	{
		wchar_t exePath[MAX_PATH] = {};
		if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
		{
			return L"Local\\CialloHook.StartupInit.Unknown";
		}

		wchar_t mutexName[96] = {};
		swprintf_s(mutexName, L"Local\\CialloHook.StartupInit.%016llX",
			static_cast<unsigned long long>(HashWideStringNoCase(exePath)));
		return mutexName;
	}

	static bool TryAcquireStartupInitializationMutex()
	{
		if (InterlockedCompareExchange(&sg_startupInitializationMutexOwned, 0, 0) != 0)
		{
			return true;
		}

		const std::wstring mutexName = BuildStartupInitializationMutexName();
		HANDLE mutexHandle = CreateMutexW(nullptr, TRUE, mutexName.c_str());
		if (!mutexHandle)
		{
			return true;
		}

		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			CloseHandle(mutexHandle);
			return false;
		}

		sg_startupInitializationMutex = mutexHandle;
		InterlockedExchange(&sg_startupInitializationMutexOwned, 1);
		return true;
	}

	static void ReleaseStartupInitializationMutex()
	{
		if (InterlockedExchange(&sg_startupInitializationMutexOwned, 0) == 0)
		{
			return;
		}

		HANDLE mutexHandle = sg_startupInitializationMutex;
		sg_startupInitializationMutex = nullptr;
		if (mutexHandle)
		{
			ReleaseMutex(mutexHandle);
			CloseHandle(mutexHandle);
		}
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

	static bool IsDirectoryPath(const std::wstring& path)
	{
		DWORD attr = GetFileAttributesW(path.c_str());
		return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

	static void CleanupMedFontCache()
	{
		const std::wstring target = JoinPath(GetCurrentDirW(), L"_FONTSET.MED");
		DWORD attr = GetFileAttributesW(target.c_str());
		if (attr == INVALID_FILE_ATTRIBUTES)
		{
			return;
		}
		if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			LogMessage(LogLevel::Debug, L"MED cleanup: target not deleted %s", target.c_str());
			return;
		}
		if (DeleteFileW(target.c_str()))
		{
			CIALLOHOOK_VERBOSE_INFO_LOG(L"MED cleanup: deleted %s", target.c_str());
			return;
		}
		LogMessage(LogLevel::Error, L"MED cleanup: delete failed %s, error=%lu", target.c_str(), GetLastError());
	}

	static void CleanupMajiroFontCache()
	{
		const std::wstring savedataDir = JoinPath(GetCurrentDirW(), L"savedata");
		if (!IsDirectoryPath(savedataDir))
		{
			return;
		}

		WIN32_FIND_DATAW findData = {};
		std::wstring pattern = JoinPath(savedataDir, L"*");
		HANDLE findHandle = FindFirstFileW(pattern.c_str(), &findData);
		if (findHandle == INVALID_HANDLE_VALUE)
		{
			DWORD error = GetLastError();
			if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
			{
				LogMessage(LogLevel::Error, L"MAJIRO cleanup: iterate failed, error=%lu", error);
			}
			return;
		}

		size_t removedCount = 0;
		do
		{
			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				continue;
			}
			std::wstring fileName = findData.cFileName;
			size_t dot = fileName.find_last_of(L'.');
			std::wstring ext = dot == std::wstring::npos ? std::wstring() : ToLowerCopy(fileName.substr(dot));
			if (ext != L".fcd")
			{
				continue;
			}
			std::wstring filePath = JoinPath(savedataDir, fileName);
			if (DeleteFileW(filePath.c_str()))
			{
				++removedCount;
			}
			else
			{
				LogMessage(LogLevel::Error, L"MAJIRO cleanup: delete failed %s, error=%lu", filePath.c_str(), GetLastError());
			}
		} while (FindNextFileW(findHandle, &findData));

		DWORD iterateError = GetLastError();
		FindClose(findHandle);
		if (iterateError != ERROR_NO_MORE_FILES)
		{
			LogMessage(LogLevel::Error, L"MAJIRO cleanup: iterate failed, error=%lu", iterateError);
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

		CIALLOHOOK_VERBOSE_INFO_LOG(L"StartupMessage: showing external consent dialog, title=%s", settings.title.c_str());
		int dialogResult = ShowExternalStartupConsentDialog(settings.title.c_str(), body.c_str());
		CIALLOHOOK_VERBOSE_INFO_LOG(L"StartupMessage: dialog result=%d", dialogResult);
		return dialogResult == IDYES;
	}

	/* ================================================================
	 * Global Exception Handler (VEH)
	 * Logs all serious exceptions with module name and registers.
	 * Crash dump is handled separately by dllmain.cpp TopLevelExceptionFilter.
	 * ================================================================ */

	static PVOID g_vehHandle = nullptr;

	/* ---- Stack overflow safe logging ----
	 * When EXCEPTION_STACK_OVERFLOW fires the thread has almost no stack left.
	 * We must avoid any heap allocation, std::wstring, or deep call chains.
	 * All buffers are static; OutputDebugStringW and WriteFile are safe to call. */
	static volatile LONG sg_stackOverflowHandlingInProgress = 0;
	static wchar_t sg_soLogBuf[512];
	static wchar_t sg_soRegBuf[512];
	static wchar_t sg_soFilePath[MAX_PATH];
	static volatile LONG sg_soFilePathReady = 0;

	static void InitStackOverflowLogFilePath()
	{
		wchar_t exePath[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
		{
			sg_soFilePath[0] = L'\0';
			return;
		}
		wchar_t* lastSep = nullptr;
		for (wchar_t* p = exePath; *p != L'\0'; ++p)
		{
			if (*p == L'\\' || *p == L'/') lastSep = p;
		}
		if (lastSep) *(lastSep + 1) = L'\0';
		else exePath[0] = L'\0';
		swprintf_s(sg_soFilePath, L"%sCialloHook_stackoverflow.log", exePath);
		InterlockedExchange(&sg_soFilePathReady, 1);
	}

	static void WriteStackOverflowLogDirect(const wchar_t* line)
	{
		if (InterlockedCompareExchange(&sg_soFilePathReady, 0, 0) == 0 || sg_soFilePath[0] == L'\0')
			return;
		HANDLE hf = CreateFileW(sg_soFilePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hf == INVALID_HANDLE_VALUE)
			return;
		/* Write BOM if file is empty */
		LARGE_INTEGER fileSize = {};
		if (GetFileSizeEx(hf, &fileSize) && fileSize.QuadPart == 0)
		{
			unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
			DWORD bw = 0;
			WriteFile(hf, bom, 3, &bw, nullptr);
		}
		/* Convert to UTF-8 using stack-minimal static buffer */
		char utf8Buf[1536] = {};
		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8Buf, sizeof(utf8Buf) - 1, nullptr, nullptr);
		if (utf8Len > 1)
		{
			DWORD bw = 0;
			WriteFile(hf, utf8Buf, (DWORD)(utf8Len - 1), &bw, nullptr);
		}
		CloseHandle(hf);
	}

	static void LogStackOverflowSafe(PEXCEPTION_POINTERS ep)
	{
		/* Prevent reentrant handling */
		if (InterlockedCompareExchange(&sg_stackOverflowHandlingInProgress, 1, 0) != 0)
			return;

		void* faultAddr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;

		HMODULE faultModule = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)faultAddr, &faultModule);

		wchar_t modPath[MAX_PATH] = L"<unknown>";
		if (faultModule) GetModuleFileNameW(faultModule, modPath, MAX_PATH);
		const wchar_t* modSlash = wcsrchr(modPath, L'\\');
		const wchar_t* modName = modSlash ? modSlash + 1 : modPath;

		SYSTEMTIME st = {};
		GetLocalTime(&st);

		swprintf_s(sg_soLogBuf,
			L"[%02d:%02d:%02d.%03d][P%lu:T%lu][VEH] EXCEPTION_STACK_OVERFLOW at 0x%p in %s\r\n",
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			GetCurrentProcessId(), GetCurrentThreadId(),
			faultAddr, modName);
		OutputDebugStringW(sg_soLogBuf);
		WriteStackOverflowLogDirect(sg_soLogBuf);

		if (ep && ep->ContextRecord)
		{
			auto* ctx = ep->ContextRecord;
#ifdef _M_IX86
			swprintf_s(sg_soRegBuf,
				L"[%02d:%02d:%02d.%03d][P%lu:T%lu][VEH] EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X EIP=%08X\r\n",
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
				GetCurrentProcessId(), GetCurrentThreadId(),
				ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp, ctx->Eip);
#elif defined(_M_X64)
			swprintf_s(sg_soRegBuf,
				L"[%02d:%02d:%02d.%03d][P%lu:T%lu][VEH] RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX RSI=%016llX RDI=%016llX RBP=%016llX RSP=%016llX RIP=%016llX\r\n",
				st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
				GetCurrentProcessId(), GetCurrentThreadId(),
				ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx, ctx->Rsi, ctx->Rdi, ctx->Rbp, ctx->Rsp, ctx->Rip);
#else
			sg_soRegBuf[0] = L'\0';
#endif
			if (sg_soRegBuf[0] != L'\0')
			{
				OutputDebugStringW(sg_soRegBuf);
				WriteStackOverflowLogDirect(sg_soRegBuf);
			}
		}

		/* Note: we do NOT reset sg_stackOverflowHandlingInProgress here.
		 * Stack overflow is typically fatal; preventing reentry avoids infinite loops. */
	}

	/* ---- VEH exception deduplication ----
	 * Hook functions use __try/__except, so most exceptions are caught by SEH.
	 * But VEH fires before SEH, and the same address can trigger hundreds of
	 * times (e.g. font metrics called repeatedly with invalid state).
	 * We deduplicate: same address within 2s → log first 3, then suppress
	 * and emit a count summary when the address changes or time expires. */
	static void* volatile sg_vehLastAddr = nullptr;
	static volatile LONG sg_vehRepeatCount = 0;
	static volatile LONG sg_vehSuppressedCount = 0;
	static void* volatile sg_vehSuppressedCaller = nullptr;
	static void* volatile sg_vehSuppressedReturnAddress = nullptr;
	static DWORD sg_vehLastTick = 0;
	static const LONG VEH_LOG_THRESHOLD = 3;
	static const DWORD VEH_DEDUP_WINDOW_MS = 2000;
	static volatile LONG sg_vehNtdllAvSuppressedCount = 0;
	static void* volatile sg_vehNtdllAvCaller = nullptr;
	static void* volatile sg_vehNtdllAvReturnAddress = nullptr;
	static DWORD sg_vehNtdllAvLastLogTick = 0;
	static const DWORD VEH_NTDLL_AV_LOG_WINDOW_MS = 2000;

	static bool TryReadPointerForVeh(uintptr_t address, void*& value)
	{
		value = nullptr;
		if (!address)
		{
			return false;
		}
		__try
		{
			value = *reinterpret_cast<void**>(address);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			value = nullptr;
			return false;
		}
	}

	static void GetVehCallerAddresses(const CONTEXT* ctx, void*& caller, void*& returnAddress)
	{
		caller = nullptr;
		returnAddress = nullptr;
		if (!ctx)
		{
			return;
		}
#ifdef _M_IX86
		returnAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(ctx->Eip));
		void* stackReturnAddress = nullptr;
		if (TryReadPointerForVeh(static_cast<uintptr_t>(ctx->Esp), stackReturnAddress))
		{
			caller = stackReturnAddress;
		}
#elif defined(_M_X64)
		returnAddress = reinterpret_cast<void*>(static_cast<uintptr_t>(ctx->Rip));
		void* stackReturnAddress = nullptr;
		if (TryReadPointerForVeh(static_cast<uintptr_t>(ctx->Rsp), stackReturnAddress))
		{
			caller = stackReturnAddress;
		}
#else
		(void)ctx;
#endif
	}

	static void FlushVehSuppressedCount()
	{
		LONG suppressed = InterlockedExchange(&sg_vehSuppressedCount, 0);
		if (suppressed > 0)
		{
			void* caller = InterlockedExchangePointer(&sg_vehSuppressedCaller, nullptr);
			void* returnAddress = InterlockedExchangePointer(&sg_vehSuppressedReturnAddress, nullptr);
			LogMessage(LogLevel::Warn,
				L"[VEH] ... suppressed %ld repeated exceptions at same address (caught by SEH, non-fatal) caller=%p return=%p",
				suppressed, caller, returnAddress);
		}
	}

	static void LogNtdllAvThrottled(void* faultAddress, void* callerAddress, void* returnAddress, DWORD currentTick)
	{
		InterlockedExchangePointer(&sg_vehNtdllAvCaller, callerAddress);
		InterlockedExchangePointer(&sg_vehNtdllAvReturnAddress, returnAddress);
		LONG suppressed = InterlockedIncrement(&sg_vehNtdllAvSuppressedCount);
		if ((currentTick - sg_vehNtdllAvLastLogTick) < VEH_NTDLL_AV_LOG_WINDOW_MS)
		{
			return;
		}

		sg_vehNtdllAvLastLogTick = currentTick;
		void* caller = InterlockedExchangePointer(&sg_vehNtdllAvCaller, nullptr);
		void* ret = InterlockedExchangePointer(&sg_vehNtdllAvReturnAddress, nullptr);
		suppressed = InterlockedExchange(&sg_vehNtdllAvSuppressedCount, 0);
		LogMessage(LogLevel::Warn,
			L"[VEH] first-chance ntdll AV at 0x%p suppressed=%ld caller=%p return=%p (caught by SEH, non-fatal)",
			faultAddress, suppressed, caller, ret);
	}

	static LONG CALLBACK GlobalVectoredExceptionHandler(PEXCEPTION_POINTERS ep)
	{
		DWORD code = ep->ExceptionRecord->ExceptionCode;

		/* Stack overflow: log with static buffers only (no heap, minimal stack) */
		if (code == EXCEPTION_STACK_OVERFLOW)
		{
			LogStackOverflowSafe(ep);
			return EXCEPTION_CONTINUE_SEARCH;
		}

		/* Only log serious exceptions, skip common noise like breakpoints */
		if (code == EXCEPTION_ACCESS_VIOLATION ||
		    code == EXCEPTION_ILLEGAL_INSTRUCTION ||
		    code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
		    code == EXCEPTION_FLT_DIVIDE_BY_ZERO)
		{
			/* ---- Deduplication ---- */
			void* currentAddr = ep->ExceptionRecord->ExceptionAddress;
			void* callerAddress = nullptr;
			void* returnAddress = nullptr;
			GetVehCallerAddresses(ep->ContextRecord, callerAddress, returnAddress);
			DWORD currentTick = GetTickCount();
			void* lastAddr = sg_vehLastAddr;

			if (currentAddr == lastAddr && (currentTick - sg_vehLastTick) < VEH_DEDUP_WINDOW_MS)
			{
				LONG count = InterlockedIncrement(&sg_vehRepeatCount);
				if (count > VEH_LOG_THRESHOLD)
				{
					InterlockedExchangePointer(&sg_vehSuppressedCaller, callerAddress);
					InterlockedExchangePointer(&sg_vehSuppressedReturnAddress, returnAddress);
					InterlockedIncrement(&sg_vehSuppressedCount);
					return EXCEPTION_CONTINUE_SEARCH;
				}
			}
			else
			{
				/* New address or time window expired: flush old suppressed count and reset */
				FlushVehSuppressedCount();
				InterlockedExchangePointer(&sg_vehLastAddr, currentAddr);
				InterlockedExchange(&sg_vehRepeatCount, 1);
			}
			sg_vehLastTick = currentTick;

			/* Get module info for the faulting address */
			HMODULE faultModule = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)ep->ExceptionRecord->ExceptionAddress, &faultModule);

			wchar_t moduleName[MAX_PATH] = L"<unknown>";
			if (faultModule) GetModuleFileNameW(faultModule, moduleName, MAX_PATH);

			/* Extract just the filename */
			const wchar_t* slash = wcsrchr(moduleName, L'\\');
			const wchar_t* name = slash ? slash + 1 : moduleName;

			/* VEH is first-chance: some games intentionally probe invalid pointers inside
			 * ntdll and then recover via their own SEH. Those entries are not actionable
			 * with CialloHook.map (the faulting IP is outside our module) and were being
			 * reported as scary errors even though execution continued normally. If the
			 * exception is truly unhandled, TopLevelExceptionFilter still writes the real
			 * crash dump/log later. */
			if (code == EXCEPTION_ACCESS_VIOLATION && _wcsicmp(name, L"ntdll.dll") == 0)
			{
				LogNtdllAvThrottled(ep->ExceptionRecord->ExceptionAddress, callerAddress, returnAddress, currentTick);
				return EXCEPTION_CONTINUE_SEARCH;
			}

			LogMessage(LogLevel::Error,
				L"[VEH] Exception 0x%08X at 0x%p in %s (base+0x%X) caller=%p return=%p",
				code, ep->ExceptionRecord->ExceptionAddress, name,
				faultModule ? (DWORD)((BYTE*)ep->ExceptionRecord->ExceptionAddress - (BYTE*)faultModule) : 0,
				callerAddress, returnAddress);

#ifdef _M_IX86
			auto* ctx = ep->ContextRecord;
			LogMessage(LogLevel::Error,
				L"[VEH] EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESI=%08X EDI=%08X EBP=%08X ESP=%08X EIP=%08X",
				ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp, ctx->Eip);
#elif defined(_M_X64)
			auto* ctx = ep->ContextRecord;
			LogMessage(LogLevel::Error,
				L"[VEH] RAX=%016llX RBX=%016llX RCX=%016llX RDX=%016llX RIP=%016llX RSP=%016llX",
				ctx->Rax, ctx->Rbx, ctx->Rcx, ctx->Rdx, ctx->Rip, ctx->Rsp);
#endif
		}

		return EXCEPTION_CONTINUE_SEARCH;
	}

	static void InstallGlobalExceptionHandlers()
	{
		InitStackOverflowLogFilePath();
		g_vehHandle = AddVectoredExceptionHandler(1, GlobalVectoredExceptionHandler);
		/* Note: SetUnhandledExceptionFilter is already set in dllmain.cpp (TopLevelExceptionFilter) */
	}

	static void UninstallGlobalExceptionHandlers()
	{
		if (g_vehHandle)
		{
			FlushVehSuppressedCount();
			RemoveVectoredExceptionHandler(g_vehHandle);
			g_vehHandle = nullptr;
		}
	}

	/* ================================================================
	 * Splash Image — entry point hook, configurable effects
	 * ================================================================ */

	struct SplashTile { int sx, sy, sw, sh; float vx, vy, rot, delay; };
	static constexpr int TILE_C = 12, TILE_R = 9, TILE_N = TILE_C * TILE_R;

	struct SplashState
	{
		Gdiplus::Bitmap* scaled;
		int W, H, posX, posY;
		DWORD startTick, entryMs, holdMs, exitMs, totalMs;
		int entryFx, exitFx;
		int interactionMode;
		bool isDragging;
		int dragMouseStartX, dragMouseStartY;
		int dragWindowStartX, dragWindowStartY;
		SplashTile tiles[TILE_N];
	};
	static SplashState g_sp = {};

	static float sp_randf() { return (float)rand() / (float)RAND_MAX; }
	static float sp_randf_r(float a, float b) { return a + sp_randf() * (b - a); }

	static void InitTiles(SplashState& s)
	{
		srand((unsigned)GetTickCount());
		int tw = s.W / TILE_C, th = s.H / TILE_R;
		for (int r = 0; r < TILE_R; ++r)
			for (int c = 0; c < TILE_C; ++c)
			{
				auto& t = s.tiles[r * TILE_C + c];
				t.sx = c * tw; t.sy = r * th;
				t.sw = (c == TILE_C - 1) ? (s.W - t.sx) : tw;
				t.sh = (r == TILE_R - 1) ? (s.H - t.sy) : th;
				float cx = (float)(t.sx + t.sw / 2 - s.W / 2);
				float cy = (float)(t.sy + t.sh / 2 - s.H / 2);
				float d = sqrtf(cx * cx + cy * cy) + 1.0f;
				float spd = sp_randf_r(180.f, 450.f);
				t.vx = (cx / d) * spd + sp_randf_r(-60.f, 60.f);
				t.vy = (cy / d) * spd + sp_randf_r(-60.f, 60.f);
				t.rot = sp_randf_r(-4.f, 4.f);
				t.delay = sp_randf() * 0.35f;
			}
	}

	static void DrawFade(Gdiplus::Graphics& gfx, float alpha)
	{
		Gdiplus::ColorMatrix cm = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1 };
		Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
		Gdiplus::Rect dr(0, 0, g_sp.W, g_sp.H);
		gfx.DrawImage(g_sp.scaled, dr, 0, 0, g_sp.W, g_sp.H, Gdiplus::UnitPixel, &ia);
	}

	static void DrawRotate(Gdiplus::Graphics& gfx, float t, bool entering)
	{
		float progress = entering ? t : (1.0f - t);
		float eased = entering ? (progress * (2.0f - progress)) : (1.0f - (1.0f - progress) * (1.0f - progress));
		float scale = 0.15f + 0.85f * eased;
		float angle = (1.0f - eased) * (entering ? -540.0f : 540.0f);
		float alpha = eased;
		if (alpha < 0.f) alpha = 0.f; if (alpha > 1.f) alpha = 1.f;
		float cx = (float)g_sp.W / 2.f, cy = (float)g_sp.H / 2.f;
		Gdiplus::Matrix mat;
		mat.Translate(cx, cy); mat.Rotate(angle); mat.Scale(scale, scale); mat.Translate(-cx, -cy);
		gfx.SetTransform(&mat);
		Gdiplus::ColorMatrix cm = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1 };
		Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
		Gdiplus::Rect dr(0, 0, g_sp.W, g_sp.H);
		gfx.DrawImage(g_sp.scaled, dr, 0, 0, g_sp.W, g_sp.H, Gdiplus::UnitPixel, &ia);
		gfx.ResetTransform();
	}

	static void DrawShatter(Gdiplus::Graphics& gfx, float phaseT, bool entering)
	{
		for (int i = 0; i < TILE_N; ++i)
		{
			const auto& t = g_sp.tiles[i];
			float lt = (phaseT - t.delay) / (1.0f - t.delay);
			if (lt < 0.f) lt = 0.f; if (lt > 1.f) lt = 1.f;
			float moveT, alpha;
			if (entering) { moveT = (1.f - lt) * (1.f - lt); alpha = lt * (2.f - lt); }
			else { moveT = lt * lt; alpha = 1.f - lt * lt; }
			if (alpha < 0.f) alpha = 0.f;
			float sec = phaseT * (float)(entering ? g_sp.entryMs : g_sp.exitMs) / 1000.f;
			float dx = t.vx * moveT * sec * 0.5f;
			float dy = t.vy * moveT * sec * 0.5f + 120.f * moveT * sec;
			Gdiplus::ColorMatrix cm = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1 };
			Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
			float ocx = (float)(t.sx + t.sw / 2), ocy = (float)(t.sy + t.sh / 2);
			Gdiplus::Matrix mat;
			mat.RotateAt(t.rot * moveT * sec * 30.f, Gdiplus::PointF(ocx, ocy));
			mat.Translate(dx, dy, Gdiplus::MatrixOrderAppend);
			gfx.SetTransform(&mat);
			Gdiplus::Rect dr(t.sx, t.sy, t.sw, t.sh);
			gfx.DrawImage(g_sp.scaled, dr, t.sx, t.sy, t.sw, t.sh, Gdiplus::UnitPixel, &ia);
			gfx.ResetTransform();
		}
	}

	static void DrawZoom(Gdiplus::Graphics& gfx, float t, bool entering)
	{
		float progress = entering ? t : (1.0f - t);
		float eased = entering ? (progress * (2.0f - progress)) : (1.0f - (1.0f - progress) * (1.0f - progress));
		float scale = entering ? (0.05f + 0.95f * eased) : (0.05f + 0.95f * eased);
		float alpha = eased;
		if (alpha < 0.f) alpha = 0.f; if (alpha > 1.f) alpha = 1.f;
		float cx = (float)g_sp.W / 2.f, cy = (float)g_sp.H / 2.f;
		Gdiplus::Matrix mat;
		mat.Translate(cx, cy); mat.Scale(scale, scale); mat.Translate(-cx, -cy);
		gfx.SetTransform(&mat);
		Gdiplus::ColorMatrix cm = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1 };
		Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
		Gdiplus::Rect dr(0, 0, g_sp.W, g_sp.H);
		gfx.DrawImage(g_sp.scaled, dr, 0, 0, g_sp.W, g_sp.H, Gdiplus::UnitPixel, &ia);
		gfx.ResetTransform();
	}

	static void DrawBlinds(Gdiplus::Graphics& gfx, float t, bool entering)
	{
		const int NUM_BLINDS = 12;
		float progress = entering ? t : (1.0f - t);
		float eased = entering ? (progress * (2.0f - progress)) : (1.0f - (1.0f - progress) * (1.0f - progress));
		int blindH = g_sp.H / NUM_BLINDS;
		for (int i = 0; i < NUM_BLINDS; ++i)
		{
			int y = i * blindH;
			int h = (i == NUM_BLINDS - 1) ? (g_sp.H - y) : blindH;
			int visH = (int)(h * eased);
			if (visH <= 0) continue;
			Gdiplus::Rect dr(0, y, g_sp.W, visH);
			gfx.DrawImage(g_sp.scaled, dr, 0, y, g_sp.W, visH, Gdiplus::UnitPixel);
		}
	}

	static void DrawPixelate(Gdiplus::Graphics& gfx, float t, bool entering)
	{
		float progress = entering ? t : (1.0f - t);
		/* eased: 0→1 for entering, 1→0 for exiting */
		float eased = entering ? (progress * (2.0f - progress)) : (1.0f - (1.0f - progress) * (1.0f - progress));
		/* block size: large when eased is small, 1 when eased is 1 */
		int maxBlock = 48;
		int blockSize = (int)(maxBlock * (1.0f - eased)) + 1;
		if (blockSize < 1) blockSize = 1;

		/* Downscale to create pixelation */
		int smallW = g_sp.W / blockSize; if (smallW < 1) smallW = 1;
		int smallH = g_sp.H / blockSize; if (smallH < 1) smallH = 1;

		Gdiplus::Bitmap tiny(smallW, smallH, PixelFormat32bppPARGB);
		{
			Gdiplus::Graphics tg(&tiny);
			tg.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
			tg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
			tg.DrawImage(g_sp.scaled, 0, 0, smallW, smallH);
		}
		/* Upscale back with nearest-neighbor for blocky look */
		gfx.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

		float alpha = eased;
		if (alpha < 0.f) alpha = 0.f;
		Gdiplus::ColorMatrix cm = { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,alpha,0, 0,0,0,0,1 };
		Gdiplus::ImageAttributes ia; ia.SetColorMatrix(&cm);
		Gdiplus::Rect dr(0, 0, g_sp.W, g_sp.H);
		gfx.DrawImage(&tiny, dr, 0, 0, smallW, smallH, Gdiplus::UnitPixel, &ia);

		/* Restore interpolation mode */
		gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		gfx.SetPixelOffsetMode(Gdiplus::PixelOffsetModeDefault);
	}

	static void SplashRenderFrame(HWND hWnd)
	{
		DWORD elapsed = GetTickCount() - g_sp.startTick;
		if (elapsed > g_sp.totalMs) elapsed = g_sp.totalMs;
		Gdiplus::Bitmap off(g_sp.W, g_sp.H, PixelFormat32bppPARGB);
		Gdiplus::Graphics gfx(&off);
		gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
		gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		gfx.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		gfx.Clear(Gdiplus::Color(0, 0, 0, 0));
		gfx.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
		DWORD entryEnd = g_sp.entryMs, holdEnd = entryEnd + g_sp.holdMs;
		if (elapsed < entryEnd && entryEnd > 0)
		{
			float t = (float)elapsed / (float)entryEnd;
			switch (g_sp.entryFx) {
			case 2: DrawRotate(gfx, t, true); break;
			case 3: DrawShatter(gfx, t, true); break;
			case 4: DrawZoom(gfx, t, true); break;
			case 5: DrawBlinds(gfx, t, true); break;
			case 6: DrawPixelate(gfx, t, true); break;
			default: DrawFade(gfx, t * (2.f - t)); break;
			}
		}
		else if (elapsed <= holdEnd)
		{
			gfx.DrawImage(g_sp.scaled, 0, 0, g_sp.W, g_sp.H);
		}
		else
		{
			float t = (float)(elapsed - holdEnd) / (float)g_sp.exitMs;
			if (t > 1.f) t = 1.f;
			switch (g_sp.exitFx) {
			case 2: DrawRotate(gfx, t, false); break;
			case 3: DrawShatter(gfx, t, false); break;
			case 4: DrawZoom(gfx, t, false); break;
			case 5: DrawBlinds(gfx, t, false); break;
			case 6: DrawPixelate(gfx, t, false); break;
			default: { float a = 1.f - t * t; DrawFade(gfx, a < 0.f ? 0.f : a); } break;
			}
		}
		HBITMAP hBmp = nullptr;
		off.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hBmp);
		HDC scDC = GetDC(nullptr), memDC = CreateCompatibleDC(scDC);
		HBITMAP old = (HBITMAP)SelectObject(memDC, hBmp);
		BLENDFUNCTION bl = {}; bl.BlendOp = AC_SRC_OVER; bl.SourceConstantAlpha = 255; bl.AlphaFormat = AC_SRC_ALPHA;
		POINT src = {0,0}; SIZE sz = {(LONG)g_sp.W,(LONG)g_sp.H}; POINT pos = {(LONG)g_sp.posX,(LONG)g_sp.posY};
		UpdateLayeredWindow(hWnd, scDC, &pos, &sz, memDC, &src, 0, &bl, ULW_ALPHA);
		SelectObject(memDC, old); DeleteObject(hBmp); DeleteDC(memDC); ReleaseDC(nullptr, scDC);
	}

	static LRESULT CALLBACK SplashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_TIMER:
			if (wParam == 1)
			{
				if (GetTickCount() - g_sp.startTick >= g_sp.totalMs)
				{
					KillTimer(hWnd, 1);
					DestroyWindow(hWnd);
				}
				else
				{
					SplashRenderFrame(hWnd);
				}
				return 0;
			}
			break;

		case WM_LBUTTONDOWN:
			if (g_sp.interactionMode == 2)
			{
				/* Click to dismiss: skip to exit phase or close immediately */
				DWORD elapsed = GetTickCount() - g_sp.startTick;
				DWORD exitStart = g_sp.entryMs + g_sp.holdMs;
				if (elapsed < exitStart)
				{
					/* Jump to exit phase start */
					g_sp.startTick = GetTickCount() - exitStart;
				}
				return 0;
			}
			if (g_sp.interactionMode == 1)
			{
				/* Start dragging */
				g_sp.isDragging = true;
				POINT cursorPos;
				GetCursorPos(&cursorPos);
				g_sp.dragMouseStartX = cursorPos.x;
				g_sp.dragMouseStartY = cursorPos.y;
				g_sp.dragWindowStartX = g_sp.posX;
				g_sp.dragWindowStartY = g_sp.posY;
				SetCapture(hWnd);
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
			if (g_sp.isDragging && g_sp.interactionMode == 1)
			{
				POINT cursorPos;
				GetCursorPos(&cursorPos);
				g_sp.posX = g_sp.dragWindowStartX + (cursorPos.x - g_sp.dragMouseStartX);
				g_sp.posY = g_sp.dragWindowStartY + (cursorPos.y - g_sp.dragMouseStartY);
				/* SplashRenderFrame will use the updated posX/posY via UpdateLayeredWindow */
				SplashRenderFrame(hWnd);
				return 0;
			}
			break;

		case WM_LBUTTONUP:
			if (g_sp.isDragging)
			{
				g_sp.isDragging = false;
				ReleaseCapture();
				return 0;
			}
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	/* ================================================================
	 * WebM Splash — 透明 WebM 视频闪屏 + MCI 音频播放
	 * 动态加载 ciallo_webm.dll，支持 VP8/VP9 + Alpha
	 * 音频通过 MCI 播放（兼容 Win7）
	 * ================================================================ */

	/* MCI 动态加载 — 避免与 winmm.dll 代理模式的循环依赖 */
	typedef MCIERROR (WINAPI *PFN_mciSendStringW)(LPCWSTR, LPWSTR, UINT, HWND);
	static PFN_mciSendStringW sg_pfnMciSendStringW = nullptr;
	static HMODULE sg_hRealWinMM = nullptr;

	static PFN_mciSendStringW GetMciSendStringW()
	{
		if (sg_pfnMciSendStringW) return sg_pfnMciSendStringW;
		/* 从系统目录加载真正的 winmm.dll */
		wchar_t sysDir[MAX_PATH] = {};
		GetSystemDirectoryW(sysDir, MAX_PATH);
		wchar_t dllPath[MAX_PATH] = {};
		_snwprintf_s(dllPath, _countof(dllPath), _TRUNCATE, L"%s\\winmm.dll", sysDir);
		sg_hRealWinMM = LoadLibraryW(dllPath);
		if (!sg_hRealWinMM) return nullptr;
		sg_pfnMciSendStringW = (PFN_mciSendStringW)GetProcAddress(sg_hRealWinMM, "mciSendStringW");
		return sg_pfnMciSendStringW;
	}

	static MCIERROR SafeMciSendStringW(LPCWSTR cmd, LPWSTR ret, UINT retLen, HWND hwndCallback)
	{
		PFN_mciSendStringW fn = GetMciSendStringW();
		if (!fn) return MCIERR_DEVICE_NOT_INSTALLED;
		return fn(cmd, ret, retLen, hwndCallback);
	}

	typedef void* CIALLO_WEBM_HANDLE_T;

	typedef struct CialloWebMInfo_T
	{
		uint32_t width;
		uint32_t height;
		int      codec;
		int      has_alpha;
		double   duration_sec;
		double   fps;
		uint32_t frame_count_hint;
	} CialloWebMInfo_T;

	typedef struct CialloWebMFrame_T
	{
		const uint8_t* pixels;
		uint32_t       stride;
		uint32_t       width;
		uint32_t       height;
		uint32_t       duration_ms;
		double         timestamp;
		int            is_key;
	} CialloWebMFrame_T;

	typedef CIALLO_WEBM_HANDLE_T (*PFN_WebM_OpenMemory)(const uint8_t*, size_t);
	typedef int  (*PFN_WebM_GetInfo)(CIALLO_WEBM_HANDLE_T, CialloWebMInfo_T*);
	typedef int  (*PFN_WebM_ReadFrame)(CIALLO_WEBM_HANDLE_T, CialloWebMFrame_T*);
	typedef int  (*PFN_WebM_Rewind)(CIALLO_WEBM_HANDLE_T);
	typedef void (*PFN_WebM_Close)(CIALLO_WEBM_HANDLE_T);
	typedef int  (*PFN_WebM_HasAudio)(CIALLO_WEBM_HANDLE_T);
	typedef int  (*PFN_WebM_ExtractAudioWav)(CIALLO_WEBM_HANDLE_T, const wchar_t*);

	struct WebMSplashAPI
	{
		HMODULE             hDll;
		PFN_WebM_OpenMemory pfnOpen;
		PFN_WebM_GetInfo    pfnGetInfo;
		PFN_WebM_ReadFrame  pfnRead;
		PFN_WebM_Rewind     pfnRewind;
		PFN_WebM_Close      pfnClose;
		PFN_WebM_HasAudio   pfnHasAudio;
		PFN_WebM_ExtractAudioWav pfnExtractWav;
	};

	struct WebMSplashState
	{
		WebMSplashAPI       api;
		CIALLO_WEBM_HANDLE_T decoder;
		int W, H, posX, posY;
		int interactionMode;
		DWORD totalMs;       /* DurationMs 上限，0 = 不限制（播完为止） */
		DWORD startTick;
		bool isDragging;
		int dragMouseStartX, dragMouseStartY;
		int dragWindowStartX, dragWindowStartY;
		bool hasAudio;
		bool mciOpened;
		wchar_t mciAlias[32];
		wchar_t tempFilePath[MAX_PATH];
		CialloWebMFrame_T curFrame;
		bool frameReady;
		bool finished;
	};
	static WebMSplashState g_ws = {};

	static bool WebM_MCI_Open(WebMSplashState& ws)
	{
		if (ws.tempFilePath[0] == L'\0') return false;
		wchar_t cmd[512] = {};
		_snwprintf_s(cmd, _countof(cmd), _TRUNCATE,
			L"open \"%s\" type waveaudio alias %s",
			ws.tempFilePath, ws.mciAlias);
		MCIERROR err = SafeMciSendStringW(cmd, nullptr, 0, nullptr);
		if (err != 0) return false;
		ws.mciOpened = true;
		return true;
	}

	static void WebM_MCI_Play(WebMSplashState& ws)
	{
		if (!ws.mciOpened) return;
		wchar_t cmd[128] = {};
		_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"play %s from 0", ws.mciAlias);
		SafeMciSendStringW(cmd, nullptr, 0, nullptr);
	}

	static DWORD WebM_MCI_GetPositionMs(WebMSplashState& ws)
	{
		if (!ws.mciOpened) return 0;
		wchar_t cmd[128] = {};
		_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"status %s position", ws.mciAlias);
		wchar_t buf[64] = {};
		if (SafeMciSendStringW(cmd, buf, _countof(buf), nullptr) != 0) return 0;
		return (DWORD)_wtol(buf);
	}

	static void WebM_MCI_Close(WebMSplashState& ws)
	{
		if (!ws.mciOpened) return;
		wchar_t cmd[128] = {};
		_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"close %s", ws.mciAlias);
		SafeMciSendStringW(cmd, nullptr, 0, nullptr);
		ws.mciOpened = false;
	}

	static void WebM_MCI_SetTimeFormatMs(WebMSplashState& ws)
	{
		if (!ws.mciOpened) return;
		wchar_t cmd[128] = {};
		_snwprintf_s(cmd, _countof(cmd), _TRUNCATE, L"set %s time format milliseconds", ws.mciAlias);
		SafeMciSendStringW(cmd, nullptr, 0, nullptr);
	}

	static void WebMSplash_RenderFrame(HWND hWnd)
	{
		if (!g_ws.frameReady) return;
		const CialloWebMFrame_T& frame = g_ws.curFrame;
		int W = g_ws.W, H = g_ws.H;

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = (LONG)W;
		bmi.bmiHeader.biHeight = -(LONG)H;
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		HDC scDC = GetDC(nullptr);
		HDC memDC = CreateCompatibleDC(scDC);
		void* bits = nullptr;
		HBITMAP hBmp = CreateDIBSection(scDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (hBmp && bits)
		{
			if (frame.width == (uint32_t)W && frame.height == (uint32_t)H)
			{
				memcpy(bits, frame.pixels, frame.stride * frame.height);
			}
			else
			{
				/* 双线性缩放（最近邻保持性能） */
				uint8_t* dst = (uint8_t*)bits;
				for (int y = 0; y < H; ++y)
				{
					int srcY = y * (int)frame.height / H;
					const uint8_t* srcRow = frame.pixels + srcY * frame.stride;
					for (int x = 0; x < W; ++x)
					{
						int srcX = x * (int)frame.width / W;
						const uint8_t* sp = srcRow + srcX * 4;
						dst[0] = sp[0]; dst[1] = sp[1]; dst[2] = sp[2]; dst[3] = sp[3];
						dst += 4;
					}
				}
			}

			HBITMAP old = (HBITMAP)SelectObject(memDC, hBmp);
			BLENDFUNCTION bl = {};
			bl.BlendOp = AC_SRC_OVER;
			bl.SourceConstantAlpha = 255;
			bl.AlphaFormat = AC_SRC_ALPHA;
			POINT src = { 0, 0 };
			SIZE sz = { (LONG)W, (LONG)H };
			POINT pos = { (LONG)g_ws.posX, (LONG)g_ws.posY };
			UpdateLayeredWindow(hWnd, scDC, &pos, &sz, memDC, &src, 0, &bl, ULW_ALPHA);
			SelectObject(memDC, old);
			DeleteObject(hBmp);
		}
		DeleteDC(memDC);
		ReleaseDC(nullptr, scDC);
	}

	static LRESULT CALLBACK WebMSplashWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_TIMER:
			if (wParam == 1)
			{
				if (g_ws.finished)
				{
					KillTimer(hWnd, 1);
					DestroyWindow(hWnd);
					return 0;
				}

				/* 计算当前应显示的时间位置 */
				DWORD elapsedMs;
				if (g_ws.hasAudio && g_ws.mciOpened)
				{
					/* 音频同步：以 MCI 播放位置为准 */
					elapsedMs = WebM_MCI_GetPositionMs(g_ws);
				}
				else
				{
					/* 无音频：使用系统时钟 */
					elapsedMs = GetTickCount() - g_ws.startTick;
				}

				/* DurationMs 上限检查 */
				if (g_ws.totalMs > 0 && elapsedMs >= g_ws.totalMs)
				{
					g_ws.finished = true;
					KillTimer(hWnd, 1);
					DestroyWindow(hWnd);
					return 0;
				}

				/* 推进帧直到追上当前时间 */
				double targetSec = (double)elapsedMs / 1000.0;
				while (g_ws.frameReady && g_ws.curFrame.timestamp + (double)g_ws.curFrame.duration_ms / 1000.0 < targetSec)
				{
					int rc = g_ws.api.pfnRead(g_ws.decoder, &g_ws.curFrame);
					if (rc != 0) /* CIALLO_WEBM_OK == 0 */
					{
						g_ws.finished = true;
						KillTimer(hWnd, 1);
						DestroyWindow(hWnd);
						return 0;
					}
				}

				WebMSplash_RenderFrame(hWnd);
				return 0;
			}
			break;

		case WM_LBUTTONDOWN:
			if (g_ws.interactionMode == 2)
			{
				/* 点击跳过 */
				g_ws.finished = true;
				KillTimer(hWnd, 1);
				DestroyWindow(hWnd);
				return 0;
			}
			if (g_ws.interactionMode == 1)
			{
				g_ws.isDragging = true;
				POINT cursorPos;
				GetCursorPos(&cursorPos);
				g_ws.dragMouseStartX = cursorPos.x;
				g_ws.dragMouseStartY = cursorPos.y;
				g_ws.dragWindowStartX = g_ws.posX;
				g_ws.dragWindowStartY = g_ws.posY;
				SetCapture(hWnd);
				return 0;
			}
			break;

		case WM_MOUSEMOVE:
			if (g_ws.isDragging && g_ws.interactionMode == 1)
			{
				POINT cursorPos;
				GetCursorPos(&cursorPos);
				g_ws.posX = g_ws.dragWindowStartX + (cursorPos.x - g_ws.dragMouseStartX);
				g_ws.posY = g_ws.dragWindowStartY + (cursorPos.y - g_ws.dragMouseStartY);
				WebMSplash_RenderFrame(hWnd);
				return 0;
			}
			break;

		case WM_LBUTTONUP:
			if (g_ws.isDragging)
			{
				g_ws.isDragging = false;
				ReleaseCapture();
				return 0;
			}
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}

	static void RunWebMSplashAnimation(
		HMODULE hWebM,
		const uint8_t* webmData, size_t webmSize,
		const SplashImageSettings& settings)
	{
		if (!hWebM) return;

		memset(&g_ws, 0, sizeof(g_ws));
		g_ws.api.hDll = hWebM;
		g_ws.api.pfnOpen    = (PFN_WebM_OpenMemory)GetProcAddress(hWebM, "CialloWebM_OpenMemory");
		g_ws.api.pfnGetInfo = (PFN_WebM_GetInfo)GetProcAddress(hWebM, "CialloWebM_GetInfo");
		g_ws.api.pfnRead    = (PFN_WebM_ReadFrame)GetProcAddress(hWebM, "CialloWebM_ReadFrame");
		g_ws.api.pfnRewind  = (PFN_WebM_Rewind)GetProcAddress(hWebM, "CialloWebM_Rewind");
		g_ws.api.pfnClose   = (PFN_WebM_Close)GetProcAddress(hWebM, "CialloWebM_Close");
		g_ws.api.pfnHasAudio = (PFN_WebM_HasAudio)GetProcAddress(hWebM, "CialloWebM_HasAudio");
		g_ws.api.pfnExtractWav = (PFN_WebM_ExtractAudioWav)GetProcAddress(hWebM, "CialloWebM_ExtractAudioWav");

		if (!g_ws.api.pfnOpen || !g_ws.api.pfnGetInfo || !g_ws.api.pfnRead || !g_ws.api.pfnClose)
		{
			return;
		}

		g_ws.decoder = g_ws.api.pfnOpen(webmData, webmSize);
		if (!g_ws.decoder)
		{
			return;
		}

		CialloWebMInfo_T info;
		g_ws.api.pfnGetInfo(g_ws.decoder, &info);

		g_ws.W = settings.width > 0 ? settings.width : (int)info.width;
		g_ws.H = settings.height > 0 ? settings.height : (int)info.height;
		g_ws.interactionMode = settings.interactionMode;
		g_ws.totalMs = settings.durationMs > 0 ? (DWORD)settings.durationMs : 0;
		g_ws.finished = false;
		g_ws.isDragging = false;
		wcscpy_s(g_ws.mciAlias, L"ciallo_webm_audio");

		/* 位置计算 */
		const int MARGIN = 20;
		int screenW = GetSystemMetrics(SM_CXSCREEN);
		int screenH = GetSystemMetrics(SM_CYSCREEN);
		switch (settings.position)
		{
		case 2: g_ws.posX = MARGIN; g_ws.posY = MARGIN; break;
		case 3: g_ws.posX = screenW - g_ws.W - MARGIN; g_ws.posY = MARGIN; break;
		case 4: g_ws.posX = MARGIN; g_ws.posY = screenH - g_ws.H - MARGIN; break;
		case 5: g_ws.posX = screenW - g_ws.W - MARGIN; g_ws.posY = screenH - g_ws.H - MARGIN; break;
		default: g_ws.posX = (screenW - g_ws.W) / 2; g_ws.posY = (screenH - g_ws.H) / 2; break;
		}

		/* 音频处理：如果 WebM 含有音频，解码为 WAV 并用 MCI 播放 */
		g_ws.hasAudio = false;
		g_ws.mciOpened = false;
		g_ws.tempFilePath[0] = L'\0';

		if (g_ws.api.pfnHasAudio && g_ws.api.pfnHasAudio(g_ws.decoder) &&
		    g_ws.api.pfnExtractWav)
		{
			/* 生成临时 WAV 文件路径 */
			wchar_t tempDir[MAX_PATH] = {};
			if (GetTempPathW(MAX_PATH, tempDir))
			{
				wchar_t tempBase[MAX_PATH] = {};
				if (GetTempFileNameW(tempDir, L"cwa", 0, tempBase))
				{
					DeleteFileW(tempBase);
					_snwprintf_s(g_ws.tempFilePath, _countof(g_ws.tempFilePath),
						_TRUNCATE, L"%s.wav", tempBase);
					/* 调用 DLL 解码 Vorbis → PCM WAV */
					int wavResult = g_ws.api.pfnExtractWav(g_ws.decoder, g_ws.tempFilePath);
					if (wavResult == 0 && WebM_MCI_Open(g_ws))
					{
						WebM_MCI_SetTimeFormatMs(g_ws);
						g_ws.hasAudio = true;
					}
					else
					{
						DeleteFileW(g_ws.tempFilePath);
						g_ws.tempFilePath[0] = L'\0';
					}
				}
			}
		}

		/* 预读第一帧 */
		int rc = g_ws.api.pfnRead(g_ws.decoder, &g_ws.curFrame);
		if (rc != 0)
		{
			/* 无法读取任何帧，清理退出 */
			WebM_MCI_Close(g_ws);
			if (g_ws.tempFilePath[0]) DeleteFileW(g_ws.tempFilePath);
			g_ws.api.pfnClose(g_ws.decoder);
			return;
		}
		g_ws.frameReady = true;

		/* 创建分层窗口 */
		HINSTANCE hInst = GetModuleHandleW(nullptr);
		static const wchar_t* cls = L"CialloWebMSplashClass";
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = WebMSplashWndProc;
		wc.hInstance = hInst;
		wc.hCursor = LoadCursorW(nullptr, settings.interactionMode == 1 ? IDC_SIZEALL : IDC_ARROW);
		wc.lpszClassName = cls;
		RegisterClassExW(&wc);

		HWND hWnd = CreateWindowExW(
			WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
			cls, L"", WS_POPUP,
			g_ws.posX, g_ws.posY, g_ws.W, g_ws.H,
			nullptr, nullptr, hInst, nullptr);

		if (!hWnd)
		{
			WebM_MCI_Close(g_ws);
			if (g_ws.tempFilePath[0]) DeleteFileW(g_ws.tempFilePath);
			g_ws.api.pfnClose(g_ws.decoder);
			return;
		}

		/* 渲染第一帧并显示窗口 */
		WebMSplash_RenderFrame(hWnd);
		ShowWindow(hWnd, SW_SHOW);

		/* 开始音频播放（与第一帧显示同步） */
		g_ws.startTick = GetTickCount();
		if (g_ws.hasAudio && g_ws.mciOpened)
		{
			WebM_MCI_Play(g_ws);
		}

		/* 约 60fps 定时器 */
		SetTimer(hWnd, 1, 16, nullptr);

		/* 消息循环 */
		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		/* 清理 */
		UnregisterClassW(cls, hInst);
		WebM_MCI_Close(g_ws);
		if (g_ws.tempFilePath[0])
		{
			DeleteFileW(g_ws.tempFilePath);
			g_ws.tempFilePath[0] = L'\0';
		}
		g_ws.api.pfnClose(g_ws.decoder);
		g_ws.decoder = nullptr;
	}

	/* ================================================================
	 * 原有 GDI+ 图片闪屏
	 * ================================================================ */

	static void RunSplashAnimation(const uint8_t* imgData, size_t imgSize, const SplashImageSettings& settings)
	{
		HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, imgSize);
		if (!hg) return;
		void* pg = GlobalLock(hg); memcpy(pg, imgData, imgSize); GlobalUnlock(hg);
		Gdiplus::GdiplusStartupInput gi; ULONG_PTR gt=0;
		if (Gdiplus::GdiplusStartup(&gt, &gi, nullptr) != Gdiplus::Ok) { GlobalFree(hg); return; }
		IStream* ps=nullptr; CreateStreamOnHGlobal(hg, TRUE, &ps);
		auto* srcBmp = new Gdiplus::Bitmap(ps); ps->Release();
		if (srcBmp->GetLastStatus() != Gdiplus::Ok) { delete srcBmp; Gdiplus::GdiplusShutdown(gt); return; }
		int W = settings.width, H = settings.height;
		auto* sc = new Gdiplus::Bitmap(W, H, PixelFormat32bppPARGB);
		{ Gdiplus::Graphics g(sc); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		  g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy); g.DrawImage(srcBmp, 0, 0, W, H); }
		delete srcBmp;
		g_sp.scaled = sc; g_sp.W = W; g_sp.H = H;
		g_sp.entryFx = settings.entryEffect; g_sp.exitFx = settings.exitEffect;
		g_sp.entryMs = settings.entryMs; g_sp.holdMs = settings.holdMs; g_sp.exitMs = settings.exitMs;
		g_sp.interactionMode = settings.interactionMode;
		g_sp.isDragging = false;

		/* Calculate total duration: DurationMs overrides if set */
		DWORD autoTotal = g_sp.entryMs + g_sp.holdMs + g_sp.exitMs;
		if (settings.durationMs > 0)
		{
			g_sp.totalMs = (DWORD)settings.durationMs;
			/* If overridden duration is shorter, adjust hold/exit proportionally */
			if (g_sp.totalMs < g_sp.entryMs)
			{
				g_sp.entryMs = g_sp.totalMs;
				g_sp.holdMs = 0;
				g_sp.exitMs = 0;
			}
			else if (g_sp.totalMs < g_sp.entryMs + g_sp.holdMs)
			{
				g_sp.holdMs = g_sp.totalMs - g_sp.entryMs;
				g_sp.exitMs = 0;
			}
			else if (g_sp.totalMs < autoTotal)
			{
				g_sp.exitMs = g_sp.totalMs - g_sp.entryMs - g_sp.holdMs;
			}
		}
		else
		{
			g_sp.totalMs = autoTotal;
		}

		/* Calculate position based on settings.position
		 * 1=center  2=top-left  3=top-right  4=bottom-left  5=bottom-right */
		{
			const int MARGIN = 20;
			int screenW = GetSystemMetrics(SM_CXSCREEN);
			int screenH = GetSystemMetrics(SM_CYSCREEN);
			switch (settings.position)
			{
			case 2: /* top-left */
				g_sp.posX = MARGIN;
				g_sp.posY = MARGIN;
				break;
			case 3: /* top-right */
				g_sp.posX = screenW - W - MARGIN;
				g_sp.posY = MARGIN;
				break;
			case 4: /* bottom-left */
				g_sp.posX = MARGIN;
				g_sp.posY = screenH - H - MARGIN;
				break;
			case 5: /* bottom-right */
				g_sp.posX = screenW - W - MARGIN;
				g_sp.posY = screenH - H - MARGIN;
				break;
			default: /* 1 = center */
				g_sp.posX = (screenW - W) / 2;
				g_sp.posY = (screenH - H) / 2;
				break;
			}
		}

		if (g_sp.entryFx == 3 || g_sp.exitFx == 3) InitTiles(g_sp);
		HINSTANCE hInst = GetModuleHandleW(nullptr);
		static const wchar_t* cls = L"CialloSplashClass";
		WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = SplashWndProc;
		wc.hInstance = hInst;
		/* Use move cursor for draggable mode, normal arrow otherwise */
		wc.hCursor = LoadCursorW(nullptr, settings.interactionMode == 1 ? IDC_SIZEALL : IDC_ARROW);
		wc.lpszClassName = cls; RegisterClassExW(&wc);
		HWND hWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
			cls, L"", WS_POPUP, g_sp.posX, g_sp.posY, W, H, nullptr, nullptr, hInst, nullptr);
		if (!hWnd) { delete sc; g_sp.scaled = nullptr; Gdiplus::GdiplusShutdown(gt); return; }
		g_sp.startTick = GetTickCount();
		SplashRenderFrame(hWnd);
		ShowWindow(hWnd, SW_SHOW);
		SetTimer(hWnd, 1, 16, nullptr);
		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
		UnregisterClassW(cls, hInst);
		delete sc; g_sp.scaled = nullptr;
		Gdiplus::GdiplusShutdown(gt);
	}

	/* SEH wrapper — no C++ objects on stack so __try is legal */
	static void RunSplashAnimationSafe(const uint8_t* data, size_t size, const SplashImageSettings* pSettings)
	{
		__try { RunSplashAnimation(data, size, *pSettings); }
		__except (EXCEPTION_EXECUTE_HANDLER) { /* splash failed, continue to game */ }
	}

	static void RunWebMSplashAnimationSafe(HMODULE hWebM, const uint8_t* data, size_t size, const SplashImageSettings* pSettings)
	{
		__try { RunWebMSplashAnimation(hWebM, data, size, *pSettings); }
		__except (EXCEPTION_EXECUTE_HANDLER) { /* WebM splash failed, continue to game */ }
	}

	static bool ReadFileToVector(const std::wstring& path, std::vector<uint8_t>& out)
	{
		HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		if (hf == INVALID_HANDLE_VALUE) return false;
		DWORD sz = GetFileSize(hf, nullptr);
		if (sz == 0 || sz == INVALID_FILE_SIZE) { CloseHandle(hf); return false; }
		out.resize(sz);
		DWORD br = 0; ReadFile(hf, out.data(), sz, &br, nullptr); CloseHandle(hf);
		return (br == sz);
	}

	void HookManager::ShowSplashFromEntryPoint(HMODULE dllModule)
	{
#if !CIALLOHOOK_FEATURE_SPLASH_IMAGE
		(void)dllModule;
		return;
#else
		const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);
		if (context.iniPath.empty()) return;

		wchar_t val[16] = {};
		GetPrivateProfileStringW(L"SplashImage", L"Enable", L"false", val, 16, context.iniPath.c_str());
		bool enabled = (lstrcmpiW(val, L"true") == 0 || lstrcmpiW(val, L"1") == 0 || lstrcmpiW(val, L"yes") == 0);
		if (!enabled) return;

		SplashImageSettings ss;
		ss.enable = true;
		wchar_t buf[256] = {};
		GetPrivateProfileStringW(L"SplashImage", L"ImageFile", L"splash.png", buf, 256, context.iniPath.c_str());
		ss.imageFile = buf;
		ss.width = GetPrivateProfileIntW(L"SplashImage", L"Width", 800, context.iniPath.c_str());
		ss.height = GetPrivateProfileIntW(L"SplashImage", L"Height", 600, context.iniPath.c_str());
		ss.entryEffect = GetPrivateProfileIntW(L"SplashImage", L"EntryEffect", 1, context.iniPath.c_str());
		ss.exitEffect = GetPrivateProfileIntW(L"SplashImage", L"ExitEffect", 1, context.iniPath.c_str());
		ss.entryMs = GetPrivateProfileIntW(L"SplashImage", L"EntryMs", 1200, context.iniPath.c_str());
		ss.holdMs = GetPrivateProfileIntW(L"SplashImage", L"HoldMs", 1800, context.iniPath.c_str());
		ss.exitMs = GetPrivateProfileIntW(L"SplashImage", L"ExitMs", 1500, context.iniPath.c_str());
		ss.durationMs = GetPrivateProfileIntW(L"SplashImage", L"DurationMs", 0, context.iniPath.c_str());
		ss.position = GetPrivateProfileIntW(L"SplashImage", L"Position", 1, context.iniPath.c_str());
		ss.interactionMode = GetPrivateProfileIntW(L"SplashImage", L"InteractionMode", 0, context.iniPath.c_str());

		wchar_t ep[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, ep, MAX_PATH);
		std::wstring gameDir(ep);
		auto sep = gameDir.find_last_of(L"/\\");
		if (sep != std::wstring::npos) gameDir = gameDir.substr(0, sep + 1);

		auto isAbsPath = [](const std::wstring& path) -> bool
		{
			return (path.size() >= 2 && path[1] == L':') || (path.size() >= 2 && path[0] == 0x5c && path[1] == 0x5c);
		};
		auto joinGame = [&](const std::wstring& path) -> std::wstring
		{
			return isAbsPath(path) ? path : (gameDir + path);
		};

		std::vector<std::wstring> patchFolders;
		int patchFolderCount = GetPrivateProfileIntW(L"FilePatch", L"PatchFolderCount", -1, context.iniPath.c_str());
		if (patchFolderCount >= 0)
		{
			for (int i = 0; i < patchFolderCount; ++i)
			{
				wchar_t key[64] = {};
				_snwprintf_s(key, _countof(key), _TRUNCATE, L"PatchFolderName_%d", i);
				wchar_t value[MAX_PATH] = {};
				GetPrivateProfileStringW(L"FilePatch", key, L"", value, MAX_PATH, context.iniPath.c_str());
				if (value[0] != L'\0') patchFolders.push_back(value);
			}
		}
		else
		{
			wchar_t value[MAX_PATH] = {};
			GetPrivateProfileStringW(L"FilePatch", L"PatchFolderName_0", L"patch", value, MAX_PATH, context.iniPath.c_str());
			if (value[0] != L'\0') patchFolders.push_back(value);
		}

		std::vector<std::wstring> cpkFiles;
		wchar_t cpkEnabled[16] = {};
		GetPrivateProfileStringW(L"FilePatch", L"CustomPakEnable", L"false", cpkEnabled, 16, context.iniPath.c_str());
		bool useCpk = (lstrcmpiW(cpkEnabled, L"true") == 0 || lstrcmpiW(cpkEnabled, L"1") == 0 || lstrcmpiW(cpkEnabled, L"yes") == 0);
		wchar_t cpkLogEnabled[16] = {};
		GetPrivateProfileStringW(L"FilePatch", L"EnableLog", L"false", cpkLogEnabled, 16, context.iniPath.c_str());
		bool enableCpkLog = (lstrcmpiW(cpkLogEnabled, L"true") == 0 || lstrcmpiW(cpkLogEnabled, L"1") == 0 || lstrcmpiW(cpkLogEnabled, L"yes") == 0);
		if (useCpk)
		{
			int cpkCount = GetPrivateProfileIntW(L"FilePatch", L"CustomPakCount", -1, context.iniPath.c_str());
			if (cpkCount >= 0)
			{
				for (int i = 0; i < cpkCount; ++i)
				{
					wchar_t key[64] = {};
					_snwprintf_s(key, _countof(key), _TRUNCATE, L"CustomPakName_%d", i);
					wchar_t value[MAX_PATH] = {};
					GetPrivateProfileStringW(L"FilePatch", key, L"", value, MAX_PATH, context.iniPath.c_str());
					if (value[0] != L'\0') cpkFiles.push_back(value);
				}
			}
			else
			{
				wchar_t value[MAX_PATH] = {};
				GetPrivateProfileStringW(L"FilePatch", L"CustomPakName_0", L"patch.cpk", value, MAX_PATH, context.iniPath.c_str());
				if (value[0] != L'\0') cpkFiles.push_back(value);
			}
		}

		if (useCpk && !cpkFiles.empty())
		{
			std::vector<const wchar_t*> pakPaths;
			pakPaths.reserve(cpkFiles.size());
			for (const std::wstring& pakFile : cpkFiles)
			{
				pakPaths.push_back(pakFile.c_str());
			}
			ConfigureCustomPakVFS(true, pakPaths.data(), pakPaths.size(), enableCpkLog);
		}

		std::vector<uint8_t> imgData;
		bool found = false;

		for (size_t i = patchFolders.size(); !found && i > 0; --i)
		{
			found = ReadFileToVector(joinGame(patchFolders[i - 1]) + L"\\" + ss.imageFile, imgData);
		}
		for (size_t i = cpkFiles.size(); !found && i > 0; --i)
		{
			std::shared_ptr<const std::vector<uint8_t>> cpkData;
			if (ResolveCustomPakArchiveData(joinGame(cpkFiles[i - 1]).c_str(), ss.imageFile.c_str(), cpkData) && cpkData && !cpkData->empty())
			{
				imgData.assign(cpkData->begin(), cpkData->end());
				found = true;
			}
		}
		if (!found) { found = ReadFileToVector(joinGame(ss.imageFile), imgData); }

		if (!found || imgData.empty()) return;

		/* 检测 EBML/WebM 魔术头: 0x1A 0x45 0xDF 0xA3 */
		bool isWebM = false;
		if (imgData.size() >= 4)
		{
			isWebM = (imgData[0] == 0x1A && imgData[1] == 0x45 &&
			          imgData[2] == 0xDF && imgData[3] == 0xA3);
		}

		if (isWebM)
		{
			/* 搜索 ciallo_webm.dll: PatchFolder → CustomPAK → 游戏根目录 → 默认搜索路径 */
			HMODULE hWebMDll = nullptr;
			wchar_t tempDllPath[MAX_PATH] = {};
			const wchar_t* dllName = L"ciallo_webm.dll";

			/* 1. PatchFolder（编号越高优先级越高） */
			for (size_t i = patchFolders.size(); !hWebMDll && i > 0; --i)
			{
				std::wstring dllPath = joinGame(patchFolders[i - 1]) + L"\\" + dllName;
				hWebMDll = LoadLibraryW(dllPath.c_str());
			}

			/* 2. CustomPAK — 提取到临时文件后加载 */
			for (size_t i = cpkFiles.size(); !hWebMDll && i > 0; --i)
			{
				std::shared_ptr<const std::vector<uint8_t>> dllData;
				if (ResolveCustomPakArchiveData(joinGame(cpkFiles[i - 1]).c_str(), dllName, dllData)
					&& dllData && !dllData->empty())
				{
					wchar_t td[MAX_PATH] = {};
					if (GetTempPathW(MAX_PATH, td))
					{
						wchar_t tb[MAX_PATH] = {};
						if (GetTempFileNameW(td, L"cwd", 0, tb))
						{
							DeleteFileW(tb);
							_snwprintf_s(tempDllPath, _countof(tempDllPath), _TRUNCATE, L"%s.dll", tb);
							HANDLE hf = CreateFileW(tempDllPath, GENERIC_WRITE, 0, nullptr,
								CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
							if (hf != INVALID_HANDLE_VALUE)
							{
								DWORD bw = 0;
								WriteFile(hf, dllData->data(), (DWORD)dllData->size(), &bw, nullptr);
								CloseHandle(hf);
								hWebMDll = LoadLibraryW(tempDllPath);
								if (!hWebMDll) { DeleteFileW(tempDllPath); tempDllPath[0] = L'\0'; }
							}
						}
					}
				}
			}

			/* 3. 游戏根目录 */
			if (!hWebMDll)
			{
				hWebMDll = LoadLibraryW((gameDir + dllName).c_str());
			}

			/* 4. 系统默认搜索路径 */
			if (!hWebMDll)
			{
				hWebMDll = LoadLibraryW(dllName);
			}

			if (hWebMDll)
			{
				RunWebMSplashAnimationSafe(hWebMDll, imgData.data(), imgData.size(), &ss);
				FreeLibrary(hWebMDll);
			}

			/* 清理从 CustomPAK 提取的临时 DLL 文件 */
			if (tempDllPath[0] != L'\0')
			{
				DeleteFileW(tempDllPath);
			}
		}
		else
		{
			RunSplashAnimationSafe(imgData.data(), imgData.size(), &ss);
		}
#endif
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
		const UINT currentAcp = GetACP();
		if (currentAcp == settings.localeEmulator.ansiCodePage && settings.localeEmulator.hookUILanguageAPI == 0)
		{
			LogMessage(LogLevel::Debug, L"LocaleEmulator skipped: ACP already %u", settings.localeEmulator.ansiCodePage);
			return false;
		}

		if (currentAcp == settings.localeEmulator.ansiCodePage)
		{
			LogMessage(LogLevel::Info, L"LocaleEmulator relaunch required: ACP already %u, HookUILanguageAPI=%u",
				settings.localeEmulator.ansiCodePage,
				settings.localeEmulator.hookUILanguageAPI);
		}
		else
		{
			LogMessage(LogLevel::Info, L"LocaleEmulator relaunch required: ACP=%u -> %u",
				currentAcp, settings.localeEmulator.ansiCodePage);
		}
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
				LogMessage(LogLevel::Error, L"LocaleEmulator: LoaderDll missing - no LE or LEP loader found in configured FilePatch targets (loading failed, GetLastError=%u)", errorCode);
				MessageBoxW(NULL, L"LoaderDll (LE/LEP) 已在 FilePatch 配置的目录或封包中找到，但加载失败", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
			}
			else
			{
				LogMessage(LogLevel::Error, L"LocaleEmulator: no LE or LEP loader found locally or in configured FilePatch targets (GetLastError=%u)", errorCode);
				MessageBoxW(NULL, L"LoaderDll.dll / LoaderDll_x86.dll / LoaderDll_x64.dll missing", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
			}
			SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
			return false;
		}
		if (loaderFromCustomPak)
		{
			CIALLOHOOK_VERBOSE_INFO_LOG(L"LocaleEmulator: loader loaded from configured CustomPak cache");
		}
		else if (loaderFromConfiguredOverride)
		{
			CIALLOHOOK_VERBOSE_INFO_LOG(L"LocaleEmulator: loader loaded from configured patch path");
		}

		// Try LEP first (LepCreateProcess / LepCreateProcess2), then fall back to LE (LeCreateProcess)
		PFN_LepCreateProcess lepCreateProcess = (PFN_LepCreateProcess)GetProcAddress(loaderDll, "LepCreateProcess");
		PFN_LeCreateProcess leCreateProcess = nullptr;
		bool usingLEP = false;
		if (lepCreateProcess)
		{
			usingLEP = true;
			LogMessage(LogLevel::Info, L"LocaleEmulator: using LEP (LepCreateProcess)");
		}
		else
		{
			leCreateProcess = (PFN_LeCreateProcess)GetProcAddress(loaderDll, "LeCreateProcess");
			if (!leCreateProcess)
			{
				LogMessage(LogLevel::Error, L"LocaleEmulator: neither LepCreateProcess nor LeCreateProcess found in loader");
				MessageBoxW(NULL, L"LepCreateProcess / LeCreateProcess not found in LoaderDll", L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
				FreeLibrary(loaderDll);
				CleanupPreparedRuntimeFiles(preparedPaths);
				SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
				return false;
			}
			LogMessage(LogLevel::Info, L"LocaleEmulator: using LE (LeCreateProcess)");
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
			LocaleEmulatorFileStageOptions stageOptions;
			stageOptions.logPrefix = L"LocaleEmulator";
			StageLocaleEmulatorFilesNextToExe(preparedPaths, exeDir, stageOptions, stagedPaths);
			if (!stagedPaths.empty())
			{
				AppendUniquePath(runtimeSearchDirs, exeDir);
				const std::wstring stagedValue = BuildDelimitedEnvironmentValue(stagedPaths);
				if (!stagedValue.empty())
				{
					originalStagedFilesValue = GetEnvironmentVariableString(kLocaleEmulatorStagedFilesEnvVar, originalStagedFilesValueExists);
					SetEnvironmentVariableW(kLocaleEmulatorStagedFilesEnvVar, stagedValue.c_str());
				}
				CIALLOHOOK_VERBOSE_INFO_LOG(L"LocaleEmulator: staged %u runtime file(s) next to exe for proxy relaunch",
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
		DWORD result;
		if (usingLEP)
		{
			LONG lepResult = lepCreateProcess(
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
			result = static_cast<DWORD>(lepResult);
		}
		else
		{
			result = leCreateProcess(
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
		}

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
			LogMessage(LogLevel::Info, L"LocaleEmulator relaunch succeeded (%s)", usingLEP ? L"LEP" : L"LE");
			ExitProcess(0);
			return true;
		}

		LogMessage(LogLevel::Error, L"LocaleEmulator relaunch failed (%s): 0x%08X", usingLEP ? L"LEP" : L"LE", result);
		wchar_t errorText[256] = {};
		swprintf_s(errorText, L"%s failed: 0x%08X", usingLEP ? L"LepCreateProcess" : L"LeCreateProcess", result);
		MessageBoxW(NULL, errorText, L"CialloHook - LE Error", MB_OK | MB_ICONERROR);
		SetEnvironmentVariableW(L"CIALLOHOOK_LE_ACTIVE", nullptr);
		return false;
	}

	bool HookManager::TryEarlyLocaleEmulatorRelaunch(HMODULE dllModule)
	{
#if !CIALLOHOOK_FEATURE_LOCALE_EMULATOR
		(void)dllModule;
		return false;
#else
		try
		{
			const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);

			AppSettings settings;
			std::string errorMessage;
			if (!TryLoadModuleSettings(context, settings, errorMessage, nullptr))
			{
				return false;
			}

			return TryRelaunchWithLocaleEmulator(settings);
		}
		catch (...)
		{
			return false;
		}
#endif
	}

	bool HookManager::TryHandleConsentInDllMain(HMODULE dllModule)
	{
#if !CIALLOHOOK_FEATURE_STARTUP_MESSAGE
		(void)dllModule;
		return true;
#else
		try
		{
			const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);

			AppSettings settings;
			std::string errorMessage;
			if (!TryLoadModuleSettings(context, settings, errorMessage, nullptr))
			{
				return true;
			}
			if (!settings.startupMessage.enable || IsTruthyEnvironmentVariable(kStartupMessageAcceptedEnvVar))
			{
				return true;
			}

			std::wstring body = BuildStartupMessageBody(settings.startupMessage);
			if (body.empty())
			{
				return true;
			}

			const std::wstring title = TrimWideCopy(settings.startupMessage.title).empty()
				? std::wstring(L"游玩通知")
				: settings.startupMessage.title;
			const int dialogResult = MessageBoxW(
				nullptr,
				body.c_str(),
				title.c_str(),
				MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST);
			if (dialogResult != IDYES)
			{
				ExitProcess(0);
				return false;
			}

			SetEnvironmentVariableW(kStartupMessageAcceptedEnvVar, L"1");
			return true;
		}
		catch (...)
		{
			return true;
		}
#endif
	}

	bool HookManager::TryLoadStartupSettings(HMODULE dllModule, AppSettings& settings)
	{
		try
		{
			const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);
			std::string errorMessage;
			return TryLoadModuleSettings(context, settings, errorMessage, nullptr);
		}
		catch (...)
		{
			settings = AppSettings{};
			return false;
		}
	}

	void HookManager::TryApplyBinaryPatchesBeforeEntry(HMODULE dllModule)
	{
#if !CIALLOHOOK_FEATURE_BINARY_PATCH
		(void)dllModule;
		return;
#else
		try
		{
			const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);
			AppSettings settings;
			std::string errorMessage;
			if (!TryLoadModuleSettings(context, settings, errorMessage, nullptr))
			{
				InitLogger(context.dllNameNoExt.c_str(), true, false, false);
				std::wstring wideError = Utf8ToWide(errorMessage);
				LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): config load failed, fallback to runtime: %s", wideError.c_str());
				return;
			}

			InitLogger(context.dllNameNoExt.c_str(), settings.debug.enable, settings.debug.logToFile, settings.debug.logToConsole);
			HookModules::TryApplyBinaryPatchesBeforeEntry(settings.binaryPatch);
		}
		catch (const std::exception& err)
		{
			std::wstring wideError = Utf8ToWide(err.what());
			LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): HookManager exception, fallback to runtime: %s", wideError.c_str());
		}
		catch (...)
		{
			LogMessage(LogLevel::Warn, L"BinaryPatch(pre-entry): HookManager unknown exception, fallback to runtime");
		}
#endif
	}


	void HookManager::TryRequestBinaryPatchOnFirstPatchHit(HMODULE dllModule)
	{
#if !CIALLOHOOK_FEATURE_BINARY_PATCH
		(void)dllModule;
		return;
#else
		try
		{
			const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);
			AppSettings settings;
			std::string errorMessage;
			if (!TryLoadModuleSettings(context, settings, errorMessage, nullptr))
			{
				return;
			}
			if (!settings.binaryPatch.enable || !settings.binaryPatch.enableHwbp)
			{
				return;
			}
			HookModules::TryRequestBinaryPatchOnFirstPatchHit(settings.binaryPatch);
		}
		catch (const std::exception& err)
		{
			std::wstring wideError = Utf8ToWide(err.what());
			LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): HookManager exception: %s", wideError.c_str());
		}
		catch (...)
		{
			LogMessage(LogLevel::Warn, L"BinaryPatch(HWBP): HookManager unknown exception");
		}
#endif
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
				AppendUniquePath(sg_localeEmulatorStagedCleanupPaths, path);
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
		const ModuleSettingsContext context = BuildModuleSettingsContext(dllModule);
		const std::wstring& dllNameNoExt = context.dllNameNoExt;
		const bool startupMessageConsentAlreadyGranted = ConsumeTruthyEnvironmentVariable(kStartupMessageAcceptedEnvVar);
		if (!TryAcquireStartupInitializationMutex())
		{
			InitLogger(dllNameNoExt.c_str(), true, false, false);
			LogMessage(LogLevel::Info, L"Startup initialization already in progress, skip duplicate launch");
			ExitProcess(0);
			return;
		}

		struct StartupInitializationMutexReleaser
		{
			~StartupInitializationMutexReleaser()
			{
				ReleaseStartupInitializationMutex();
			}
		} startupInitializationMutexReleaser;

		try
		{
			const std::wstring configSource = ConfigManager::DescribeSource(context.iniPath);

			InitLogger(dllNameNoExt.c_str(), true, false, false);
			LogMessage(LogLevel::Info, L"Initialize start: module=%s", context.modulePath.empty() ? L"(unknown)" : context.modulePath.c_str());
			LogMessage(LogLevel::Info, L"Initialize context: PID=%lu TID=%lu", GetCurrentProcessId(), GetCurrentThreadId());
			LogMessage(LogLevel::Info, L"Config source: %s", configSource.c_str());

			AppSettings settings;
			std::string errorMessage;
			std::string warningMessage;
			if (!TryLoadModuleSettings(context, settings, errorMessage, &warningMessage))
			{
				std::wstring wideError = Utf8ToWide(errorMessage);
				LogMessage(LogLevel::Error, L"Config load failed: %s", wideError.c_str());
					ReleaseStartupWindowGate();
				MessageBoxW(NULL, wideError.c_str(), L"CialloHook", MB_OK | MB_ICONERROR);
				return;
			}

			bool effectiveDebugEnable = settings.debug.enable;
			InitLogger(dllNameNoExt.c_str(), effectiveDebugEnable, settings.debug.logToFile, settings.debug.logToConsole);
			InstallGlobalExceptionHandlers();
			LogMessage(LogLevel::Info, L"Global VEH exception handler installed");
			LogMessage(LogLevel::Info, L"Config loaded: %s", configSource.c_str());
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
			LogMessage(LogLevel::Info, L"Feature flags: Font=%d Text=%d Title=%d ScreenCaptureProtection=%d(%s) FilePatch=%d CodePage=%d AliceSystem3x=%d RioShiina=%d(mode=%d)",
				hasFontHook ? 1 : 0, settings.textReplace.rules.empty() ? 0 : 1, settings.windowTitle.rules.empty() ? 0 : 1,
				settings.screenCaptureProtection.enable ? 1 : 0, settings.screenCaptureProtection.mode.c_str(),
				settings.filePatch.enable ? 1 : 0, settings.codePage.enable ? 1 : 0,
				settings.aliceSystem3x.enable ? 1 : 0, settings.rioShiina.enable ? 1 : 0, settings.rioShiina.mode);
			LogMessage(LogLevel::Info, L"Engine cache flags: MED=%d MAJIRO=%d",
				settings.engineCache.med ? 1 : 0, settings.engineCache.majiro ? 1 : 0);
			LogMessage(LogLevel::Info, L"Engine patch flags: KrkrPatch=%d(count=%u) WafflePatch=%d(GetTextCrash=%d)",
				settings.enginePatches.enableKrkrPatch ? 1 : 0,
				(uint32_t)settings.enginePatches.krkrPatchNames.size(),
				settings.enginePatches.enableWafflePatch ? 1 : 0,
				settings.enginePatches.waffleFixGetTextCrash ? 1 : 0);
#if CIALLOHOOK_FEATURE_ENGINE_CACHE
			RunEngineCacheCleanup(settings.engineCache);
#endif

			std::wstring dllNameLower = ToLowerCopy(dllNameNoExt);
			bool isWinmmProxy = (dllNameLower == L"winmm");
			LogMessage(LogLevel::Info, L"Load mode: %s", isWinmmProxy ? L"proxy(winmm)" : L"dll");

#if CIALLOHOOK_FEATURE_LOCALE_EMULATOR
			if (TryRelaunchWithLocaleEmulator(settings))
			{
				return;
			}
#endif

#if CIALLOHOOK_FEATURE_STARTUP_MESSAGE
			if (!startupMessageConsentAlreadyGranted)
			{
				if (!ShowStartupMessage(settings.startupMessage))
				{
					LogMessage(LogLevel::Info, L"StartupMessage: user declined patch notice, exit process");
					ExitProcess(0);
					return;
				}
			}
			else if (settings.startupMessage.enable)
			{
				LogMessage(LogLevel::Info, L"StartupMessage: prior consent already granted, skip in-process dialog");
			}
#endif
			HookModules::ApplyPostStartupHooks(settings);
			LogMessage(LogLevel::Info, L"All hook modules applied");
		}
		catch (const std::exception& err)
		{
			std::wstring wideError = Utf8ToWide(err.what());
			InitLogger(dllNameNoExt.c_str(), true, false, false);
			LogMessage(LogLevel::Error, L"Initialize exception: %s", wideError.c_str());
				ReleaseStartupWindowGate();
			MessageBoxW(NULL, wideError.c_str(), L"CialloHook", MB_OK | MB_ICONERROR);
		}
	}
}
