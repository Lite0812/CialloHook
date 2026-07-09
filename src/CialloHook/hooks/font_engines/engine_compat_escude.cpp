#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook.h"
#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>
#include <Shlwapi.h>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            using pGetPrivateProfileStringA = DWORD(WINAPI*)(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
            using pGetPrivateProfileStringW = DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
            using pGetPrivateProfileIntA = UINT(WINAPI*)(LPCSTR, LPCSTR, INT, LPCSTR);
            using pGetPrivateProfileIntW = UINT(WINAPI*)(LPCWSTR, LPCWSTR, INT, LPCWSTR);

            pGetPrivateProfileStringA sg_orgGetPrivateProfileStringA = GetPrivateProfileStringA;
            pGetPrivateProfileStringW sg_orgGetPrivateProfileStringW = GetPrivateProfileStringW;
            pGetPrivateProfileIntA sg_orgGetPrivateProfileIntA = GetPrivateProfileIntA;
            pGetPrivateProfileIntW sg_orgGetPrivateProfileIntW = GetPrivateProfileIntW;
            volatile LONG sg_escudeHooksInstalled = 0;
            std::wstring sg_escudeProductName;
            std::wstring sg_escudeFaceNameW;
            std::string sg_escudeFaceNameA;

            bool EqualsI(const char* left, const char* right)
            {
                return left && right && _stricmp(left, right) == 0;
            }

            bool EqualsI(const wchar_t* left, const wchar_t* right)
            {
                return left && right && _wcsicmp(left, right) == 0;
            }

            std::wstring GameRoot()
            {
                wchar_t path[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, path, MAX_PATH);
                PathRemoveFileSpecW(path);
                return path;
            }

            std::wstring FullPath(const wchar_t* path)
            {
                if (!path || !path[0])
                {
                    return L"";
                }
                wchar_t full[MAX_PATH] = {};
                DWORD len = GetFullPathNameW(path, MAX_PATH, full, nullptr);
                if (len == 0 || len >= MAX_PATH)
                {
                    return path;
                }
                return full;
            }

            std::wstring NormalizePath(std::wstring path)
            {
                for (wchar_t& ch : path)
                {
                    if (ch == L'/')
                    {
                        ch = L'\\';
                    }
                    ch = static_cast<wchar_t>(towlower(ch));
                }
                return path;
            }

            bool IsUnderRoot(const std::wstring& fullPath)
            {
                std::wstring root = NormalizePath(GameRoot());
                std::wstring full = NormalizePath(fullPath);
                if (root.empty() || full.size() < root.size())
                {
                    return false;
                }
                if (full.compare(0, root.size(), root) != 0)
                {
                    return false;
                }
                return full.size() == root.size() || full[root.size()] == L'\\';
            }

            std::wstring AnsiToWide(const char* text)
            {
                if (!text || !text[0])
                {
                    return L"";
                }
                int len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
                if (len <= 0)
                {
                    return L"";
                }
                std::wstring result(static_cast<size_t>(len), L'\0');
                MultiByteToWideChar(CP_ACP, 0, text, -1, &result[0], len);
                if (!result.empty() && result.back() == L'\0')
                {
                    result.pop_back();
                }
                return result;
            }

            std::string WideToAnsi(const std::wstring& text)
            {
                if (text.empty())
                {
                    return "";
                }
                int len = WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0)
                {
                    return "";
                }
                std::string result(static_cast<size_t>(len), '\0');
                WideCharToMultiByte(CP_ACP, 0, text.c_str(), -1, &result[0], len, nullptr, nullptr);
                if (!result.empty() && result.back() == '\0')
                {
                    result.pop_back();
                }
                return result;
            }

            std::wstring RootConfigurePath()
            {
                std::wstring path = GameRoot();
                if (!path.empty() && path.back() != L'\\')
                {
                    path += L"\\";
                }
                path += L"configure.cfg";
                return path;
            }

            bool ProbeEscudeProduct()
            {
                if (!sg_escudeProductName.empty())
                {
                    return true;
                }

                const std::wstring rootConfig = RootConfigurePath();
                wchar_t company[64] = {};
                wchar_t product[128] = {};
                sg_orgGetPrivateProfileStringW(L"General", L"Company", L"", company, _countof(company), rootConfig.c_str());
                sg_orgGetPrivateProfileStringW(L"General", L"Product", L"", product, _countof(product), rootConfig.c_str());
                if (_wcsicmp(company, L"ESCUDE") == 0 && product[0] != L'\0')
                {
                    sg_escudeProductName = product;
                    LogMessage(LogLevel::Info, L"EngineCompat: Escu:de product detected product=%s", sg_escudeProductName.c_str());
                    return true;
                }
                return false;
            }

            bool IsConfigureCfgPath(const std::wstring& fullPath)
            {
                if (fullPath.empty())
                {
                    return false;
                }
                const wchar_t* baseName = PathFindFileNameW(fullPath.c_str());
                if (!baseName || _wcsicmp(baseName, L"configure.cfg") != 0)
                {
                    return false;
                }
                if (IsUnderRoot(fullPath))
                {
                    return true;
                }
                if (!ProbeEscudeProduct())
                {
                    return false;
                }
                const std::wstring normalized = NormalizePath(fullPath);
                const std::wstring product = NormalizePath(sg_escudeProductName);
                const std::wstring suffix = L"\\escude\\" + product + L"\\configure.cfg";
                return normalized.size() >= suffix.size()
                    && normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix) == 0;
            }

            bool IsFontFaceRequestA(const char* appName, const char* keyName)
            {
                return EqualsI(appName, "Font") && (EqualsI(keyName, "Face") || EqualsI(keyName, "Font"));
            }

            bool IsFontFaceRequestW(const wchar_t* appName, const wchar_t* keyName)
            {
                return EqualsI(appName, L"Font") && (EqualsI(keyName, L"Face") || EqualsI(keyName, L"Font"));
            }

            bool ShouldVirtualizeFontFaceA(const char* appName, const char* keyName, const char* fileName)
            {
                return IsFontFaceRequestA(appName, keyName)
                    && ProbeEscudeProduct()
                    && IsConfigureCfgPath(FullPath(AnsiToWide(fileName).c_str()));
            }

            bool ShouldVirtualizeFontFaceW(const wchar_t* appName, const wchar_t* keyName, const wchar_t* fileName)
            {
                return IsFontFaceRequestW(appName, keyName)
                    && ProbeEscudeProduct()
                    && IsConfigureCfgPath(FullPath(fileName));
            }

            DWORD CopyProfileStringA(const std::string& value, char* out, DWORD outCount)
            {
                if (!out || outCount == 0)
                {
                    return static_cast<DWORD>(value.size());
                }
                strncpy_s(out, outCount, value.c_str(), _TRUNCATE);
                return static_cast<DWORD>(strlen(out));
            }

            DWORD CopyProfileStringW(const std::wstring& value, wchar_t* out, DWORD outCount)
            {
                if (!out || outCount == 0)
                {
                    return static_cast<DWORD>(value.size());
                }
                wcsncpy_s(out, outCount, value.c_str(), _TRUNCATE);
                return static_cast<DWORD>(wcslen(out));
            }

            DWORD WINAPI HookGetPrivateProfileStringA(LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName)
            {
                if (ShouldVirtualizeFontFaceA(lpAppName, lpKeyName, lpFileName))
                {
                    return CopyProfileStringA(sg_escudeFaceNameA, lpReturnedString, nSize);
                }
                return sg_orgGetPrivateProfileStringA(lpAppName, lpKeyName, lpDefault, lpReturnedString, nSize, lpFileName);
            }

            DWORD WINAPI HookGetPrivateProfileStringW(LPCWSTR lpAppName, LPCWSTR lpKeyName, LPCWSTR lpDefault, LPWSTR lpReturnedString, DWORD nSize, LPCWSTR lpFileName)
            {
                if (ShouldVirtualizeFontFaceW(lpAppName, lpKeyName, lpFileName))
                {
                    return CopyProfileStringW(sg_escudeFaceNameW, lpReturnedString, nSize);
                }
                return sg_orgGetPrivateProfileStringW(lpAppName, lpKeyName, lpDefault, lpReturnedString, nSize, lpFileName);
            }

            UINT WINAPI HookGetPrivateProfileIntA(LPCSTR lpAppName, LPCSTR lpKeyName, INT nDefault, LPCSTR lpFileName)
            {
                return sg_orgGetPrivateProfileIntA(lpAppName, lpKeyName, nDefault, lpFileName);
            }

            UINT WINAPI HookGetPrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, INT nDefault, LPCWSTR lpFileName)
            {
                return sg_orgGetPrivateProfileIntW(lpAppName, lpKeyName, nDefault, lpFileName);
            }

            void InstallEscudeProfileHooks()
            {
                if (InterlockedCompareExchange(&sg_escudeHooksInstalled, 1, 0) != 0)
                {
                    return;
                }

                bool failed = false;
                if (BeginDetourBatch())
                {
                    failed = !TryDetourAttach(&sg_orgGetPrivateProfileStringA, HookGetPrivateProfileStringA) || failed;
                    failed = !TryDetourAttach(&sg_orgGetPrivateProfileStringW, HookGetPrivateProfileStringW) || failed;
                    failed = !TryDetourAttach(&sg_orgGetPrivateProfileIntA, HookGetPrivateProfileIntA) || failed;
                    failed = !TryDetourAttach(&sg_orgGetPrivateProfileIntW, HookGetPrivateProfileIntW) || failed;
                    failed = !EndDetourBatch(L"Escu:de profile hooks") || failed;
                }
                else
                {
                    failed = true;
                }

                if (failed)
                {
                    InterlockedExchange(&sg_escudeHooksInstalled, 0);
                    LogMessage(LogLevel::Warn, L"EngineCompat: Escu:de profile hooks install failed");
                    return;
                }
                LogMessage(LogLevel::Info, L"EngineCompat: Escu:de profile hooks installed");
            }
        }

        void ApplyEscudeFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!HasEngine(state, EngineEscude) || !settings.engineCompat.enableEscudeFontConfig)
            {
                return;
            }

            sg_escudeFaceNameW = ResolveReplacementFaceName(settings.font);
            sg_escudeFaceNameA = WideToAnsi(sg_escudeFaceNameW);
            if (sg_escudeFaceNameW.empty())
            {
                return;
            }
            InstallEscudeProfileHooks();
        }
    }
}
