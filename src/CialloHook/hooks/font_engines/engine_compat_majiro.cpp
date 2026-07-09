#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>
#include <Psapi.h>
#include <mutex>
#include <vector>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            volatile LONG sg_majiroRuntimeFlushAttempted = 0;

#if defined(_M_IX86)
            struct MajiroRuntimeFontCacheLayout
            {
                BYTE* glyphWidthTable = nullptr;
                DWORD* glyphOffsetTable = nullptr;
                DWORD* dataCapacity = nullptr;
                DWORD* dataUsed = nullptr;
                BYTE** dataBuffer = nullptr;
                DWORD* dirtyFlag = nullptr;
            };

            std::mutex sg_majiroRuntimeCacheMutex;
            bool sg_majiroRuntimeCacheScanned = false;
            std::vector<MajiroRuntimeFontCacheLayout> sg_majiroRuntimeFontCaches;

            DWORD ReadU32(const BYTE* p)
            {
                DWORD value = 0;
                memcpy(&value, p, sizeof(value));
                return value;
            }

            bool MemoryContains(const BYTE* bytes, size_t size, const char* needle)
            {
                if (!bytes || !needle || !needle[0])
                {
                    return false;
                }
                const size_t needleLen = strlen(needle);
                if (size < needleLen)
                {
                    return false;
                }
                for (size_t i = 0; i <= size - needleLen; ++i)
                {
                    if (memcmp(bytes + i, needle, needleLen) == 0)
                    {
                        return true;
                    }
                }
                return false;
            }

            bool AddressInRange(uintptr_t address, uintptr_t begin, uintptr_t end, size_t size = 1)
            {
                if (address < begin || address >= end)
                {
                    return false;
                }
                return size <= static_cast<size_t>(end - address);
            }

            bool RangeWritable(void* address, SIZE_T size)
            {
                if (!address || size == 0)
                {
                    return false;
                }

                BYTE* current = static_cast<BYTE*>(address);
                BYTE* end = current + size;
                if (end < current)
                {
                    return false;
                }

                while (current < end)
                {
                    MEMORY_BASIC_INFORMATION mbi = {};
                    if (VirtualQuery(current, &mbi, sizeof(mbi)) != sizeof(mbi))
                    {
                        return false;
                    }
                    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
                    {
                        return false;
                    }

                    DWORD protect = mbi.Protect & 0xFF;
                    const bool writable = protect == PAGE_READWRITE
                        || protect == PAGE_WRITECOPY
                        || protect == PAGE_EXECUTE_READWRITE
                        || protect == PAGE_EXECUTE_WRITECOPY;
                    if (!writable)
                    {
                        return false;
                    }

                    BYTE* regionEnd = static_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
                    if (regionEnd <= current)
                    {
                        return false;
                    }
                    current = regionEnd < end ? regionEnd : end;
                }
                return true;
            }

            bool TryAddRuntimeCacheLayout(uintptr_t moduleBegin, uintptr_t moduleEnd,
                uintptr_t glyphWidthTable, uintptr_t glyphOffsetTable, uintptr_t dataBuffer,
                uintptr_t dataUsed, uintptr_t dataCapacity, uintptr_t dirtyFlag)
            {
                if (!AddressInRange(glyphWidthTable, moduleBegin, moduleEnd, 0x10000)
                    || !AddressInRange(glyphOffsetTable, moduleBegin, moduleEnd, 0x40000)
                    || !AddressInRange(dataBuffer, moduleBegin, moduleEnd, sizeof(void*))
                    || !AddressInRange(dataUsed, moduleBegin, moduleEnd, sizeof(DWORD))
                    || !AddressInRange(dataCapacity, moduleBegin, moduleEnd, sizeof(DWORD)))
                {
                    return false;
                }

                for (const MajiroRuntimeFontCacheLayout& existing : sg_majiroRuntimeFontCaches)
                {
                    if (existing.glyphWidthTable == reinterpret_cast<BYTE*>(glyphWidthTable)
                        && existing.glyphOffsetTable == reinterpret_cast<DWORD*>(glyphOffsetTable))
                    {
                        return false;
                    }
                }

                MajiroRuntimeFontCacheLayout layout = {};
                layout.glyphWidthTable = reinterpret_cast<BYTE*>(glyphWidthTable);
                layout.glyphOffsetTable = reinterpret_cast<DWORD*>(glyphOffsetTable);
                layout.dataBuffer = reinterpret_cast<BYTE**>(dataBuffer);
                layout.dataUsed = reinterpret_cast<DWORD*>(dataUsed);
                layout.dataCapacity = reinterpret_cast<DWORD*>(dataCapacity);
                if (dirtyFlag && AddressInRange(dirtyFlag, moduleBegin, moduleEnd, sizeof(DWORD)))
                {
                    layout.dirtyFlag = reinterpret_cast<DWORD*>(dirtyFlag);
                }
                sg_majiroRuntimeFontCaches.push_back(layout);
                return true;
            }

            uintptr_t FindRuntimeCacheDirtyFlag(const BYTE* moduleBytes, size_t moduleSize,
                uintptr_t moduleBegin, uintptr_t moduleEnd, uintptr_t dataBuffer)
            {
                const uintptr_t searchBegin = dataBuffer;
                const uintptr_t searchEnd = dataBuffer + 0x80;
                if (searchEnd < searchBegin)
                {
                    return 0;
                }

                for (size_t i = 0; i + 10 <= moduleSize; ++i)
                {
                    const BYTE* p = moduleBytes + i;
                    if (p[0] != 0xC7 || p[1] != 0x05 || ReadU32(p + 6) != 1)
                    {
                        continue;
                    }

                    const uintptr_t candidate = static_cast<uintptr_t>(ReadU32(p + 2));
                    if (candidate >= searchBegin && candidate < searchEnd
                        && AddressInRange(candidate, moduleBegin, moduleEnd, sizeof(DWORD)))
                    {
                        return candidate;
                    }
                }
                return 0;
            }

            void DiscoverRuntimeFontCachesLocked()
            {
                if (sg_majiroRuntimeCacheScanned)
                {
                    return;
                }
                sg_majiroRuntimeCacheScanned = true;
                sg_majiroRuntimeFontCaches.clear();

                HMODULE mainModule = GetModuleHandleW(nullptr);
                MODULEINFO moduleInfo = {};
                if (!mainModule || !GetModuleInformation(GetCurrentProcess(), mainModule, &moduleInfo, sizeof(moduleInfo))
                    || !moduleInfo.lpBaseOfDll || moduleInfo.SizeOfImage < 0x1000)
                {
                    return;
                }

                BYTE* moduleBytes = static_cast<BYTE*>(moduleInfo.lpBaseOfDll);
                const size_t moduleSize = static_cast<size_t>(moduleInfo.SizeOfImage);
                const uintptr_t moduleBegin = reinterpret_cast<uintptr_t>(moduleBytes);
                const uintptr_t moduleEnd = moduleBegin + moduleSize;
                if (moduleEnd < moduleBegin || !MemoryContains(moduleBytes, moduleSize, "savedata\\fc_%s_%03dx%03d.fcd"))
                {
                    return;
                }

                for (size_t i = 0; i + 72 <= moduleSize; ++i)
                {
                    BYTE* p = moduleBytes + i;
                    if (p[0] != 0xB9 || ReadU32(p + 1) != 0x4000
                        || p[5] != 0xBF
                        || p[10] != 0xF3 || p[11] != 0xAB
                        || p[12] != 0xB9 || ReadU32(p + 13) != 0x10000
                        || p[17] != 0xBF
                        || p[22] != 0x68 || ReadU32(p + 23) != 0x80000
                        || p[27] != 0xF3 || p[28] != 0xAB
                        || p[29] != 0xE8)
                    {
                        continue;
                    }

                    const uintptr_t glyphWidthTable = static_cast<uintptr_t>(ReadU32(p + 6));
                    const uintptr_t glyphOffsetTable = static_cast<uintptr_t>(ReadU32(p + 18));
                    uintptr_t dataBuffer = 0;
                    uintptr_t dataUsed = 0;
                    uintptr_t dataCapacity = 0;

                    for (size_t j = 34; j + 10 <= 96 && i + j + 10 <= moduleSize; ++j)
                    {
                        BYTE* q = p + j;
                        if (q[0] == 0xA3)
                        {
                            uintptr_t candidate = static_cast<uintptr_t>(ReadU32(q + 1));
                            if (AddressInRange(candidate, moduleBegin, moduleEnd, sizeof(void*)))
                            {
                                dataBuffer = candidate;
                            }
                        }
                        else if (q[0] == 0xC7 && q[1] == 0x05)
                        {
                            uintptr_t candidate = static_cast<uintptr_t>(ReadU32(q + 2));
                            DWORD value = ReadU32(q + 6);
                            if (value == 4 && AddressInRange(candidate, moduleBegin, moduleEnd, sizeof(DWORD)))
                            {
                                dataUsed = candidate;
                            }
                            else if (value == 0x80000 && AddressInRange(candidate, moduleBegin, moduleEnd, sizeof(DWORD)))
                            {
                                dataCapacity = candidate;
                            }
                        }
                    }

                    const uintptr_t dirtyFlag = dataBuffer
                        ? FindRuntimeCacheDirtyFlag(moduleBytes, moduleSize, moduleBegin, moduleEnd, dataBuffer)
                        : 0;
                    TryAddRuntimeCacheLayout(moduleBegin, moduleEnd,
                        glyphWidthTable, glyphOffsetTable, dataBuffer, dataUsed, dataCapacity, dirtyFlag);
                }
            }

            void FlushRuntimeFontCaches()
            {
                if (InterlockedExchange(&sg_majiroRuntimeFlushAttempted, 1) != 0)
                {
                    return;
                }

                std::lock_guard<std::mutex> lock(sg_majiroRuntimeCacheMutex);
                DiscoverRuntimeFontCachesLocked();

                size_t resetCount = 0;
                for (MajiroRuntimeFontCacheLayout& layout : sg_majiroRuntimeFontCaches)
                {
                    if (!RangeWritable(layout.glyphWidthTable, 0x10000)
                        || !RangeWritable(layout.glyphOffsetTable, 0x40000)
                        || !RangeWritable(layout.dataUsed, sizeof(DWORD)))
                    {
                        continue;
                    }

                    SecureZeroMemory(layout.glyphWidthTable, 0x10000);
                    SecureZeroMemory(layout.glyphOffsetTable, 0x40000);
                    *layout.dataUsed = 4;
                    if (layout.dirtyFlag && RangeWritable(layout.dirtyFlag, sizeof(DWORD)))
                    {
                        *layout.dirtyFlag = 1;
                    }
                    ++resetCount;
                }

                LogMessage(LogLevel::Info, L"EngineCompat: Majiro runtime font cache flush layouts=%u reset=%u",
                    static_cast<unsigned>(sg_majiroRuntimeFontCaches.size()), static_cast<unsigned>(resetCount));
            }
#else
            void FlushRuntimeFontCaches()
            {
                if (InterlockedExchange(&sg_majiroRuntimeFlushAttempted, 1) == 0)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Majiro runtime font cache flush skipped on non-x86 build");
                }
            }
#endif
        }

        void ApplyMajiroFileCompat(const AppSettings&, const EngineCompatState& state)
        {
            if (HasEngine(state, EngineMajiro))
            {
                AddEngineMajiroFontCacheRule();
                FlushRuntimeFontCaches();
            }
        }
    }
}
