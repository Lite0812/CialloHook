#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdarg>
#include <string>

#include "../../RuntimeCore/hook/Hook_API.h"
#include "Proxy.h"
#include "../core/hook_manager.h"
#include "../hooks/hook_modules.h"

struct HookInitContext
{
	HMODULE module;
	DWORD delayMs;
	bool waitForGuiReady;
};

static volatile LONG sg_isProcessDetaching = 0;
static volatile LONG sg_inTopLevelExceptionFilter = 0;
static LPTOP_LEVEL_EXCEPTION_FILTER sg_previousTopLevelExceptionFilter = nullptr;

extern "C" __declspec(dllexport) VOID CALLBACK DetourFinishHelperProcess(HWND, HINSTANCE, LPSTR, int)
{
}

static std::string WideToUtf8(const std::wstring& text)
{
	if (text.empty())
	{
		return "";
	}

	int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
	{
		return "";
	}

	std::string result(len - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, &result[0], len, nullptr, nullptr);
	return result;
}

static std::wstring GetBootstrapLogPath()
{
	wchar_t exePath[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
	{
		return L"CialloHook_bootstrap.log";
	}

	std::wstring path = exePath;
	size_t pos = path.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
	{
		path = path.substr(0, pos + 1);
	}
	else
	{
		path.clear();
	}
	path += L"CialloHook_bootstrap.log";
	return path;
}

static bool IsBootstrapFileLogEnabled()
{
	wchar_t value[16] = {};
	DWORD len = GetEnvironmentVariableW(L"CIALLOHOOK_BOOTSTRAP_LOG", value, _countof(value));
	if (len == 0 || len >= _countof(value))
	{
		return false;
	}

	return lstrcmpiW(value, L"1") == 0
		|| lstrcmpiW(value, L"true") == 0
		|| lstrcmpiW(value, L"on") == 0;
}

static void BootstrapLog(const wchar_t* format, ...)
{
	wchar_t message[1536] = {};
	va_list args;
	va_start(args, format);
	_vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
	va_end(args);

	SYSTEMTIME st = {};
	GetLocalTime(&st);

	wchar_t line[1800] = {};
	swprintf_s(line, L"[%02d:%02d:%02d.%03d][P%lu:T%lu][BOOT] %s\r\n",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		GetCurrentProcessId(), GetCurrentThreadId(), message);
	OutputDebugStringW(line);

	if (!IsBootstrapFileLogEnabled())
	{
		return;
	}

	std::wstring logPath = GetBootstrapLogPath();
	FILE* fp = nullptr;
	if (_wfopen_s(&fp, logPath.c_str(), L"ab+") == 0 && fp)
	{
		fseek(fp, 0, SEEK_END);
		long fileSize = ftell(fp);
		if (fileSize <= 0)
		{
			unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
			fwrite(bom, 1, sizeof(bom), fp);
		}
		std::string utf8 = WideToUtf8(line);
		if (!utf8.empty())
		{
			fwrite(utf8.data(), 1, utf8.size(), fp);
		}
		fflush(fp);
		fclose(fp);
	}
}

static std::wstring BuildCrashDumpPath()
{
	wchar_t exePath[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	std::wstring path = exePath;
	size_t pos = path.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
	{
		path = path.substr(0, pos + 1);
	}
	else
	{
		path.clear();
	}

	wchar_t fileName[128] = {};
	swprintf_s(fileName, L"CialloHook_crash_%lu_%lu.dmp", GetCurrentProcessId(), GetCurrentThreadId());
	path += fileName;
	return path;
}

static bool IsProcessShuttingDown()
{
	if (InterlockedCompareExchange(&sg_isProcessDetaching, 0, 0) != 0)
	{
		return true;
	}

	using RtlDllShutdownInProgressFn = BOOLEAN(WINAPI*)();
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (!ntdll)
	{
		return false;
	}
	RtlDllShutdownInProgressFn rtlDllShutdownInProgress =
		reinterpret_cast<RtlDllShutdownInProgressFn>(GetProcAddress(ntdll, "RtlDllShutdownInProgress"));
	if (!rtlDllShutdownInProgress)
	{
		return false;
	}
	return rtlDllShutdownInProgress() ? true : false;
}

static bool ShouldAbortHookInitialization(const wchar_t* stage)
{
	if (!IsProcessShuttingDown())
	{
		return false;
	}

	BootstrapLog(L"%s: abort initialization because process is shutting down", stage);
	return true;
}

static bool WriteCrashDump(EXCEPTION_POINTERS* exceptionInfo, const wchar_t* stage)
{
	if (IsProcessShuttingDown())
	{
		BootstrapLog(L"%s: skip dump because process is shutting down", stage);
		return false;
	}

	HMODULE dbgHelp = LoadLibraryW(L"dbghelp.dll");
	if (!dbgHelp)
	{
		BootstrapLog(L"%s: failed to load dbghelp.dll (GetLastError=%lu)", stage, GetLastError());
		return false;
	}

	using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
		PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
	MiniDumpWriteDumpFn miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
		GetProcAddress(dbgHelp, "MiniDumpWriteDump"));
	if (!miniDumpWriteDump)
	{
		BootstrapLog(L"%s: MiniDumpWriteDump not found", stage);
		FreeLibrary(dbgHelp);
		return false;
	}

	std::wstring dumpPath = BuildCrashDumpPath();
	HANDLE dumpFile = CreateFileW(
		dumpPath.c_str(),
		GENERIC_WRITE,
		FILE_SHARE_READ,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (dumpFile == INVALID_HANDLE_VALUE)
	{
		BootstrapLog(L"%s: failed to create dump file %s (GetLastError=%lu)", stage, dumpPath.c_str(), GetLastError());
		FreeLibrary(dbgHelp);
		return false;
	}

	MINIDUMP_EXCEPTION_INFORMATION mei = {};
	mei.ThreadId = GetCurrentThreadId();
	mei.ExceptionPointers = exceptionInfo;
	mei.ClientPointers = FALSE;

	BOOL ok = miniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		dumpFile,
		MiniDumpNormal,
		exceptionInfo ? &mei : nullptr,
		nullptr,
		nullptr);
	CloseHandle(dumpFile);
	FreeLibrary(dbgHelp);

	if (!ok)
	{
		DeleteFileW(dumpPath.c_str());
		BootstrapLog(L"%s: MiniDumpWriteDump failed (GetLastError=%lu)", stage, GetLastError());
		return false;
	}

	BootstrapLog(L"%s: crash dump created: %s", stage, dumpPath.c_str());
	return true;
}

static LONG HandleSehException(EXCEPTION_POINTERS* exceptionInfo, const wchar_t* stage)
{
	DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord
		? exceptionInfo->ExceptionRecord->ExceptionCode
		: 0;
	void* address = exceptionInfo && exceptionInfo->ExceptionRecord
		? exceptionInfo->ExceptionRecord->ExceptionAddress
		: nullptr;
	BootstrapLog(L"%s: SEH exception code=0x%08lX address=%p", stage, code, address);
	WriteCrashDump(exceptionInfo, stage);
	return EXCEPTION_EXECUTE_HANDLER;
}

static LONG WINAPI TopLevelExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
	if (InterlockedCompareExchange(&sg_inTopLevelExceptionFilter, 1, 0) != 0)
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}
	BootstrapLog(L"TopLevelExceptionFilter triggered");
	Rut::HookX::SetHookRuntimeShuttingDown(true);
	WriteCrashDump(exceptionInfo, L"TopLevelExceptionFilter");
	LONG previousResult = EXCEPTION_CONTINUE_SEARCH;
	if (sg_previousTopLevelExceptionFilter && sg_previousTopLevelExceptionFilter != TopLevelExceptionFilter)
	{
		previousResult = sg_previousTopLevelExceptionFilter(exceptionInfo);
	}
	InterlockedExchange(&sg_inTopLevelExceptionFilter, 0);
	return previousResult;
}

