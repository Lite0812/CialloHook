#pragma once
#include <Windows.h>
#pragma comment(lib, "version.lib")

using PFN_GetFileVersionInfoByHandle = BOOL(WINAPI*)(HANDLE, DWORD, DWORD, LPVOID);

class Proxy
{
public:
	static void Init();

	static inline decltype(GetFileVersionInfoA)*		OriginalGetFileVersionInfoA{};
	static inline PFN_GetFileVersionInfoByHandle		OriginalGetFileVersionInfoByHandle{};
	static inline decltype(GetFileVersionInfoExA)*		OriginalGetFileVersionInfoExA{};
	static inline decltype(GetFileVersionInfoExW)*		OriginalGetFileVersionInfoExW{};
	static inline decltype(GetFileVersionInfoSizeA)*	OriginalGetFileVersionInfoSizeA{};
	static inline decltype(GetFileVersionInfoSizeExA)*	OriginalGetFileVersionInfoSizeExA{};
	static inline decltype(GetFileVersionInfoSizeExW)*	OriginalGetFileVersionInfoSizeExW{};
	static inline decltype(GetFileVersionInfoSizeW)*	OriginalGetFileVersionInfoSizeW{};
	static inline decltype(GetFileVersionInfoW)*		OriginalGetFileVersionInfoW{};
	static inline decltype(VerFindFileA)*				OriginalVerFindFileA{};
	static inline decltype(VerFindFileW)*				OriginalVerFindFileW{};
	static inline decltype(VerInstallFileA)*			OriginalVerInstallFileA{};
	static inline decltype(VerInstallFileW)*			OriginalVerInstallFileW{};
	static inline decltype(VerLanguageNameA)*			OriginalVerLanguageNameA{};
	static inline decltype(VerLanguageNameW)*			OriginalVerLanguageNameW{};
	static inline decltype(VerQueryValueA)*				OriginalVerQueryValueA{};
	static inline decltype(VerQueryValueW)*				OriginalVerQueryValueW{};
};

// 导出的代理函数声明
extern "C" __declspec(dllexport) BOOL WINAPI FakeGetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern "C" __declspec(dllexport) BOOL WINAPI FakeGetFileVersionInfoByHandle(HANDLE hFile, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern "C" __declspec(dllexport) BOOL WINAPI FakeGetFileVersionInfoExA(DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern "C" __declspec(dllexport) BOOL WINAPI FakeGetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern "C" __declspec(dllexport) DWORD WINAPI FakeGetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle);
extern "C" __declspec(dllexport) DWORD WINAPI FakeGetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle);
extern "C" __declspec(dllexport) DWORD WINAPI FakeGetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
extern "C" __declspec(dllexport) DWORD WINAPI FakeGetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle);
extern "C" __declspec(dllexport) BOOL WINAPI FakeGetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerFindFileA(DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerFindFileW(DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerInstallFileA(DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerInstallFileW(DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang);
extern "C" __declspec(dllexport) DWORD WINAPI FakeVerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang);
extern "C" __declspec(dllexport) BOOL WINAPI FakeVerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
extern "C" __declspec(dllexport) BOOL WINAPI FakeVerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen);
