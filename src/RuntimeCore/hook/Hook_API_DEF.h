#pragma once
#include <Windows.h>
#include <commdlg.h>
#include <shlobj_core.h>


typedef INT(WINAPI* pMessageBoxA)(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

typedef BOOL(WINAPI* pChooseFontA)(LPCHOOSEFONTA lpcf);

typedef BOOL(WINAPI* pChooseFontW)(LPCHOOSEFONTW lpcf);

typedef HFONT(WINAPI* pCreateFontA)(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);

typedef HFONT(WINAPI* pCreateFontIndirectA)(CONST LOGFONTA* lplf);

typedef HFONT(WINAPI* pCreateFontW)(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName);

typedef HFONT(WINAPI* pCreateFontIndirectW)(CONST LOGFONTW* lplf);

typedef int (CALLBACK* pFONTENUMPROCA)(const LOGFONTA* lpelfe, const TEXTMETRICA* lpntme, DWORD FontType, LPARAM lParam);

typedef int (CALLBACK* pFONTENUMPROCW)(const LOGFONTW* lpelfe, const TEXTMETRICW* lpntme, DWORD FontType, LPARAM lParam);

typedef int (WINAPI* pEnumFontFamiliesExA)(HDC hdc, LPLOGFONTA lpLogfont, pFONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags);

typedef int (WINAPI* pEnumFontFamiliesExW)(HDC hdc, LPLOGFONTW lpLogfont, pFONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags);

typedef HGDIOBJ(WINAPI* pSelectObject)(HDC hdc, HGDIOBJ h);

typedef int(WINAPI* pGetObjectA)(HANDLE h, int c, LPVOID pv);

typedef int(WINAPI* pGetObjectW)(HANDLE h, int c, LPVOID pv);

typedef int(WINAPI* pGetTextFaceA)(HDC hdc, int c, LPSTR lpName);

typedef int(WINAPI* pGetTextFaceW)(HDC hdc, int c, LPWSTR lpName);

typedef BOOL(WINAPI* pGetTextMetricsA)(HDC hdc, LPTEXTMETRICA lptm);

typedef BOOL(WINAPI* pGetTextMetricsW)(HDC hdc, LPTEXTMETRICW lptm);

typedef HFONT(WINAPI* pCreateFontIndirectExA)(const ENUMLOGFONTEXDVA* penumlfex);

typedef HFONT(WINAPI* pCreateFontIndirectExW)(const ENUMLOGFONTEXDVW* penumlfex);

typedef BOOL(WINAPI* pGetCharABCWidthsA)(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC);

typedef BOOL(WINAPI* pGetCharABCWidthsW)(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC);

typedef BOOL(WINAPI* pGetCharABCWidthsFloatA)(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC);

typedef BOOL(WINAPI* pGetCharABCWidthsFloatW)(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC);

typedef BOOL(WINAPI* pGetCharWidthA)(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer);

typedef BOOL(WINAPI* pGetCharWidthW)(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer);

typedef BOOL(WINAPI* pGetCharWidth32A)(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer);

typedef BOOL(WINAPI* pGetCharWidth32W)(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer);

typedef DWORD(WINAPI* pGetKerningPairsA)(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs);

typedef DWORD(WINAPI* pGetKerningPairsW)(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs);

typedef UINT(WINAPI* pGetOutlineTextMetricsA)(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA lpotm);

typedef UINT(WINAPI* pGetOutlineTextMetricsW)(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW lpotm);

typedef int (WINAPI* pAddFontResourceA)(LPCSTR lpFileName);

typedef int (WINAPI* pAddFontResourceW)(LPCWSTR lpFileName);

typedef int (WINAPI* pAddFontResourceExA)(LPCSTR name, DWORD fl, PVOID pdv);

typedef HANDLE(WINAPI* pAddFontMemResourceEx)(PVOID pbFont, DWORD cbFont, PVOID pdv, DWORD* pcFonts);

typedef BOOL(WINAPI* pRemoveFontResourceA)(LPCSTR lpFileName);

typedef BOOL(WINAPI* pRemoveFontResourceW)(LPCWSTR lpFileName);

typedef BOOL(WINAPI* pRemoveFontResourceExA)(LPCSTR name, DWORD fl, PVOID pdv);

typedef BOOL(WINAPI* pRemoveFontMemResourceEx)(HANDLE h);

typedef int (WINAPI* pEnumFontsA)(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam);

typedef int (WINAPI* pEnumFontsW)(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam);

typedef int (WINAPI* pEnumFontFamiliesA)(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam);

typedef int (WINAPI* pEnumFontFamiliesW)(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam);

typedef BOOL(WINAPI* pGetCharWidthFloatA)(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer);

typedef BOOL(WINAPI* pGetCharWidthFloatW)(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer);

typedef BOOL(WINAPI* pGetCharWidthI)(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT piWidths);

typedef BOOL(WINAPI* pGetCharABCWidthsI)(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPABC lpabc);

typedef BOOL(WINAPI* pGetTextExtentPointI)(HDC hdc, LPWORD pgiIn, int cgi, LPSIZE pSize);

typedef BOOL(WINAPI* pGetTextExtentExPointI)(HDC hdc, LPWORD lpwszString, int cwchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize);

typedef DWORD(WINAPI* pGetFontData)(HDC hdc, DWORD dwTable, DWORD dwOffset, PVOID pvBuffer, DWORD cjBuffer);

typedef DWORD(WINAPI* pGetFontLanguageInfo)(HDC hdc);

typedef DWORD(WINAPI* pGetFontUnicodeRanges)(HDC hdc, LPGLYPHSET lpgs);

typedef DWORD(WINAPI* pGetGlyphOutlineA)(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2);

typedef DWORD(WINAPI* pGetGlyphOutlineW)(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2);

typedef HANDLE(WINAPI* pCreateFileA)(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR lpLibFileName);

typedef HMODULE(WINAPI* pLoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

typedef HMODULE(WINAPI* pLoadLibraryW)(LPCWSTR lpLibFileName);

typedef HMODULE(WINAPI* pLoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

typedef HRESULT(WINAPI* pDWriteCreateFactory)(UINT factoryType, REFIID iid, IUnknown** factory);

typedef HRESULT(__stdcall* pDWriteFactoryCreateTextFormat)(void* factory,
	LPCWSTR fontFamilyName,
	void* fontCollection,
	int fontWeight,
	int fontStyle,
	int fontStretch,
	float fontSize,
	LPCWSTR localeName,
	void** textFormat);

typedef int (WINAPI* pGdipCreateFontFamilyFromName)(LPCWSTR name, void* fontCollection, void** fontFamily);

typedef int (WINAPI* pGdipCreateFontFromLogfontW)(HDC hdc, const LOGFONTW* logfont, void** font);

typedef int (WINAPI* pGdipCreateFontFromLogfontA)(HDC hdc, const LOGFONTA* logfont, void** font);

typedef int (WINAPI* pGdipCreateFontFromHFONT)(HDC hdc, HFONT hfont, void** font);

typedef int (WINAPI* pGdipCreateFontFromDC)(HDC hdc, void** font);

typedef int (WINAPI* pGdipCreateFont)(const void* fontFamily, float emSize, int style, int unit, void** font);

typedef int (WINAPI* pGdipDrawString)(void* graphics, const WCHAR* string, int length, const void* font, const void* layoutRect, const void* stringFormat, const void* brush);

typedef int (WINAPI* pGdipDrawDriverString)(void* graphics, const UINT16* text, int length, const void* font, const void* brush, const void* positions, int flags, const void* matrix);

typedef int (WINAPI* pGdipMeasureString)(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, void* boundingBox, INT* codepointsFitted, INT* linesFilled);

typedef int (WINAPI* pGdipMeasureCharacterRanges)(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, INT regionCount, void** regions);

typedef int (WINAPI* pGdipMeasureDriverString)(void* graphics, const UINT16* text, INT length, const void* font, const void* positions, INT flags, const void* matrix, void* boundingBox);

typedef HRESULT(WINAPI* pSHGetFolderPathA)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);

// 文本输出API类型定义
typedef BOOL(WINAPI* pTextOutA)(HDC hdc, int x, int y, LPCSTR lpString, int c);

typedef BOOL(WINAPI* pTextOutW)(HDC hdc, int x, int y, LPCWSTR lpString, int c);

typedef BOOL(WINAPI* pExtTextOutA)(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx);

typedef BOOL(WINAPI* pExtTextOutW)(HDC hdc, int x, int y, UINT options, CONST RECT* lprect, LPCWSTR lpString, UINT c, CONST INT* lpDx);

typedef int(WINAPI* pDrawTextA)(HDC hdc, LPCSTR lpchText, int cchText, LPRECT lprc, UINT format);

typedef int(WINAPI* pDrawTextW)(HDC hdc, LPCWSTR lpchText, int cchText, LPRECT lprc, UINT format);

typedef int(WINAPI* pDrawTextExA)(HDC hdc, LPSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp);

typedef int(WINAPI* pDrawTextExW)(HDC hdc, LPWSTR lpchText, int cchText, LPRECT lprc, UINT format, LPDRAWTEXTPARAMS lpdtp);

typedef BOOL(WINAPI* pPolyTextOutA)(HDC hdc, const POLYTEXTA* ppt, int nStrings);

typedef BOOL(WINAPI* pPolyTextOutW)(HDC hdc, const POLYTEXTW* ppt, int nStrings);

typedef LONG(WINAPI* pTabbedTextOutA)(HDC hdc, int x, int y, LPCSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions, int nTabOrigin);

typedef LONG(WINAPI* pTabbedTextOutW)(HDC hdc, int x, int y, LPCWSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions, int nTabOrigin);

typedef DWORD(WINAPI* pGetTabbedTextExtentA)(HDC hdc, LPCSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions);

typedef DWORD(WINAPI* pGetTabbedTextExtentW)(HDC hdc, LPCWSTR lpString, int chCount, int nTabPositions, const INT* lpnTabStopPositions);

typedef BOOL(WINAPI* pGetTextExtentPoint32A)(HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);

typedef BOOL(WINAPI* pGetTextExtentPoint32W)(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl);

typedef BOOL(WINAPI* pGetTextExtentExPointA)(HDC hdc, LPCSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize);

typedef BOOL(WINAPI* pGetTextExtentExPointW)(HDC hdc, LPCWSTR lpszStr, int cchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize);

typedef BOOL(WINAPI* pGetTextExtentPointA)(HDC hdc, LPCSTR lpString, int c, LPSIZE lpsz);

typedef BOOL(WINAPI* pGetTextExtentPointW)(HDC hdc, LPCWSTR lpString, int c, LPSIZE lpsz);

typedef DWORD(WINAPI* pGetCharacterPlacementA)(HDC hdc, LPCSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSA lpResults, DWORD dwFlags);

typedef DWORD(WINAPI* pGetCharacterPlacementW)(HDC hdc, LPCWSTR lpString, int nCount, int nMaxExtent, LPGCP_RESULTSW lpResults, DWORD dwFlags);

typedef DWORD(WINAPI* pGetGlyphIndicesA)(HDC hdc, LPCSTR lpstr, int c, LPWORD pgi, DWORD fl);

typedef DWORD(WINAPI* pGetGlyphIndicesW)(HDC hdc, LPCWSTR lpstr, int c, LPWORD pgi, DWORD fl);

// 窗口标题相关API类型定义
typedef HWND(WINAPI* pCreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

typedef HWND(WINAPI* pCreateWindowExW)(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

typedef BOOL(WINAPI* pSetWindowTextA)(HWND hWnd, LPCSTR lpString);

typedef BOOL(WINAPI* pSetWindowTextW)(HWND hWnd, LPCWSTR lpString);

typedef BOOL(WINAPI* pShowWindow)(HWND hWnd, int nCmdShow);

typedef BOOL(WINAPI* pShowWindowAsync)(HWND hWnd, int nCmdShow);

typedef BOOL(WINAPI* pSetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);

// 文件热补丁相关API类型定义
typedef HANDLE(WINAPI* pCreateFileA_File)(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

typedef HANDLE(WINAPI* pCreateFileW_File)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

typedef HFILE(WINAPI* pOpenFile)(LPCSTR lpFileName, LPOFSTRUCT lpReOpenBuff, UINT uStyle);

typedef BOOL(WINAPI* pGetFileAttributesExA_File)(LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);

typedef BOOL(WINAPI* pGetFileAttributesExW_File)(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);

typedef BOOL(WINAPI* pGetFileInformationByHandle_File)(HANDLE hFile, LPBY_HANDLE_FILE_INFORMATION lpFileInformation);

typedef DWORD(WINAPI* pGetFileType_File)(HANDLE hFile);

typedef DWORD(WINAPI* pGetFileAttributesA_File)(LPCSTR lpFileName);

typedef DWORD(WINAPI* pGetFileAttributesW_File)(LPCWSTR lpFileName);

typedef BOOL(WINAPI* pReadFile_File)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);

typedef DWORD(WINAPI* pSetFilePointer_File)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);

typedef BOOL(WINAPI* pSetFilePointerEx_File)(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);

typedef DWORD(WINAPI* pGetFileSize_File)(HANDLE hFile, LPDWORD lpFileSizeHigh);

typedef BOOL(WINAPI* pGetFileSizeEx_File)(HANDLE hFile, PLARGE_INTEGER lpFileSize);

typedef BOOL(WINAPI* pCloseHandle_File)(HANDLE hObject);

typedef HANDLE(WINAPI* pCreateFileMappingW_File)(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName);

typedef HANDLE(WINAPI* pCreateFileMappingA_File)(HANDLE hFile, LPSECURITY_ATTRIBUTES lpFileMappingAttributes, DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName);

typedef HANDLE(WINAPI* pFindFirstFileA_File)(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);

typedef HANDLE(WINAPI* pFindFirstFileW_File)(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);

typedef BOOL(WINAPI* pFindNextFileA_File)(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);

typedef BOOL(WINAPI* pFindNextFileW_File)(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData);

typedef BOOL(WINAPI* pFindClose_File)(HANDLE hFindFile);

// 虚拟注册表相关API类型定义
typedef LSTATUS(WINAPI* pRegOpenKeyExW)(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegOpenKeyExA)(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegOpenKeyW)(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegOpenKeyA)(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegCreateKeyExW)(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved, LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired, CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
typedef LSTATUS(WINAPI* pRegCreateKeyExA)(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved, LPSTR lpClass, DWORD dwOptions, REGSAM samDesired, CONST LPSECURITY_ATTRIBUTES lpSecurityAttributes, PHKEY phkResult, LPDWORD lpdwDisposition);
typedef LSTATUS(WINAPI* pRegCreateKeyW)(HKEY hKey, LPCWSTR lpSubKey, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegCreateKeyA)(HKEY hKey, LPCSTR lpSubKey, PHKEY phkResult);
typedef LSTATUS(WINAPI* pRegCloseKey)(HKEY hKey);
typedef LSTATUS(WINAPI* pRegQueryValueExW)(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
typedef LSTATUS(WINAPI* pRegQueryValueExA)(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
typedef LSTATUS(WINAPI* pRegGetValueW)(HKEY hKey, LPCWSTR lpSubKey, LPCWSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData);
typedef LSTATUS(WINAPI* pRegGetValueA)(HKEY hKey, LPCSTR lpSubKey, LPCSTR lpValue, DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData);
typedef LSTATUS(WINAPI* pRegSetValueExW)(HKEY hKey, LPCWSTR lpValueName, DWORD Reserved, DWORD dwType, CONST BYTE* lpData, DWORD cbData);
typedef LSTATUS(WINAPI* pRegSetValueExA)(HKEY hKey, LPCSTR lpValueName, DWORD Reserved, DWORD dwType, CONST BYTE* lpData, DWORD cbData);
typedef LSTATUS(WINAPI* pRegEnumKeyExW)(HKEY hKey, DWORD dwIndex, LPWSTR lpName, LPDWORD lpcName, LPDWORD lpReserved, LPWSTR lpClass, LPDWORD lpcClass, PFILETIME lpftLastWriteTime);
typedef LSTATUS(WINAPI* pRegEnumKeyExA)(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcName, LPDWORD lpReserved, LPSTR lpClass, LPDWORD lpcClass, PFILETIME lpftLastWriteTime);
typedef LSTATUS(WINAPI* pRegEnumValueW)(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
typedef LSTATUS(WINAPI* pRegEnumValueA)(HKEY hKey, DWORD dwIndex, LPSTR lpValueName, LPDWORD lpcchValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
typedef LSTATUS(WINAPI* pRegQueryInfoKeyW)(HKEY hKey, LPWSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);
typedef LSTATUS(WINAPI* pRegQueryInfoKeyA)(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD lpReserved, LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen, LPDWORD lpcbMaxClassLen, LPDWORD lpcValues, LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen, LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime);

// 代码页转换相关API类型定义
typedef int(WINAPI* pMultiByteToWideChar)(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);

typedef int(WINAPI* pWideCharToMultiByte)(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar);
