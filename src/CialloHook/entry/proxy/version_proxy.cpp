#include <Windows.h>
#include "version_proxy.h"
#include "../../config/build_options.h"

#ifndef CIALLOHOOK_PROTECT_VERSION_PROXY
#define CIALLOHOOK_PROTECT_VERSION_PROXY 1
#endif

#if defined(_MSC_VER) && defined(_WIN32) && CIALLOHOOK_FEATURE_CODECRYPT_PATCH && CIALLOHOOK_PROTECT_VERSION_PROXY
#define CIALLOHOOK_VERSION_PROXY_PROTECTED_BEGIN __pragma(code_seg(push, ".lpksc$m"))
#define CIALLOHOOK_VERSION_PROXY_PROTECTED_END __pragma(code_seg(pop))
#else
#define CIALLOHOOK_VERSION_PROXY_PROTECTED_BEGIN
#define CIALLOHOOK_VERSION_PROXY_PROTECTED_END
#endif

CIALLOHOOK_VERSION_PROXY_PROTECTED_BEGIN

static void VersionProxyOutput(const wchar_t* text)
{
	if (text)
	{
		OutputDebugStringW(text);
	}
}

static INIT_ONCE g_versionInitOnce = INIT_ONCE_STATIC_INIT;
static HMODULE g_realVersion = nullptr;

