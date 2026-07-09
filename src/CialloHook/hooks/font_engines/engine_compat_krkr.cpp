#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>
#include <Psapi.h>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            volatile LONG sg_krkrMapPatchAttempted = 0;

            bool KrkrFindFirstFileInRoot(const wchar_t* pattern)
            {
                wchar_t modulePath[MAX_PATH] = {};
                const DWORD pathLen = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
                if (pathLen == 0 || pathLen >= MAX_PATH)
                {
                    return false;
                }

                wchar_t* fileName = modulePath + pathLen;
                while (fileName > modulePath && fileName[-1] != L'\\' && fileName[-1] != L'/')
                {
                    --fileName;
                }
                if (fileName == modulePath)
                {
                    return false;
                }

                const size_t dirLen = static_cast<size_t>(fileName - modulePath);
                const int patternLen = lstrlenW(pattern);
                if (patternLen <= 0 || dirLen + static_cast<size_t>(patternLen) >= MAX_PATH)
                {
                    return false;
                }

                lstrcpyW(modulePath + dirLen, pattern);
                WIN32_FIND_DATAW data = {};
                HANDLE hFind = FindFirstFileW(modulePath, &data);
                if (hFind == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                FindClose(hFind);
                return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }

            bool HasKrkrRootMarker()
            {
                return KrkrFindFirstFileInRoot(L"*.xp3") && KrkrFindFirstFileInRoot(L"font\\*.tft");
            }

            bool BytesEqual(const BYTE* left, const BYTE* right, size_t len)
            {
                for (size_t i = 0; i < len; ++i)
                {
                    if (left[i] != right[i])
                    {
                        return false;
                    }
                }
                return true;
            }

            bool PatchBytes(BYTE* address, const BYTE* replacement, size_t len)
            {
                DWORD oldProtect = 0;
                if (!VirtualProtect(address, len, PAGE_EXECUTE_READWRITE, &oldProtect))
                {
                    return false;
                }

                for (size_t i = 0; i < len; ++i)
                {
                    address[i] = replacement[i];
                }

                FlushInstructionCache(GetCurrentProcess(), address, len);
                DWORD ignoredProtect = 0;
                VirtualProtect(address, len, oldProtect, &ignoredProtect);
                return true;
            }

            DWORD PatchPatternInMainModule(const BYTE* pattern, const BYTE* replacement, size_t len)
            {
                HMODULE hMain = GetModuleHandleW(nullptr);
                MODULEINFO moduleInfo = {};
                if (!hMain || !GetModuleInformation(GetCurrentProcess(), hMain, &moduleInfo, sizeof(moduleInfo)))
                {
                    return 0;
                }

                BYTE* base = static_cast<BYTE*>(moduleInfo.lpBaseOfDll);
                const size_t size = moduleInfo.SizeOfImage;
                if (!base || size < len)
                {
                    return 0;
                }

                DWORD patched = 0;
                for (size_t offset = 0; offset <= size - len; ++offset)
                {
                    BYTE* current = base + offset;
                    if (!BytesEqual(current, pattern, len))
                    {
                        continue;
                    }

                    if (PatchBytes(current, replacement, len))
                    {
                        ++patched;
                        offset += len - 1;
                    }
                }
                return patched;
            }

            void PatchMapPrerenderedFontName(const AppSettings& settings, const EngineCompatState& state)
            {
                if (!settings.engineCompat.krkrMapPrerenderedFontPatch || !HasEngine(state, EngineKrkr))
                {
                    return;
                }
                if (InterlockedExchange(&sg_krkrMapPatchAttempted, 1) != 0)
                {
                    return;
                }

                static const char kMethodA[] = "mapPrerenderedFont";
                static const char kDisabledA[] = "sfhPrerenderedFont";
                static const wchar_t kMethodW[] = L"mapPrerenderedFont";
                static const wchar_t kDisabledW[] = L"sfhPrerenderedFont";

                DWORD patched = 0;
                patched += PatchPatternInMainModule(reinterpret_cast<const BYTE*>(kMethodA), reinterpret_cast<const BYTE*>(kDisabledA), sizeof(kMethodA) - 1);
                patched += PatchPatternInMainModule(reinterpret_cast<const BYTE*>(kMethodW), reinterpret_cast<const BYTE*>(kDisabledW), sizeof(kMethodW) - sizeof(wchar_t));

                LogMessage(LogLevel::Info, L"EngineCompat: KRKR mapPrerenderedFont disabled patches=%lu", patched);
            }
        }

        void ApplyKrkrFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!HasEngine(state, EngineKrkr) || !HasKrkrRootMarker())
            {
                return;
            }

            AddEngineKrkrTftCacheRule();
            PatchMapPrerenderedFontName(settings, state);
        }
    }
}
