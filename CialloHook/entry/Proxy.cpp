#include <Windows.h>
#include "Proxy.h"

static void ProxyOutput(const wchar_t* text)
{
	if (text)
	{
		OutputDebugStringW(text);
	}
}

static void FailProxyInit(const wchar_t* message)
{
	ProxyOutput(message);
	MessageBoxW(nullptr, message, L"CialloHook", MB_ICONERROR);
	ExitProcess(0);
}

static BOOL WINAPI FallbackGetFileVersionInfoByHandle(HANDLE, DWORD, DWORD, LPVOID)
{
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

static BOOL WINAPI FallbackGetFileVersionInfoExA(DWORD, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
}

static BOOL WINAPI FallbackGetFileVersionInfoExW(DWORD, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

static DWORD WINAPI FallbackGetFileVersionInfoSizeExA(DWORD, LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
}

static DWORD WINAPI FallbackGetFileVersionInfoSizeExW(DWORD, LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

void Proxy::Init()
{
	wchar_t realDllPath[MAX_PATH];
	GetSystemDirectoryW(realDllPath, MAX_PATH);
	wcscat_s(realDllPath, L"\\version.dll");
	ProxyOutput(L"[CialloHook] Proxy::Init loading version.dll\r\n");
	HMODULE hDll = LoadLibraryW(realDllPath);
	if (hDll == nullptr)
	{
		FailProxyInit(L"无法加载系统 version.dll");
	}

#define RESOLVE(fn) Original##fn = reinterpret_cast<decltype(Original##fn)>(GetProcAddress(hDll, #fn))
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

	if (!OriginalGetFileVersionInfoByHandle)
	{
		OriginalGetFileVersionInfoByHandle = FallbackGetFileVersionInfoByHandle;
	}
	if (!OriginalGetFileVersionInfoExA)
	{
		OriginalGetFileVersionInfoExA = FallbackGetFileVersionInfoExA;
	}
	if (!OriginalGetFileVersionInfoExW)
	{
		OriginalGetFileVersionInfoExW = FallbackGetFileVersionInfoExW;
	}
	if (!OriginalGetFileVersionInfoSizeExA)
	{
		OriginalGetFileVersionInfoSizeExA = FallbackGetFileVersionInfoSizeExA;
	}
	if (!OriginalGetFileVersionInfoSizeExW)
	{
		OriginalGetFileVersionInfoSizeExW = FallbackGetFileVersionInfoSizeExW;
	}

	if (!OriginalGetFileVersionInfoA ||
		!OriginalGetFileVersionInfoSizeA ||
		!OriginalGetFileVersionInfoSizeW ||
		!OriginalGetFileVersionInfoW ||
		!OriginalVerFindFileA ||
		!OriginalVerFindFileW ||
		!OriginalVerInstallFileA ||
		!OriginalVerInstallFileW ||
		!OriginalVerLanguageNameA ||
		!OriginalVerLanguageNameW ||
		!OriginalVerQueryValueA ||
		!OriginalVerQueryValueW)
	{
		FailProxyInit(L"初始化 version.dll 导出转发失败");
	}

	ProxyOutput(L"[CialloHook] Proxy::Init finished\r\n");
}

extern "C" BOOL WINAPI FakeGetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoA(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI FakeGetFileVersionInfoByHandle(HANDLE hFile, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoByHandle)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoByHandle(hFile, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI FakeGetFileVersionInfoExA(DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoExA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoExA(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" BOOL WINAPI FakeGetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoExW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoExW(dwFlags, lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI FakeGetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeA(lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI FakeGetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeExA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeExA(dwFlags, lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI FakeGetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeExW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeExW(dwFlags, lptstrFilename, lpdwHandle);
}

extern "C" DWORD WINAPI FakeGetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle)
{
	if (!Proxy::OriginalGetFileVersionInfoSizeW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalGetFileVersionInfoSizeW(lptstrFilename, lpdwHandle);
}

extern "C" BOOL WINAPI FakeGetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData)
{
	if (!Proxy::OriginalGetFileVersionInfoW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalGetFileVersionInfoW(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" DWORD WINAPI FakeVerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen)
{
	if (!Proxy::OriginalVerFindFileA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerFindFileA(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

extern "C" DWORD WINAPI FakeVerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen)
{
	if (!Proxy::OriginalVerFindFileW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerFindFileW(uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen);
}

extern "C" DWORD WINAPI FakeVerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen)
{
	if (!Proxy::OriginalVerInstallFileA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerInstallFileA(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

extern "C" DWORD WINAPI FakeVerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen)
{
	if (!Proxy::OriginalVerInstallFileW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerInstallFileW(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen);
}

extern "C" DWORD WINAPI FakeVerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang)
{
	if (!Proxy::OriginalVerLanguageNameA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerLanguageNameA(wLang, szLang, cchLang);
}

extern "C" DWORD WINAPI FakeVerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang)
{
	if (!Proxy::OriginalVerLanguageNameW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return 0;
	}
	return Proxy::OriginalVerLanguageNameW(wLang, szLang, cchLang);
}

extern "C" BOOL WINAPI FakeVerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!Proxy::OriginalVerQueryValueA)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalVerQueryValueA(pBlock, lpSubBlock, lplpBuffer, puLen);
}

extern "C" BOOL WINAPI FakeVerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen)
{
	if (!Proxy::OriginalVerQueryValueW)
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return FALSE;
	}
	return Proxy::OriginalVerQueryValueW(pBlock, lpSubBlock, lplpBuffer, puLen);
}