static bool IsWinmmProxyModule(HMODULE module)
{
	wchar_t modulePath[MAX_PATH] = {};
	if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0)
	{
		return false;
	}

	const wchar_t* fileName = modulePath;
	for (const wchar_t* p = modulePath; *p != L'\0'; ++p)
	{
		if (*p == L'\\' || *p == L'/')
		{
			fileName = p + 1;
		}
	}

	return lstrcmpiW(fileName, L"winmm.dll") == 0;
}

static void RunHookInitialization(HookInitContext* initContext)
{
	if (ShouldAbortHookInitialization(L"HookInitThread: pre-start"))
	{
		return;
	}

	if (initContext->waitForGuiReady)
	{
		DWORD waitedMs = 0;
		const DWORD maxWaitMs = 10000;
		const DWORD stepMs = 100;
		while (waitedMs < maxWaitMs)
		{
			HMODULE user32 = GetModuleHandleW(L"user32.dll");
			HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
			if (user32 != nullptr && gdi32 != nullptr)
			{
				break;
			}
			if (ShouldAbortHookInitialization(L"HookInitThread: waiting for GUI"))
			{
				return;
			}
			Sleep(stepMs);
			waitedMs += stepMs;
		}
		BootstrapLog(L"HookInitThread: GUI wait finished after %lu ms", waitedMs);
	}
	if (initContext->delayMs > 0)
	{
		DWORD waitedMs = 0;
		const DWORD stepMs = 100;
		while (waitedMs < initContext->delayMs)
		{
			if (ShouldAbortHookInitialization(L"HookInitThread: delayed startup"))
			{
				return;
			}

			DWORD remainingMs = initContext->delayMs - waitedMs;
			DWORD sleepMs = remainingMs < stepMs ? remainingMs : stepMs;
			Sleep(sleepMs);
			waitedMs += sleepMs;
		}
	}

	if (ShouldAbortHookInitialization(L"HookInitThread: before initialize"))
	{
		return;
	}

	CialloHook::HookManager::Initialize(initContext->module);
	BootstrapLog(L"HookInitThread: HookManager::Initialize completed");
}