static BOOL WINAPI FallbackGetFileVersionInfoByHandle(HANDLE, DWORD, DWORD, LPVOID)
{
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

static BOOL WINAPI FallbackGetFileVersionInfoExA(DWORD, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!VersionProxy::OriginalGetFileVersionInfoA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return VersionProxy::OriginalGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
}

static BOOL WINAPI FallbackGetFileVersionInfoExW(DWORD, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!VersionProxy::OriginalGetFileVersionInfoW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return VersionProxy::OriginalGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

static DWORD WINAPI FallbackGetFileVersionInfoSizeExA(DWORD, LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!VersionProxy::OriginalGetFileVersionInfoSizeA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return VersionProxy::OriginalGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
}

static DWORD WINAPI FallbackGetFileVersionInfoSizeExW(DWORD, LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!VersionProxy::OriginalGetFileVersionInfoSizeW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return VersionProxy::OriginalGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

static BOOL CALLBACK InitVersionProxy(PINIT_ONCE, PVOID, PVOID*)
{
	wchar_t realDllPath[MAX_PATH] = {};
	const UINT systemDirectoryLength = GetSystemDirectoryW(realDllPath, _countof(realDllPath));
	if (systemDirectoryLength == 0 || systemDirectoryLength >= _countof(realDllPath) ||
		wcscat_s(realDllPath, L"\\version.dll") != 0)
	{
		VersionProxyOutput(L"[Ciallo Runtime] invalid system version.dll path\r\n");
		return FALSE;
	}

	VersionProxyOutput(L"[Ciallo Runtime] component init begin\r\n");
	if (g_realVersion == nullptr)
	{
		g_realVersion = LoadLibraryW(realDllPath);
	}
	if (g_realVersion == nullptr)
	{
		VersionProxyOutput(L"[Ciallo Runtime] failed to load system version.dll\r\n");
		return FALSE;
	}

#define RESOLVE(fn) VersionProxy::Original##fn = reinterpret_cast<decltype(VersionProxy::Original##fn)>(GetProcAddress(g_realVersion, #fn))
	RESOLVE(GetFileVersionInfoA);
	RESOLVE(GetFileVersionInfoByHandle);
	RESOLVE(GetFileVersionInfoExA);
	RESOLVE(GetFileVersionInfoExW);
	RESOLVE(GetFileVersionInfoSizeA);
	RESOLVE(GetFileVersionInfoSizeExA);
	RESOLVE(GetFileVersionInfoSizeExW);
	RESOLVE(GetFileVersionInfoSizeW);
	RESOLVE(GetFileVersionInfoW);
	RESOLVE(VerFindFileA);
	RESOLVE(VerFindFileW);
	RESOLVE(VerInstallFileA);
	RESOLVE(VerInstallFileW);
	RESOLVE(VerLanguageNameA);
	RESOLVE(VerLanguageNameW);
	RESOLVE(VerQueryValueA);
	RESOLVE(VerQueryValueW);
#undef RESOLVE

	// GetFileVersionInfoByHandle is absent on Windows 7. The Ex APIs are
	// available on Vista+, but keep their legacy fallbacks for older variants.
	if (!VersionProxy::OriginalGetFileVersionInfoByHandle)
	{
		VersionProxy::OriginalGetFileVersionInfoByHandle = FallbackGetFileVersionInfoByHandle;
	}
	if (!VersionProxy::OriginalGetFileVersionInfoExA)
	{
		VersionProxy::OriginalGetFileVersionInfoExA = FallbackGetFileVersionInfoExA;
	}
	if (!VersionProxy::OriginalGetFileVersionInfoExW)
	{
		VersionProxy::OriginalGetFileVersionInfoExW = FallbackGetFileVersionInfoExW;
	}
	if (!VersionProxy::OriginalGetFileVersionInfoSizeExA)
	{
		VersionProxy::OriginalGetFileVersionInfoSizeExA = FallbackGetFileVersionInfoSizeExA;
	}
	if (!VersionProxy::OriginalGetFileVersionInfoSizeExW)
	{
		VersionProxy::OriginalGetFileVersionInfoSizeExW = FallbackGetFileVersionInfoSizeExW;
	}

	if (!VersionProxy::OriginalGetFileVersionInfoA ||
		!VersionProxy::OriginalGetFileVersionInfoSizeA ||
		!VersionProxy::OriginalGetFileVersionInfoSizeW ||
		!VersionProxy::OriginalGetFileVersionInfoW ||
		!VersionProxy::OriginalVerFindFileA ||
		!VersionProxy::OriginalVerFindFileW ||
		!VersionProxy::OriginalVerInstallFileA ||
		!VersionProxy::OriginalVerInstallFileW ||
		!VersionProxy::OriginalVerLanguageNameA ||
		!VersionProxy::OriginalVerLanguageNameW ||
		!VersionProxy::OriginalVerQueryValueA ||
		!VersionProxy::OriginalVerQueryValueW)
	{
		VersionProxyOutput(L"[Ciallo Runtime] required version.dll export is missing\r\n");
		return FALSE;
	}

	VersionProxyOutput(L"[Ciallo Runtime] component init success\r\n");
	return TRUE;
}

bool VersionProxy::Init()
{
	return InitOnceExecuteOnce(&g_versionInitOnce, InitVersionProxy, nullptr, nullptr) != FALSE;
}

#define REQUIRE_VERSION_EXPORT(fn, failureValue) \
	if (!VersionProxy::Init() || !VersionProxy::Original##fn) \
	{ \
		SetLastError(ERROR_PROC_NOT_FOUND); \
		return failureValue; \
	}

extern "C" BOOL WINAPI CialloVersion_GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoA, FALSE);
	return VersionProxy::OriginalGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI CialloVersion_GetFileVersionInfoByHandle(HANDLE hFile, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoByHandle, FALSE);
	return VersionProxy::OriginalGetFileVersionInfoByHandle(hFile, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI CialloVersion_GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoExA, FALSE);
	return VersionProxy::OriginalGetFileVersionInfoExA(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI CialloVersion_GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoExW, FALSE);
	return VersionProxy::OriginalGetFileVersionInfoExW(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI CialloVersion_GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoSizeA, 0);
	return VersionProxy::OriginalGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI CialloVersion_GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoSizeExA, 0);
	return VersionProxy::OriginalGetFileVersionInfoSizeExA(dwFlags, lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI CialloVersion_GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoSizeExW, 0);
	return VersionProxy::OriginalGetFileVersionInfoSizeExW(dwFlags, lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI CialloVersion_GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoSizeW, 0);
	return VersionProxy::OriginalGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

extern "C" BOOL WINAPI CialloVersion_GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	REQUIRE_VERSION_EXPORT(GetFileVersionInfoW, FALSE);
	return VersionProxy::OriginalGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI CialloVersion_VerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen)
{
	REQUIRE_VERSION_EXPORT(VerFindFileA, 0);
	return VersionProxy::OriginalVerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

extern "C" DWORD WINAPI CialloVersion_VerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen)
{
	REQUIRE_VERSION_EXPORT(VerFindFileW, 0);
	return VersionProxy::OriginalVerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

extern "C" DWORD WINAPI CialloVersion_VerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen)
{
	REQUIRE_VERSION_EXPORT(VerInstallFileA, 0);
	return VersionProxy::OriginalVerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

extern "C" DWORD WINAPI CialloVersion_VerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen)
{
	REQUIRE_VERSION_EXPORT(VerInstallFileW, 0);
	return VersionProxy::OriginalVerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

extern "C" DWORD WINAPI CialloVersion_VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang)
{
	REQUIRE_VERSION_EXPORT(VerLanguageNameA, 0);
	return VersionProxy::OriginalVerLanguageNameA(wLang, szLang, cchLang);
}

extern "C" DWORD WINAPI CialloVersion_VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang)
{
	REQUIRE_VERSION_EXPORT(VerLanguageNameW, 0);
	return VersionProxy::OriginalVerLanguageNameW(wLang, szLang, cchLang);
}

extern "C" BOOL WINAPI CialloVersion_VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	REQUIRE_VERSION_EXPORT(VerQueryValueA, FALSE);
	return VersionProxy::OriginalVerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen);
}

extern "C" BOOL WINAPI CialloVersion_VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	REQUIRE_VERSION_EXPORT(VerQueryValueW, FALSE);
	return VersionProxy::OriginalVerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
}

#undef REQUIRE_VERSION_EXPORT
