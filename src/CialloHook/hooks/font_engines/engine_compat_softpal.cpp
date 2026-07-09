#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook.h"
#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            using pSoftpalPalFontBegin = int(__cdecl*)();
            using pSoftpalPalFontSetType = int(__cdecl*)(int);
            using pSoftpalPalFontGetType = int(__cdecl*)();
            using pLoadLibraryA = HMODULE(WINAPI*)(LPCSTR);
            using pLoadLibraryW = HMODULE(WINAPI*)(LPCWSTR);
            using pLoadLibraryExA = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
            using pLoadLibraryExW = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

            pSoftpalPalFontBegin sg_orgSoftpalPalFontBegin = nullptr;
            pSoftpalPalFontSetType sg_orgSoftpalPalFontSetType = nullptr;
            pSoftpalPalFontGetType sg_orgSoftpalPalFontGetType = nullptr;
            pLoadLibraryA sg_orgLoadLibraryA = LoadLibraryA;
            pLoadLibraryW sg_orgLoadLibraryW = LoadLibraryW;
            pLoadLibraryExA sg_orgLoadLibraryExA = LoadLibraryExA;
            pLoadLibraryExW sg_orgLoadLibraryExW = LoadLibraryExW;
            volatile LONG sg_softpalPalFontHooksInstalled = 0;
            volatile LONG sg_softpalLoadLibraryHooksInstalled = 0;

            constexpr int kSoftpalOriginalFontType = 4;
            constexpr int kSoftpalHookableFontType = 1;

            HMODULE GetPalModule()
            {
                HMODULE hPal = GetModuleHandleW(L"Pal.dll");
                if (!hPal)
                {
                    hPal = GetModuleHandleW(L"PAL.dll");
                }
                return hPal;
            }

            bool IsSoftpalPalLibraryNameW(const wchar_t* fileName)
            {
                if (!fileName || !fileName[0]) return false;
                const wchar_t* baseName = fileName;
                for (const wchar_t* p = fileName; *p; ++p)
                {
                    if (*p == L'\\' || *p == L'/') baseName = p + 1;
                }
                return _wcsicmp(baseName, L"Pal.dll") == 0;
            }

            bool IsSoftpalPalLibraryNameA(const char* fileName)
            {
                if (!fileName || !fileName[0]) return false;
                const char* baseName = fileName;
                for (const char* p = fileName; *p; ++p)
                {
                    if (*p == '\\' || *p == '/') baseName = p + 1;
                }
                return _stricmp(baseName, "Pal.dll") == 0;
            }

            void NormalizeSoftpalFontType(const wchar_t* reason)
            {
                if (!sg_orgSoftpalPalFontGetType || !sg_orgSoftpalPalFontSetType)
                {
                    return;
                }

                __try
                {
                    const int type = sg_orgSoftpalPalFontGetType();
                    if (type == kSoftpalOriginalFontType)
                    {
                        sg_orgSoftpalPalFontSetType(kSoftpalHookableFontType);
                        LogMessage(LogLevel::Info, L"EngineCompat: Softpal font type normalized reason=%s original=%d mapped=%d",
                            reason ? reason : L"normalize", kSoftpalOriginalFontType, kSoftpalHookableFontType);
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }

            int __cdecl HookSoftpalPalFontSetType(int type)
            {
                int mappedType = type;
                if (type == kSoftpalOriginalFontType)
                {
                    mappedType = kSoftpalHookableFontType;
                    LogMessage(LogLevel::Info, L"EngineCompat: Softpal PalFontSetType mapped original=%d mapped=%d", type, mappedType);
                }

                const int result = sg_orgSoftpalPalFontSetType ? sg_orgSoftpalPalFontSetType(mappedType) : 0;
                NormalizeSoftpalFontType(L"after-set-type");
                return result;
            }

            int __cdecl HookSoftpalPalFontBegin()
            {
                NormalizeSoftpalFontType(L"before-begin");
                return sg_orgSoftpalPalFontBegin ? sg_orgSoftpalPalFontBegin() : 0;
            }

            void InstallSoftpalPalFontHooks();

            void TryInstallSoftpalPalFontHooksAfterLoadA(const char* fileName, HMODULE module)
            {
                UNREFERENCED_PARAMETER(fileName);
                if (module)
                {
                    InstallSoftpalPalFontHooks();
                }
            }

            void TryInstallSoftpalPalFontHooksAfterLoadW(const wchar_t* fileName, HMODULE module)
            {
                UNREFERENCED_PARAMETER(fileName);
                if (module)
                {
                    InstallSoftpalPalFontHooks();
                }
            }

            HMODULE WINAPI HookSoftpalLoadLibraryA(LPCSTR lpLibFileName)
            {
                HMODULE module = sg_orgLoadLibraryA ? sg_orgLoadLibraryA(lpLibFileName) : nullptr;
                TryInstallSoftpalPalFontHooksAfterLoadA(lpLibFileName, module);
                return module;
            }

            HMODULE WINAPI HookSoftpalLoadLibraryW(LPCWSTR lpLibFileName)
            {
                HMODULE module = sg_orgLoadLibraryW ? sg_orgLoadLibraryW(lpLibFileName) : nullptr;
                TryInstallSoftpalPalFontHooksAfterLoadW(lpLibFileName, module);
                return module;
            }

            HMODULE WINAPI HookSoftpalLoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
            {
                HMODULE module = sg_orgLoadLibraryExA ? sg_orgLoadLibraryExA(lpLibFileName, hFile, dwFlags) : nullptr;
                TryInstallSoftpalPalFontHooksAfterLoadA(lpLibFileName, module);
                return module;
            }

            HMODULE WINAPI HookSoftpalLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
            {
                HMODULE module = sg_orgLoadLibraryExW ? sg_orgLoadLibraryExW(lpLibFileName, hFile, dwFlags) : nullptr;
                TryInstallSoftpalPalFontHooksAfterLoadW(lpLibFileName, module);
                return module;
            }

            void InstallSoftpalLoadLibraryHooks()
            {
                if (InterlockedCompareExchange(&sg_softpalLoadLibraryHooksInstalled, 1, 0) != 0)
                {
                    return;
                }

                bool failed = false;
                if (BeginDetourBatch())
                {
                    failed = !TryDetourAttach(&sg_orgLoadLibraryA, HookSoftpalLoadLibraryA) || failed;
                    failed = !TryDetourAttach(&sg_orgLoadLibraryW, HookSoftpalLoadLibraryW) || failed;
                    failed = !TryDetourAttach(&sg_orgLoadLibraryExA, HookSoftpalLoadLibraryExA) || failed;
                    failed = !TryDetourAttach(&sg_orgLoadLibraryExW, HookSoftpalLoadLibraryExW) || failed;
                    failed = !EndDetourBatch(L"Softpal LoadLibrary hooks") || failed;
                }
                else
                {
                    failed = true;
                }

                if (failed)
                {
                    InterlockedExchange(&sg_softpalLoadLibraryHooksInstalled, 0);
                    LogMessage(LogLevel::Warn, L"EngineCompat: Softpal LoadLibrary hooks install failed");
                    return;
                }

                LogMessage(LogLevel::Info, L"EngineCompat: Softpal LoadLibrary hooks installed");
            }

            void InstallSoftpalPalFontHooks()
            {
                if (InterlockedCompareExchange(&sg_softpalPalFontHooksInstalled, 1, 0) != 0)
                {
                    return;
                }

                HMODULE hPal = GetPalModule();
                FARPROC beginProc = hPal ? GetProcAddress(hPal, "PalFontBegin") : nullptr;
                FARPROC setTypeProc = hPal ? GetProcAddress(hPal, "PalFontSetType") : nullptr;
                FARPROC getTypeProc = hPal ? GetProcAddress(hPal, "PalFontGetType") : nullptr;
                if (!hPal || !beginProc || !setTypeProc || !getTypeProc)
                {
                    InterlockedExchange(&sg_softpalPalFontHooksInstalled, 0);
                    return;
                }

                sg_orgSoftpalPalFontBegin = reinterpret_cast<pSoftpalPalFontBegin>(beginProc);
                sg_orgSoftpalPalFontSetType = reinterpret_cast<pSoftpalPalFontSetType>(setTypeProc);
                sg_orgSoftpalPalFontGetType = reinterpret_cast<pSoftpalPalFontGetType>(getTypeProc);

                bool failed = false;
                if (BeginDetourBatch())
                {
                    failed = !TryDetourAttach(&sg_orgSoftpalPalFontBegin, HookSoftpalPalFontBegin) || failed;
                    failed = !TryDetourAttach(&sg_orgSoftpalPalFontSetType, HookSoftpalPalFontSetType) || failed;
                    failed = !EndDetourBatch(L"Softpal Pal.dll font hooks") || failed;
                }
                else
                {
                    failed = true;
                }

                if (failed)
                {
                    sg_orgSoftpalPalFontBegin = nullptr;
                    sg_orgSoftpalPalFontSetType = nullptr;
                    sg_orgSoftpalPalFontGetType = nullptr;
                    InterlockedExchange(&sg_softpalPalFontHooksInstalled, 0);
                    LogMessage(LogLevel::Warn, L"EngineCompat: Softpal Pal.dll font hooks install failed");
                    return;
                }

                LogMessage(LogLevel::Info, L"EngineCompat: Softpal Pal.dll font hooks installed");
                NormalizeSoftpalFontType(L"install");
            }
        }

        void ApplySoftpalFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (HasEngine(state, EngineSoftpal))
            {
                AddEngineHideFileRule(L"DEFAULT_FONT.DAT", true, true, true);
                AddEngineHideFileRule(L"dll\\DEFAULT_FONT.DAT", true, true, true);
            }
            if (HasEngine(state, EngineSoftpal) && settings.engineCompat.softpalPalDllShim)
            {
                InstallSoftpalLoadLibraryHooks();
                InstallSoftpalPalFontHooks();
            }
        }
    }
}