static DWORD WINAPI HookInitThread(LPVOID context)
{
	HookInitContext* initContext = reinterpret_cast<HookInitContext*>(context);
	HMODULE pinnedModule = nullptr;
	if (initContext == nullptr)
	{
		BootstrapLog(L"HookInitThread: context is null");
		return 0;
	}

	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<LPCWSTR>(initContext->module),
		&pinnedModule))
	{
		BootstrapLog(L"HookInitThread: failed to pin module %p (GetLastError=%lu), abort initialization",
			initContext->module, GetLastError());
		delete initContext;
		return 0;
	}

	BootstrapLog(L"HookInitThread start: module=%p delayMs=%lu", initContext->module, initContext->delayMs);

	__try
	{
		RunHookInitialization(initContext);
	}
	__except (HandleSehException(GetExceptionInformation(), L"HookManager::Initialize"))
	{
		BootstrapLog(L"HookInitThread: HookManager::Initialize aborted by SEH");
	}

	delete initContext;
	BootstrapLog(L"HookInitThread end");
	BootstrapLog(L"HookInitThread: releasing module pin %p", pinnedModule);
	FreeLibraryAndExitThread(pinnedModule, 0);
	return 0;
}

static void StartHookInitialization(HMODULE hModule)
{
	HookInitContext* initContext = new HookInitContext{};
	initContext->module = hModule;
	initContext->waitForGuiReady = false;
	initContext->delayMs = 0;

	HANDLE threadHandle = CreateThread(nullptr, 0, HookInitThread, initContext, 0, nullptr);
	if (threadHandle)
	{
		BootstrapLog(L"DllMain: HookInitThread created");
		CloseHandle(threadHandle);
		return;
	}

	BootstrapLog(L"DllMain: CreateThread failed (GetLastError=%lu), skip direct initialize to avoid loader-lock risk", GetLastError());
	delete initContext;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		Rut::HookX::SetHookRuntimeShuttingDown(false);
		CialloHook::HookManager::RegisterLocaleEmulatorStagedFilesFromEnvironment();
		{
			wchar_t modulePath[MAX_PATH] = {};
			GetModuleFileNameW(hModule, modulePath, MAX_PATH);
			BootstrapLog(L"DLL_PROCESS_ATTACH: module=%s", modulePath[0] ? modulePath : L"(unknown)");
		}
		{
			const bool isWinmmProxy = IsWinmmProxyModule(hModule);
			BootstrapLog(L"DLL_PROCESS_ATTACH: isWinmmProxy=%d", isWinmmProxy ? 1 : 0);
			if (isWinmmProxy)
			{
				BootstrapLog(L"DllMain: winmm mode, skip Proxy::Init and use immediate runtime initialization");
			}
			else
			{
				BootstrapLog(L"DllMain: Proxy::Init begin");
				Proxy::Init();
				BootstrapLog(L"DllMain: Proxy::Init success");
			}
		}
		sg_previousTopLevelExceptionFilter = SetUnhandledExceptionFilter(TopLevelExceptionFilter);
		if (CialloHook::HookManager::TryEarlyLocaleEmulatorRelaunch(hModule))
		{
			BootstrapLog(L"DllMain: locale emulator relaunch handled during early init");
			return TRUE;
		}
		if (!CialloHook::HookManager::TryHandleConsentInDllMain(hModule))
		{
			BootstrapLog(L"DllMain: startup consent declined during attach");
			return FALSE;
		}
		StartHookInitialization(hModule);
		break;
	case DLL_PROCESS_DETACH:
		InterlockedExchange(&sg_isProcessDetaching, 1);
		Rut::HookX::SetHookRuntimeShuttingDown(true);
		Rut::HookX::UnhookFileAPIs();
		CialloHook::HookManager::CleanupLocaleEmulatorStagedFilesOnShutdown();
		CialloHook::HookModules::CleanupLoadedFontTempFiles();
		Rut::HookX::CleanupCustomPakCacheOnShutdown();
		SetUnhandledExceptionFilter(sg_previousTopLevelExceptionFilter);
		BootstrapLog(L"DLL_PROCESS_DETACH");
		Rut::HookX::ShutdownLogger();
		break;
	default:
		break;
	}
	return TRUE;
}
