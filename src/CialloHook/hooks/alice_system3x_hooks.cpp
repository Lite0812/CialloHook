#include "alice_system3x_hooks.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <strsafe.h>

#include "../../RuntimeCore/base/Str.h"
#include "../../RuntimeCore/hook/Hook_API.h"
#include "../../RuntimeCore/io/CustomPakVFS.h"
#include "../../../third/detours/include/detours.h"

using namespace Rut::HookX;

namespace CialloHook
{
namespace AliceSystem3xHooks
{
    namespace
    {
        AliceSystem3xSettings sg_settings;
        std::wstring sg_gameDir;
        std::vector<std::wstring> sg_patchFolders;

        static void Log(const wchar_t* level, const wchar_t* format, ...)
        {
            LogLevel logLevel = LogLevel::Info;
            if (level && _wcsicmp(level, L"DEBUG") == 0)
            {
                logLevel = LogLevel::Debug;
            }
            else if (level && _wcsicmp(level, L"WARN") == 0)
            {
                logLevel = LogLevel::Warn;
            }
            else if (level && _wcsicmp(level, L"ERROR") == 0)
            {
                logLevel = LogLevel::Error;
            }

            wchar_t message[2048] = {};
            va_list args;
            va_start(args, format);
            StringCchVPrintfW(message, _countof(message), format, args);
            va_end(args);
            LogMessage(logLevel, L"AliceSystem3x: %s", message);
        }

        static std::wstring JoinPathBasic(const std::wstring& left, const std::wstring& right)
        {
            if (left.empty()) return right;
            if (right.empty()) return left;
            if (left.back() == L'\\' || left.back() == L'/') return left + right;
            return left + L"\\" + right;
        }

        static bool IsAbsolutePath(const std::wstring& path)
        {
            if (path.size() >= 2 && path[1] == L':') return true;
            if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
            return false;
        }

        static std::wstring GetGameDirectory()
        {
            wchar_t exePath[MAX_PATH] = {};
            DWORD length = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(_countof(exePath)));
            if (length == 0 || length >= _countof(exePath))
            {
                return L".";
            }
            std::wstring gameDir = exePath;
            size_t pos = gameDir.find_last_of(L"\\/");
            return pos == std::wstring::npos ? L"." : gameDir.substr(0, pos);
        }

        static std::wstring ToAbsoluteGamePath(const std::wstring& path)
        {
            return IsAbsolutePath(path) ? path : JoinPathBasic(sg_gameDir, path);
        }

        static bool IsDirectoryPath(const std::wstring& path)
        {
            DWORD attr = GetFileAttributesW(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        static bool AppendUniquePath(std::vector<std::wstring>& paths, const std::wstring& path)
        {
            for (const std::wstring& existing : paths)
            {
                if (_wcsicmp(existing.c_str(), path.c_str()) == 0)
                {
                    return false;
                }
            }
            paths.push_back(path);
            return true;
        }

        static void InitializePatchFolders(const AliceSystem3xSettings& settings, const FilePatchSettings& filePatchSettings)
        {
            sg_patchFolders.clear();
            std::vector<std::wstring> configured = settings.patchFolders;
            if (configured.empty() && filePatchSettings.enable)
            {
                configured = filePatchSettings.patchFolders;
            }
            if (configured.empty())
            {
                configured.push_back(L"patch");
            }

            for (auto it = configured.rbegin(); it != configured.rend(); ++it)
            {
                std::wstring folder = Rut::StrX::Trim(*it);
                if (folder.empty())
                {
                    continue;
                }
                std::wstring absolute = ToAbsoluteGamePath(folder);
                if (!IsDirectoryPath(absolute) && settings.enableLog)
                {
                    Log(L"INFO", L"patch folder missing on disk, still try CustomPak path: %s", absolute.c_str());
                }
                AppendUniquePath(sg_patchFolders, absolute);
            }
        }

        static bool IsRegularFilePath(const std::wstring& path)
        {
            DWORD attr = GetFileAttributesW(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        static bool TryGetLooseFileSize(const std::wstring& path, DWORD& outSize)
        {
            outSize = 0;
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            LARGE_INTEGER size = {};
            bool ok = GetFileSizeEx(file, &size) != FALSE;
            CloseHandle(file);
            if (!ok || size.QuadPart <= 0 || size.QuadPart > 0x7fffffff)
            {
                return false;
            }
            if (static_cast<unsigned long long>(size.QuadPart) > sg_settings.maxFileSize)
            {
                if (sg_settings.enableLog)
                {
                    Log(L"WARN", L"loose file too large, fallback: %s size=%lld max=%u", path.c_str(), size.QuadPart, sg_settings.maxFileSize);
                }
                return false;
            }
            outSize = static_cast<DWORD>(size.QuadPart);
            return true;
        }

        static bool TryResolveLoosePath(const std::wstring& path, DWORD& outSize, std::shared_ptr<const std::vector<uint8_t>>& outData)
        {
            outSize = 0;
            outData.reset();
            if (IsRegularFilePath(path) && TryGetLooseFileSize(path, outSize))
            {
                return true;
            }
            std::shared_ptr<const std::vector<uint8_t>> data;
            if (ResolveCustomPakVFSData(path.c_str(), data) && data && !data->empty())
            {
                if (data->size() > sg_settings.maxFileSize || data->size() > 0x7fffffff)
                {
                    if (sg_settings.enableLog)
                    {
                        Log(L"WARN", L"CustomPak loose file too large, fallback: %s size=%u max=%u", path.c_str(), static_cast<unsigned>(data->size()), sg_settings.maxFileSize);
                    }
                    return false;
                }
                outSize = static_cast<DWORD>(data->size());
                outData = std::move(data);
                return true;
            }
            return false;
        }

        static bool TryFindLooseCandidate(const std::vector<std::wstring>& candidates, std::wstring& outPath, DWORD& outSize, std::shared_ptr<const std::vector<uint8_t>>& outData, std::wstring* outName)
        {
            outPath.clear();
            outSize = 0;
            outData.reset();
            for (const std::wstring& name : candidates)
            {
                for (const std::wstring& patchRoot : sg_patchFolders)
                {
                    std::wstring path = JoinPathBasic(patchRoot, name);
                    if (TryResolveLoosePath(path, outSize, outData))
                    {
                        outPath = path;
                        if (outName)
                        {
                            *outName = name;
                        }
                        return true;
                    }
                }
                std::wstring gameRootPath = JoinPathBasic(sg_gameDir, name);
                if (TryResolveLoosePath(gameRootPath, outSize, outData))
                {
                    outPath = gameRootPath;
                    if (outName)
                    {
                        *outName = name;
                    }
                    return true;
                }
            }
            return false;
        }

#if defined(_M_IX86)
    static constexpr int kAldOffsetEntrySize = 3;
    static constexpr int kAldOffsetShift = 8;
    static constexpr int kAldPayloadNameOffset = 0x10;
    static constexpr int kAldPayloadNameMax = 32;

    static constexpr int kAldGroupArchivesOffset = 4;
    static constexpr int kAldGroupResourcesOffset = 12;
    static constexpr int kAldGroupFileCountOffset = 20;
    static constexpr int kArchiveEntryStride = 28;
    static constexpr int kArchivePathOffset = 8;
    static constexpr int kResourceRecordStride = 20;
    static constexpr int kResourceArchiveNoOffset = 0;
    static constexpr int kResourceEntryNoOffset = 2;

    using FnGetAldSize = int(__thiscall*)(void* self, int logicalId);
    using FnReadAld = bool(__thiscall*)(void* self, int logicalId, void* outBuffer);
    using FnExistsAld = bool(__thiscall*)(void* self, int logicalId);
    using FnResizeBuffer = bool(__thiscall*)(void* buffer, unsigned int size);

    static FnGetAldSize RealGetAldSize = nullptr;
    static FnReadAld RealReadAld = nullptr;
    static FnExistsAld RealExistsAld = nullptr;
    static FnResizeBuffer EngineResizeBuffer = nullptr;
    static bool g_installed = false;
    static volatile LONG g_missLogCount = 0;
    static thread_local bool g_inHook = false;

    enum class ResolveSource
    {
        None,
        Heuristic,
    };

    struct HookTargets
    {
        FnGetAldSize getSize = nullptr;
        FnReadAld read = nullptr;
        FnExistsAld exists = nullptr;
        FnResizeBuffer resize = nullptr;
        uintptr_t getSizeRva = 0;
        uintptr_t readRva = 0;
        uintptr_t existsRva = 0;
        uintptr_t resizeRva = 0;
        ResolveSource source = ResolveSource::None;
    };

    struct PatternByte
    {
        uint8_t data = 0;
        uint8_t mask = 0;
    };

    static const wchar_t* ResolveSourceName(ResolveSource source)
    {
        switch (source)
        {
        case ResolveSource::Heuristic: return L"Heuristic";
        default: return L"None";
        }
    }

    static size_t GetMainModuleSize(HMODULE module)
    {
        auto* base = reinterpret_cast<const uint8_t*>(module);
        if (!base)
        {
            return 0;
        }
        __try
        {
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return 0;
            }
            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return 0;
            }
            if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
            {
                Log(L"WARN", L"AliceSystem3x unsupported PE machine: 0x%04X", nt->FileHeader.Machine);
                return 0;
            }
            return nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static int HexToInt(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    static std::vector<PatternByte> ParsePattern(std::string_view pattern)
    {
        std::vector<PatternByte> result;
        result.reserve((pattern.size() + 1) / 3);
        for (size_t i = 0; i + 1 < pattern.size(); ++i)
        {
            if (pattern[i] == ' ')
            {
                continue;
            }
            int high = HexToInt(pattern[i]);
            int low = HexToInt(pattern[i + 1]);
            ++i;
            uint8_t mask = 0;
            uint8_t data = 0;
            if (high != -1)
            {
                mask |= 0xF0;
                data |= static_cast<uint8_t>(high << 4);
            }
            if (low != -1)
            {
                mask |= 0x0F;
                data |= static_cast<uint8_t>(low);
            }
            result.push_back({ data, mask });
        }
        return result;
    }

    static bool PatternMatchesAt(const uint8_t* memory, size_t memorySize, size_t offset, const std::vector<PatternByte>& parsed)
    {
        if (!memory || parsed.empty() || offset > memorySize || memorySize - offset < parsed.size())
        {
            return false;
        }
        for (size_t i = 0; i < parsed.size(); ++i)
        {
            if ((memory[offset + i] & parsed[i].mask) != parsed[i].data)
            {
                return false;
            }
        }
        return true;
    }

    static bool ContainsPattern(const uint8_t* start, size_t size, std::string_view pattern)
    {
        std::vector<PatternByte> parsed = ParsePattern(pattern);
        if (parsed.empty() || !start || size < parsed.size())
        {
            return false;
        }
        const size_t end = size - parsed.size();
        for (size_t i = 0; i <= end; ++i)
        {
            if (PatternMatchesAt(start, size, i, parsed))
            {
                return true;
            }
        }
        return false;
    }

    static std::vector<uint8_t*> FindAllPatterns(uint8_t* start, size_t size, std::string_view pattern, size_t maxMatches = 64)
    {
        std::vector<uint8_t*> matches;
        std::vector<PatternByte> parsed = ParsePattern(pattern);
        if (parsed.empty() || !start || size < parsed.size())
        {
            return matches;
        }
        const size_t end = size - parsed.size();
        for (size_t i = 0; i <= end; ++i)
        {
            if (PatternMatchesAt(start, size, i, parsed))
            {
                matches.push_back(start + i);
                if (matches.size() >= maxMatches)
                {
                    break;
                }
            }
        }
        return matches;
    }

    static bool IsInModuleRange(const uint8_t* base, size_t size, const void* ptr, size_t bytes = 1)
    {
        auto* p = static_cast<const uint8_t*>(ptr);
        return base && p >= base && static_cast<size_t>(p - base) < size && bytes <= size - static_cast<size_t>(p - base);
    }

    static uintptr_t RvaFromPtr(uint8_t* base, const void* ptr)
    {
        return ptr ? static_cast<uintptr_t>(static_cast<const uint8_t*>(ptr) - base) : 0;
    }

    static uint8_t* ResolveRel32CallTarget(uint8_t* callInsn, uint8_t* moduleBase, size_t moduleSize)
    {
        if (!callInsn || callInsn[0] != 0xE8)
        {
            return nullptr;
        }
        int32_t rel = 0;
        std::memcpy(&rel, callInsn + 1, sizeof(rel));
        uint8_t* target = callInsn + 5 + rel;
        return IsInModuleRange(moduleBase, moduleSize, target, 1) ? target : nullptr;
    }

    static bool IsReadableMemory(const void* ptr, size_t size)
    {
        if (!ptr || size == 0)
        {
            return false;
        }
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
        {
            return false;
        }
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
        {
            return false;
        }
        uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t regionEnd = regionBegin + mbi.RegionSize;
        return begin >= regionBegin && size <= regionEnd - begin;
    }

    static bool IsReadableCStringA(const char* text, size_t maxLen)
    {
        if (!text || !IsReadableMemory(text, 1))
        {
            return false;
        }
        for (size_t i = 0; i < maxLen; ++i)
        {
            if (!IsReadableMemory(text + i, 1))
            {
                return false;
            }
            if (text[i] == 0)
            {
                return i > 0;
            }
        }
        return false;
    }

    static uint8_t* FindFunctionStartNear(uint8_t* moduleBase, size_t moduleSize, uint8_t* anchor)
    {
        static constexpr std::string_view kSehPrologue = "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00";
        if (!IsInModuleRange(moduleBase, moduleSize, anchor))
        {
            return nullptr;
        }
        std::vector<PatternByte> seh = ParsePattern(kSehPrologue);
        size_t anchorOffset = static_cast<size_t>(anchor - moduleBase);
        size_t maxBack = std::min<size_t>(anchorOffset, 0x140);
        for (size_t back = 0; back <= maxBack; ++back)
        {
            size_t candidate = anchorOffset - back;
            if (PatternMatchesAt(moduleBase, moduleSize, candidate, seh))
            {
                return moduleBase + candidate;
            }
        }
        return nullptr;
    }

    static int ScoreReadCandidate(uint8_t* functionStart, size_t maxWindow)
    {
        if (!functionStart) return 0;
        int score = 0;
        if (ContainsPattern(functionStart, maxWindow, "8B 47 0C 8D 34 B6 C1 E6 02")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "8A 1C 06 66 8B 4C 06 02")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "3B 77 14")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "8A 4C 06 0A")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "6A 08")) score += 1;
        if (ContainsPattern(functionStart, maxWindow, "C2 08 00")) score += 1;
        return score;
    }

    static int ScoreSizeCandidate(uint8_t* functionStart, size_t maxWindow)
    {
        if (!functionStart) return 0;
        int score = 0;
        if (ContainsPattern(functionStart, maxWindow, "8B 45 0C 8D 3C BF C1 E7 02")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "8A 4C 07 08")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "8B 44 07 04")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "3B 7D 14")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "C2 04 00")) score += 1;
        return score;
    }

    static int ScoreExistsCandidate(uint8_t* functionStart, size_t maxWindow)
    {
        if (!functionStart) return 0;
        int score = 0;
        if (ContainsPattern(functionStart, maxWindow, "8B 47 0C 8D 34 B6 C1 E6 02")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "80 3C 06 00")) score += 3;
        if (ContainsPattern(functionStart, maxWindow, "3B 77 14")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "C2 04 00")) score += 1;
        return score;
    }

    static int ScoreResizeCandidate(uint8_t* functionStart, size_t maxWindow)
    {
        if (!functionStart) return 0;
        int score = 0;
        if (ContainsPattern(functionStart, maxWindow, "56 57 8B 7C 24 0C 8B F1 85 FF")) score += 4;
        if (ContainsPattern(functionStart, maxWindow, "C7 06 00 00 00 00")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "C7 46 04 00 00 00 00")) score += 2;
        if (ContainsPattern(functionStart, maxWindow, "89 06")) score += 1;
        if (ContainsPattern(functionStart, maxWindow, "89 7E 04")) score += 1;
        if (ContainsPattern(functionStart, maxWindow, "C2 04 00")) score += 2;
        return score;
    }

    static bool PickBestCandidate(const std::vector<uint8_t*>& starts, uint8_t* moduleBase, size_t moduleSize, int(*scoreFn)(uint8_t*, size_t), int minScore, uint8_t** out, const wchar_t* label)
    {
        uint8_t* best = nullptr;
        int bestScore = 0;
        int bestCount = 0;
        for (uint8_t* start : starts)
        {
            if (!IsInModuleRange(moduleBase, moduleSize, start, 0x40))
            {
                continue;
            }
            size_t maxWindow = std::min<size_t>(0x500, moduleSize - static_cast<size_t>(start - moduleBase));
            int score = scoreFn(start, maxWindow);
            if (score > bestScore)
            {
                best = start;
                bestScore = score;
                bestCount = 1;
            }
            else if (score == bestScore && score >= minScore)
            {
                ++bestCount;
            }
        }
        if (!best || bestScore < minScore)
        {
            Log(L"WARN", L"AliceSystem3x heuristic %s not found bestScore=%d candidates=%u", label, bestScore, static_cast<unsigned>(starts.size()));
            return false;
        }
        if (bestCount > 1)
        {
            Log(L"WARN", L"AliceSystem3x heuristic %s ambiguous bestScore=%d count=%d", label, bestScore, bestCount);
            return false;
        }
        *out = best;
        Log(L"INFO", L"AliceSystem3x heuristic %s rva=%04X score=%d candidates=%u", label, static_cast<unsigned>(RvaFromPtr(moduleBase, best)), bestScore, static_cast<unsigned>(starts.size()));
        return true;
    }

    static bool ResolveReadByHeuristic(uint8_t* moduleBase, size_t moduleSize, HookTargets& out)
    {
        std::vector<uint8_t*> anchors = FindAllPatterns(moduleBase, moduleSize, "8B 47 0C 8D 34 B6 C1 E6 02 33 DB 33 C9 8A 1C 06 66 8B 4C 06 02");
        std::vector<uint8_t*> starts;
        for (uint8_t* anchor : anchors)
        {
            uint8_t* start = FindFunctionStartNear(moduleBase, moduleSize, anchor);
            if (start && std::find(starts.begin(), starts.end(), start) == starts.end())
            {
                starts.push_back(start);
            }
        }
        uint8_t* chosen = nullptr;
        if (!PickBestCandidate(starts, moduleBase, moduleSize, ScoreReadCandidate, 10, &chosen, L"read"))
        {
            return false;
        }
        out.read = reinterpret_cast<FnReadAld>(chosen);
        out.readRva = RvaFromPtr(moduleBase, chosen);
        return true;
    }

    static bool ResolveSizeByHeuristic(uint8_t* moduleBase, size_t moduleSize, HookTargets& out)
    {
        std::vector<uint8_t*> anchors = FindAllPatterns(moduleBase, moduleSize, "8B 45 0C 8D 3C BF C1 E7 02 8A 4C 07 08 84 C9 74 ?? 8B 44 07 04");
        std::vector<uint8_t*> starts;
        for (uint8_t* anchor : anchors)
        {
            uint8_t* start = FindFunctionStartNear(moduleBase, moduleSize, anchor);
            if (start && std::find(starts.begin(), starts.end(), start) == starts.end())
            {
                starts.push_back(start);
            }
        }
        uint8_t* chosen = nullptr;
        if (!PickBestCandidate(starts, moduleBase, moduleSize, ScoreSizeCandidate, 10, &chosen, L"size"))
        {
            return false;
        }
        out.getSize = reinterpret_cast<FnGetAldSize>(chosen);
        out.getSizeRva = RvaFromPtr(moduleBase, chosen);
        return true;
    }

    static bool ResolveExistsByHeuristic(uint8_t* moduleBase, size_t moduleSize, HookTargets& out)
    {
        std::vector<uint8_t*> anchors = FindAllPatterns(moduleBase, moduleSize, "8B 47 0C 8D 34 B6 C1 E6 02 80 3C 06 00");
        std::vector<uint8_t*> starts;
        for (uint8_t* anchor : anchors)
        {
            uint8_t* start = FindFunctionStartNear(moduleBase, moduleSize, anchor);
            if (start && std::find(starts.begin(), starts.end(), start) == starts.end())
            {
                starts.push_back(start);
            }
        }
        uint8_t* chosen = nullptr;
        if (!PickBestCandidate(starts, moduleBase, moduleSize, ScoreExistsCandidate, 8, &chosen, L"exists"))
        {
            return false;
        }
        out.exists = reinterpret_cast<FnExistsAld>(chosen);
        out.existsRva = RvaFromPtr(moduleBase, chosen);
        return true;
    }

    static bool ResolveResizeFromReadCalls(uint8_t* moduleBase, size_t moduleSize, HookTargets& out)
    {
        uint8_t* read = reinterpret_cast<uint8_t*>(out.read);
        if (!IsInModuleRange(moduleBase, moduleSize, read, 0x40))
        {
            return false;
        }
        size_t readOffset = static_cast<size_t>(read - moduleBase);
        size_t window = std::min<size_t>(0x500, moduleSize - readOffset);
        std::vector<uint8_t*> callTargets;
        for (size_t i = 0; i + 5 <= window; ++i)
        {
            if (read[i] != 0xE8)
            {
                continue;
            }
            uint8_t* target = ResolveRel32CallTarget(read + i, moduleBase, moduleSize);
            if (target && std::find(callTargets.begin(), callTargets.end(), target) == callTargets.end())
            {
                callTargets.push_back(target);
            }
        }
        uint8_t* chosen = nullptr;
        if (!PickBestCandidate(callTargets, moduleBase, moduleSize, ScoreResizeCandidate, 10, &chosen, L"resize"))
        {
            return false;
        }
        out.resize = reinterpret_cast<FnResizeBuffer>(chosen);
        out.resizeRva = RvaFromPtr(moduleBase, chosen);
        return true;
    }

    static bool ResolveByHeuristics(HMODULE mainModule, HookTargets& out)
    {
        auto* base = reinterpret_cast<uint8_t*>(mainModule);
        size_t size = GetMainModuleSize(mainModule);
        if (!base || size == 0)
        {
            return false;
        }

        HookTargets tmp;
        if (!ResolveReadByHeuristic(base, size, tmp)) return false;
        if (!ResolveSizeByHeuristic(base, size, tmp)) return false;
        if (!ResolveResizeFromReadCalls(base, size, tmp)) return false;
        if (sg_settings.hookExistsCheck && !ResolveExistsByHeuristic(base, size, tmp)) return false;
        tmp.source = ResolveSource::Heuristic;
        out = tmp;
        return true;
    }

    static bool ResolveHookTargets(HMODULE mainModule, HookTargets& out)
    {
        if (ResolveByHeuristics(mainModule, out))
        {
            return true;
        }
        Log(L"ERROR", L"AliceSystem3x heuristic resolver failed");
        return false;
    }
    struct ScopedReentry
    {
        bool active = false;
        ScopedReentry()
        {
            if (!g_inHook)
            {
                g_inHook = true;
                active = true;
            }
        }
        ~ScopedReentry()
        {
            if (active)
            {
                g_inHook = false;
            }
        }
    };

    struct LooseFile
    {
        std::wstring path;
        DWORD size = 0;
        std::shared_ptr<const std::vector<uint8_t>> data;
    };

    struct AldLocation
    {
        const char* archivePath = nullptr;
        unsigned short entryNo = 0;
    };

    static std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
    {
        if (left.empty()) return right;
        if (right.empty()) return left;
        if (left.back() == L'\\' || left.back() == L'/') return left + right;
        return left + L"\\" + right;
    }

    static std::wstring FormatId(unsigned int id, int width, bool hex)
    {
        wchar_t buf[32] = {};
        if (hex)
        {
            if (width > 0)
            {
                StringCchPrintfW(buf, 32, L"%0*X", width, id);
            }
            else
            {
                StringCchPrintfW(buf, 32, L"%X", id);
            }
        }
        else
        {
            if (width > 0)
            {
                StringCchPrintfW(buf, 32, L"%0*u", width, id);
            }
            else
            {
                StringCchPrintfW(buf, 32, L"%u", id);
            }
        }
        return buf;
    }

    static bool TryConsumeIdToken(const std::wstring& pattern, size_t& i, unsigned int id, std::wstring& out)
    {
        const size_t start = i;
        if (pattern[i] != L'%')
        {
            return false;
        }
        ++i;

        int width = 0;
        if (i < pattern.size() && pattern[i] == L'0')
        {
            ++i;
            while (i < pattern.size() && pattern[i] >= L'0' && pattern[i] <= L'9')
            {
                width = width * 10 + (pattern[i] - L'0');
                ++i;
            }
        }

        if (i < pattern.size() && (pattern[i] == L'X' || pattern[i] == L'x'))
        {
            out += FormatId(id, width, true);
            ++i;
            return true;
        }
        if (i < pattern.size() && (pattern[i] == L'D' || pattern[i] == L'd' || pattern[i] == L'u'))
        {
            out += FormatId(id, width, false);
            ++i;
            return true;
        }

        i = start;
        return false;
    }

    static std::wstring ApplyPattern(const std::wstring& pattern, wchar_t kind, unsigned int id)
    {
        std::wstring out;
        out.reserve(pattern.size() + 16);
        for (size_t i = 0; i < pattern.size();)
        {
            if (pattern[i] == L'%')
            {
                if (i + 1 < pattern.size() && pattern[i + 1] == L'K')
                {
                    out.push_back(kind);
                    i += 2;
                    continue;
                }
                size_t saved = i;
                if (TryConsumeIdToken(pattern, i, id, out))
                {
                    continue;
                }
                i = saved;
            }
            out.push_back(pattern[i]);
            ++i;
        }
        return out;
    }

    static bool IsRegularFile(const std::wstring& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    static bool TryGetFileSize(const std::wstring& path, DWORD& outSize)
    {
        outSize = 0;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        LARGE_INTEGER size = {};
        bool ok = GetFileSizeEx(file, &size) != FALSE;
        CloseHandle(file);
        if (!ok || size.QuadPart <= 0 || size.QuadPart > 0x7fffffff)
        {
            return false;
        }
        if (static_cast<unsigned long long>(size.QuadPart) > sg_settings.maxFileSize)
        {
            if (sg_settings.enableLog)
            {
                Log(L"WARN", L"Loose file too large, fallback: %s size=%lld max=%u", path.c_str(), size.QuadPart, sg_settings.maxFileSize);
            }
            return false;
        }
        outSize = static_cast<DWORD>(size.QuadPart);
        return true;
    }

    static bool ReadWholeFile(const std::wstring& path, void* dst, DWORD size)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD readBytes = 0;
        BOOL ok = ReadFile(file, dst, size, &readBytes, nullptr);
        CloseHandle(file);
        return ok && readBytes == size;
    }

    static DWORD DecodeAldOffset3(const unsigned char bytes[3])
    {
        return static_cast<DWORD>((bytes[0] | (bytes[1] << 8) | (bytes[2] << 16)) << kAldOffsetShift);
    }

    static bool ReadAtA(const char* path, DWORD offset, void* dst, DWORD size)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        bool ok = false;
        if (SetFilePointer(file, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR)
        {
            DWORD readBytes = 0;
            ok = ReadFile(file, dst, size, &readBytes, nullptr) && readBytes == size;
        }
        CloseHandle(file);
        return ok;
    }

    static char GetKindFromArchivePath(const char* pathA)
    {
        if (!pathA || !*pathA)
        {
            return 0;
        }
        const char* dot = strrchr(pathA, '.');
        if (!dot || _stricmp(dot, ".ALD") != 0 || dot - pathA < 2)
        {
            return 0;
        }
        unsigned char kind = static_cast<unsigned char>(dot[-2]);
        if (!std::isalnum(kind))
        {
            return 0;
        }
        return static_cast<char>(std::toupper(kind));
    }

    static bool GetAldLocationRaw(void* self, int logicalId, AldLocation* out)
    {
        __try
        {
            if (!self || !out || logicalId <= 0)
            {
                return false;
            }
            uintptr_t group = reinterpret_cast<uintptr_t>(self);
            if (!IsReadableMemory(reinterpret_cast<void*>(group), kAldGroupFileCountOffset + sizeof(int)))
            {
                return false;
            }
            uintptr_t archives = *reinterpret_cast<uintptr_t*>(group + kAldGroupArchivesOffset);
            uintptr_t files = *reinterpret_cast<uintptr_t*>(group + kAldGroupResourcesOffset);
            int fileCount = *reinterpret_cast<int*>(group + kAldGroupFileCountOffset);
            if (!archives || !files || fileCount <= 0 || fileCount > 1000000 || logicalId >= fileCount)
            {
                return false;
            }

            uintptr_t fileEntry = files + kResourceRecordStride * logicalId;
            if (!IsReadableMemory(reinterpret_cast<void*>(fileEntry), kResourceEntryNoOffset + sizeof(unsigned short)))
            {
                return false;
            }
            unsigned char archiveNo = *reinterpret_cast<unsigned char*>(fileEntry + kResourceArchiveNoOffset);
            unsigned short entryNo = *reinterpret_cast<unsigned short*>(fileEntry + kResourceEntryNoOffset);
            if (!archiveNo || !entryNo)
            {
                return false;
            }

            uintptr_t archiveEntry = archives + kArchiveEntryStride * archiveNo;
            if (!IsReadableMemory(reinterpret_cast<void*>(archiveEntry), kArchivePathOffset + sizeof(const char*)))
            {
                return false;
            }
            const char* archivePath = *reinterpret_cast<const char**>(archiveEntry + kArchivePathOffset);
            if (!IsReadableCStringA(archivePath, MAX_PATH) || !GetKindFromArchivePath(archivePath))
            {
                return false;
            }

            out->archivePath = archivePath;
            out->entryNo = entryNo;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static char GetKindFromAldGroup(void* self)
    {
        AldLocation loc;
        if (GetAldLocationRaw(self, 1, &loc))
        {
            return GetKindFromArchivePath(loc.archivePath);
        }

        __try
        {
            if (!self)
            {
                return 0;
            }
            uintptr_t group = reinterpret_cast<uintptr_t>(self);
            if (!IsReadableMemory(reinterpret_cast<void*>(group), kAldGroupArchivesOffset + sizeof(uintptr_t)))
            {
                return 0;
            }
            uintptr_t archives = *reinterpret_cast<uintptr_t*>(group + kAldGroupArchivesOffset);
            if (!archives)
            {
                return 0;
            }
            uintptr_t archiveEntry = archives + kArchiveEntryStride;
            if (!IsReadableMemory(reinterpret_cast<void*>(archiveEntry), kArchivePathOffset + sizeof(const char*)))
            {
                return 0;
            }
            const char* path = *reinterpret_cast<const char**>(archiveEntry + kArchivePathOffset);
            return IsReadableCStringA(path, MAX_PATH) ? GetKindFromArchivePath(path) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static void AddCandidate(std::vector<std::wstring>& candidates, const std::wstring& name)
    {
        if (name.empty())
        {
            return;
        }
        for (const std::wstring& existing : candidates)
        {
            if (_wcsicmp(existing.c_str(), name.c_str()) == 0)
            {
                return;
            }
        }
        candidates.push_back(name);
    }

    static std::wstring AnsiToWide(const char* text)
    {
        if (!text || !*text)
        {
            return L"";
        }
        int len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (len <= 1)
        {
            return L"";
        }
        std::wstring result(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), len);
        result.resize(static_cast<size_t>(len - 1));
        return result;
    }

    static bool IsSafeFlatName(const std::wstring& name)
    {
        if (name.empty() || name.size() > 64)
        {
            return false;
        }
        for (wchar_t ch : name)
        {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
            {
                return false;
            }
        }
        return name.find(L'.') != std::wstring::npos;
    }

    static std::wstring ChangeExtension(const std::wstring& name, const wchar_t* newExt)
    {
        size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos)
        {
            return L"";
        }
        return name.substr(0, dot) + newExt;
    }

    static std::wstring ExtractOriginalNameFromAld(void* self, int logicalId)
    {
        AldLocation loc;
        if (!GetAldLocationRaw(self, logicalId, &loc))
        {
            return L"";
        }

        unsigned char off3[kAldOffsetEntrySize] = {};
        if (!ReadAtA(loc.archivePath, static_cast<DWORD>(loc.entryNo) * kAldOffsetEntrySize, off3, kAldOffsetEntrySize))
        {
            return L"";
        }
        DWORD entryOffset = DecodeAldOffset3(off3);
        if (!entryOffset)
        {
            return L"";
        }

        char nameBytes[kAldPayloadNameMax + 1] = {};
        // Current System39 ALD entries store the original payload filename at entryOffset + 0x10.
        if (!ReadAtA(loc.archivePath, entryOffset + kAldPayloadNameOffset, nameBytes, kAldPayloadNameMax))
        {
            return L"";
        }
        nameBytes[kAldPayloadNameMax] = 0;
        for (char& ch : nameBytes)
        {
            unsigned char c = static_cast<unsigned char>(ch);
            if (c == 0)
            {
                break;
            }
            if (c < 0x20)
            {
                ch = 0;
                break;
            }
        }

        std::wstring name = AnsiToWide(nameBytes);
        return IsSafeFlatName(name) ? name : L"";
    }

    static bool HasExtension(const std::wstring& name, const wchar_t* ext)
    {
        size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos)
        {
            return false;
        }
        return _wcsicmp(name.c_str() + dot, ext) == 0;
    }

    static void AddOriginalNameCandidates(std::vector<std::wstring>& candidates, const std::wstring& originalName)
    {
        if (!IsSafeFlatName(originalName))
        {
            return;
        }

        // First try the exact embedded name from the ALD entry. This covers all resource
        // types that carry a filename in their payload header: SCO/QNT/WAV/OGG/etc.
        AddCandidate(candidates, originalName);

        // Convenience aliases for CG entries: allow patch\CG0001.BMP to override an
        // original CG0001.QNT/AJP without requiring users to convert it back to QNT/AJP.
        if (HasExtension(originalName, L".QNT") || HasExtension(originalName, L".AJP") || HasExtension(originalName, L".BMP"))
        {
            AddCandidate(candidates, ChangeExtension(originalName, L".BMP"));
            AddCandidate(candidates, ChangeExtension(originalName, L".QNT"));
            AddCandidate(candidates, ChangeExtension(originalName, L".AJP"));
        }
    }

    static std::vector<std::wstring> BuildSameNameCandidates(wchar_t kind, unsigned int logicalId)
    {
        std::vector<std::wstring> candidates;
        const std::wstring dec4 = FormatId(logicalId, 4, false);
        const std::wstring dec6 = FormatId(logicalId, 6, false);
        const std::wstring hex4 = FormatId(logicalId, 4, true);
        const std::wstring hex6 = FormatId(logicalId, 6, true);

        switch (towupper(kind))
        {
        case L'G':
            // System 3.x CG resources are conventionally named CG0001.QNT.
            // For patching convenience, prefer same stem BMP first, then the native QNT/AJP names.
            AddCandidate(candidates, L"CG" + dec4 + L".BMP");
            AddCandidate(candidates, L"CG" + dec4 + L".QNT");
            AddCandidate(candidates, L"CG" + dec4 + L".AJP");
            AddCandidate(candidates, L"CG" + hex4 + L".BMP");
            AddCandidate(candidates, L"CG" + hex4 + L".QNT");
            AddCandidate(candidates, L"CG" + hex4 + L".AJP");
            AddCandidate(candidates, L"G" + dec6 + L".bin");
            AddCandidate(candidates, L"G" + hex6 + L".bin");
            break;
        case L'S':
            AddCandidate(candidates, L"S" + dec6 + L".bin");
            AddCandidate(candidates, L"S" + hex6 + L".bin");
            break;
        case L'W':
            AddCandidate(candidates, L"WAVE" + dec4 + L".WAV");
            AddCandidate(candidates, L"WAVE" + hex4 + L".WAV");
            AddCandidate(candidates, L"W" + dec6 + L".bin");
            AddCandidate(candidates, L"W" + hex6 + L".bin");
            break;
        case L'M':
            AddCandidate(candidates, L"MIDI" + dec4 + L".MID");
            AddCandidate(candidates, L"MIDI" + hex4 + L".MID");
            AddCandidate(candidates, L"M" + dec6 + L".bin");
            AddCandidate(candidates, L"M" + hex6 + L".bin");
            break;
        case L'D':
            AddCandidate(candidates, L"DATA" + dec4 + L".bin");
            AddCandidate(candidates, L"DATA" + hex4 + L".bin");
            AddCandidate(candidates, L"D" + dec6 + L".bin");
            AddCandidate(candidates, L"D" + hex6 + L".bin");
            break;
        case L'R':
            AddCandidate(candidates, L"RES" + dec4 + L".bin");
            AddCandidate(candidates, L"RES" + hex4 + L".bin");
            AddCandidate(candidates, L"R" + dec6 + L".bin");
            AddCandidate(candidates, L"R" + hex6 + L".bin");
            break;
        default:
            AddCandidate(candidates, std::wstring(1, kind) + dec6 + L".bin");
            AddCandidate(candidates, std::wstring(1, kind) + hex6 + L".bin");
            break;
        }
        return candidates;
    }

    static bool FindLooseFile(void* self, int logicalId, LooseFile& out)
    {
        out = LooseFile{};
        if (logicalId <= 0)
        {
            return false;
        }

        char kindA = GetKindFromAldGroup(self);
        if (!kindA)
        {
            return false;
        }
        wchar_t kind = static_cast<wchar_t>(kindA);
        std::vector<std::wstring> candidates;
        AddOriginalNameCandidates(candidates, ExtractOriginalNameFromAld(self, logicalId));
        std::vector<std::wstring> fallbackCandidates = BuildSameNameCandidates(kind, static_cast<unsigned int>(logicalId));
        for (const std::wstring& fallback : fallbackCandidates)
        {
            AddCandidate(candidates, fallback);
        }

        std::wstring matchedName;
        if (TryFindLooseCandidate(candidates, out.path, out.size, out.data, &matchedName))
        {
            if (sg_settings.enableLog)
            {
                Log(L"INFO", L"candidate hit kind=%c idDec=%d idHex=%04X name=%s", kind, logicalId, logicalId, matchedName.c_str());
            }
            return true;
        }

        if (sg_settings.enableLog && towupper(kind) == L'G' && InterlockedIncrement(&g_missLogCount) <= 1000)
        {
            Log(L"DEBUG", L"miss kind=%c idDec=%d idHex=%04X triedFirst=%s",
                kind,
                logicalId,
                logicalId,
                candidates.empty() ? L"" : candidates.front().c_str());
        }
        return false;
    }

    static int __fastcall HookGetAldSize(void* self, void*, int logicalId)
    {
        if (g_inHook || !RealGetAldSize)
        {
            return RealGetAldSize ? RealGetAldSize(self, logicalId) : 0;
        }
        ScopedReentry guard;
        if (!guard.active)
        {
            return RealGetAldSize(self, logicalId);
        }

        LooseFile loose;
        if (FindLooseFile(self, logicalId, loose))
        {
            if (sg_settings.enableLog)
            {
                Log(L"INFO", L"size hit id=%04X size=%u file=%s", logicalId, loose.size, loose.path.c_str());
            }
            return static_cast<int>(loose.size);
        }
        return RealGetAldSize(self, logicalId);
    }

    static bool __fastcall HookReadAld(void* self, void*, int logicalId, void* outBuffer)
    {
        if (g_inHook || !RealReadAld)
        {
            return RealReadAld ? RealReadAld(self, logicalId, outBuffer) : false;
        }
        ScopedReentry guard;
        if (!guard.active)
        {
            return RealReadAld(self, logicalId, outBuffer);
        }

        LooseFile loose;
        if (!FindLooseFile(self, logicalId, loose))
        {
            return RealReadAld(self, logicalId, outBuffer);
        }

        if (!EngineResizeBuffer || !outBuffer)
        {
            Log(L"ERROR", L"read hit but buffer allocator missing id=%04X file=%s", logicalId, loose.path.c_str());
            return RealReadAld(self, logicalId, outBuffer);
        }

        if (!EngineResizeBuffer(outBuffer, loose.size))
        {
            Log(L"ERROR", L"EngineResizeBuffer failed id=%04X size=%u file=%s", logicalId, loose.size, loose.path.c_str());
            return false;
        }

        void* dst = *reinterpret_cast<void**>(outBuffer);

        if (!dst)
        {
            Log(L"ERROR", L"read failed id=%04X size=%u file=%s empty buffer", logicalId, loose.size, loose.path.c_str());
            return false;
        }
        if (loose.data)
        {
            if (loose.data->size() != loose.size)
            {
                Log(L"ERROR", L"read failed id=%04X size mismatch file=%s expected=%u actual=%u", logicalId, loose.path.c_str(), loose.size, static_cast<unsigned>(loose.data->size()));
                return false;
            }
            std::memcpy(dst, loose.data->data(), loose.size);
        }
        else if (!ReadWholeFile(loose.path, dst, loose.size))
        {
            Log(L"ERROR", L"read failed id=%04X size=%u file=%s gle=%lu", logicalId, loose.size, loose.path.c_str(), GetLastError());
            return false;
        }

        if (sg_settings.enableLog)
        {
            Log(L"INFO", L"read hit id=%04X size=%u file=%s", logicalId, loose.size, loose.path.c_str());
        }
        return true;
    }

    static bool __fastcall HookExistsAld(void* self, void*, int logicalId)
    {
        if (g_inHook || !RealExistsAld)
        {
            return RealExistsAld ? RealExistsAld(self, logicalId) : false;
        }
        ScopedReentry guard;
        if (!guard.active)
        {
            return RealExistsAld(self, logicalId);
        }

        LooseFile loose;
        if (FindLooseFile(self, logicalId, loose))
        {
            if (sg_settings.enableLog)
            {
                Log(L"INFO", L"exists hit id=%04X file=%s", logicalId, loose.path.c_str());
            }
            return true;
        }
        return RealExistsAld(self, logicalId);
    }

    static bool IsSystem39Host()
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const wchar_t* name = wcsrchr(exePath, L'\\');
        name = name ? name + 1 : exePath;

        // Patched/transcoded builds are often renamed, e.g. System39_chs.exe.
        // The resolver still validates the actual ALD loader signatures before hooking.
        if (_wcsnicmp(name, L"System39", 8) != 0)
        {
            return false;
        }
        const wchar_t* dot = wcsrchr(name, L'.');
        return dot && _wcsicmp(dot, L".exe") == 0;
    }

    static bool InstallAliceSystem3xHooks()
    {
        if (g_installed)
        {
            Log(L"INFO", L"AliceSystem3x hooks already installed");
            return true;
        }
        if (!sg_settings.enable)
        {
            Log(L"INFO", L"AliceSystem3x disabled by config");
            return false;
        }
        // Do not gate by executable name. Some compatible builds are renamed
        // (System39_chs.exe), and unsupported builds are filtered by PE/signature checks below.
        HMODULE mainModule = GetModuleHandleW(nullptr);
        if (!mainModule)
        {
            Log(L"ERROR", L"GetModuleHandleW(nullptr) failed");
            return false;
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(mainModule);
        HookTargets targets;
        if (!ResolveHookTargets(mainModule, targets) || !targets.getSize || !targets.read || !targets.resize)
        {
            Log(L"ERROR", L"AliceSystem3x skipped: could not resolve required System39 ALD targets");
            return false;
        }
        RealGetAldSize = targets.getSize;
        RealReadAld = targets.read;
        RealExistsAld = targets.exists;
        EngineResizeBuffer = targets.resize;

        Log(L"INFO", L"AliceSystem3x resolver source=%s base=%p getSizeRva=%04X readRva=%04X existsRva=%04X resizeRva=%04X",
            ResolveSourceName(targets.source),
            reinterpret_cast<void*>(base),
            static_cast<unsigned>(targets.getSizeRva),
            static_cast<unsigned>(targets.readRva),
            static_cast<unsigned>(targets.existsRva),
            static_cast<unsigned>(targets.resizeRva));

        LONG error = DetourTransactionBegin();
        if (error != NO_ERROR)
        {
            Log(L"ERROR", L"DetourTransactionBegin failed: %ld", error);
            RealGetAldSize = nullptr;
            RealReadAld = nullptr;
            RealExistsAld = nullptr;
            EngineResizeBuffer = nullptr;
            return false;
        }
        DetourUpdateThread(GetCurrentThread());

        bool ok = true;
        error = DetourAttach(reinterpret_cast<PVOID*>(&RealGetAldSize), reinterpret_cast<PVOID>(HookGetAldSize));
        if (error != NO_ERROR)
        {
            ok = false;
            Log(L"ERROR", L"DetourAttach GetAldSize failed: %ld", error);
        }
        error = DetourAttach(reinterpret_cast<PVOID*>(&RealReadAld), reinterpret_cast<PVOID>(HookReadAld));
        if (error != NO_ERROR)
        {
            ok = false;
            Log(L"ERROR", L"DetourAttach ReadAld failed: %ld", error);
        }
        if (sg_settings.hookExistsCheck)
        {
            error = DetourAttach(reinterpret_cast<PVOID*>(&RealExistsAld), reinterpret_cast<PVOID>(HookExistsAld));
            if (error != NO_ERROR)
            {
                ok = false;
                Log(L"ERROR", L"DetourAttach ExistsAld failed: %ld", error);
            }
        }

        if (!ok)
        {
            DetourTransactionAbort();
            RealGetAldSize = nullptr;
            RealReadAld = nullptr;
            RealExistsAld = nullptr;
            EngineResizeBuffer = nullptr;
            Log(L"ERROR", L"AliceSystem3x hook transaction aborted");
            return false;
        }

        error = DetourTransactionCommit();
        if (error != NO_ERROR)
        {
            ok = false;
            RealGetAldSize = nullptr;
            RealReadAld = nullptr;
            RealExistsAld = nullptr;
            EngineResizeBuffer = nullptr;
            Log(L"ERROR", L"DetourTransactionCommit failed: %ld", error);
        }

        g_installed = ok;
        Log(ok ? L"INFO" : L"ERROR",
            L"AliceSystem3x hook summary base=%p source=%s getSize=%p read=%p exists=%p resize=%p hookExists=%d result=%s",
            reinterpret_cast<void*>(base),
            ResolveSourceName(targets.source),
            reinterpret_cast<void*>(targets.getSize),
            reinterpret_cast<void*>(targets.read),
            reinterpret_cast<void*>(targets.exists),
            reinterpret_cast<void*>(targets.resize),
            sg_settings.hookExistsCheck ? 1 : 0,
            ok ? L"success" : L"failed");
        return ok;
    }
#elif defined(_M_X64)
    static constexpr int kX35AldOffsetEntrySize = 3;
    static constexpr int kX35AldPayloadNameOffset = 0x10;
    static constexpr int kX35AldPayloadNameMax = 32;
    static constexpr int kX35ManagerPathBase = 0x800;
    static constexpr int kX35ManagerCountOffset = 0xFF8;
    static constexpr int kX35ManagerDirectoryOffset = 0x1000;
    static constexpr int kX35ManagerOffsetsOffset = 0x1008;
    static constexpr uint32_t kX35LooseMagic = 0x4C414C44; // DLAL / Alice System3.x ALD

    using FnX35OpenResource = void* (*)(unsigned int type, int index);
    using FnX35ReleaseResource = void (*)(void* resource);

    struct X35Resource
    {
        uint32_t payloadSize;
        uint32_t pad04;
        uint8_t* recordBase;
        uint8_t* payload;
        uint8_t* headerExtra;
        int32_t refCount;
        uint32_t pad24;
        void* owner;
    };

    struct X35LooseOwner
    {
        uint8_t mappedMode = 1;
        uint8_t pad[3] = {};
        uint32_t magic = kX35LooseMagic;
    };

    struct PatternByte
    {
        uint8_t data = 0;
        uint8_t mask = 0;
    };

    struct X35Targets
    {
        FnX35OpenResource open = nullptr;
        FnX35ReleaseResource release = nullptr;
        void** managers = nullptr;
        uintptr_t openRva = 0;
        uintptr_t releaseRva = 0;
        uintptr_t managersRva = 0;
        const wchar_t* source = L"None";
    };

    struct X35LooseFile
    {
        std::wstring path;
        DWORD size = 0;
        std::shared_ptr<const std::vector<uint8_t>> data;
    };

    static FnX35OpenResource RealX35OpenResource = nullptr;
    static FnX35ReleaseResource RealX35ReleaseResource = nullptr;
    static void** g_x35Managers = nullptr;
    static X35LooseOwner g_x35LooseOwner;
    static bool g_x35Installed = false;
    static thread_local bool g_x35InHook = false;

    struct ScopedX35Reentry
    {
        bool active = false;
        ScopedX35Reentry()
        {
            if (!g_x35InHook)
            {
                g_x35InHook = true;
                active = true;
            }
        }
        ~ScopedX35Reentry()
        {
            if (active)
            {
                g_x35InHook = false;
            }
        }
    };

    static std::wstring X35JoinPath(const std::wstring& left, const std::wstring& right)
    {
        if (left.empty()) return right;
        if (right.empty()) return left;
        if (left.back() == L'\\' || left.back() == L'/') return left + right;
        return left + L"\\" + right;
    }

    static std::wstring X35FormatId(unsigned int id, int width)
    {
        wchar_t buf[32] = {};
        StringCchPrintfW(buf, 32, L"%0*u", width, id);
        return buf;
    }

    static bool X35IsRegularFile(const std::wstring& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    static bool X35TryGetFileSize(const std::wstring& path, DWORD& outSize)
    {
        outSize = 0;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size = {};
        bool ok = GetFileSizeEx(file, &size) != FALSE;
        CloseHandle(file);
        if (!ok || size.QuadPart <= 0 || size.QuadPart > 0x7fffffff) return false;
        if (static_cast<unsigned long long>(size.QuadPart) > sg_settings.maxFileSize)
        {
            Log(L"WARN", L"X35 loose file too large, fallback: %s size=%lld max=%u", path.c_str(), size.QuadPart, sg_settings.maxFileSize);
            return false;
        }
        outSize = static_cast<DWORD>(size.QuadPart);
        return true;
    }

    static bool X35ReadWholeFile(const std::wstring& path, void* dst, DWORD size)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD readBytes = 0;
        BOOL ok = ReadFile(file, dst, size, &readBytes, nullptr);
        CloseHandle(file);
        return ok && readBytes == size;
    }

    static bool X35ReadAtA(const char* path, uint32_t offset, void* dst, DWORD size)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li = {};
        li.QuadPart = offset;
        bool ok = false;
        if (SetFilePointerEx(file, li, nullptr, FILE_BEGIN))
        {
            DWORD readBytes = 0;
            ok = ReadFile(file, dst, size, &readBytes, nullptr) && readBytes == size;
        }
        CloseHandle(file);
        return ok;
    }

    static bool X35IsSafeFlatName(const std::wstring& name)
    {
        if (name.empty() || name.size() > 64) return false;
        for (wchar_t ch : name)
        {
            if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') return false;
        }
        return name.find(L'.') != std::wstring::npos;
    }

    static std::wstring X35AnsiToWide(const char* text)
    {
        if (!text || !*text) return L"";
        int len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (len <= 1) return L"";
        std::wstring result(static_cast<size_t>(len), L'\0');
        MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), len);
        result.resize(static_cast<size_t>(len - 1));
        return result;
    }

    static bool X35HasExtension(const std::wstring& name, const wchar_t* ext)
    {
        size_t dot = name.find_last_of(L'.');
        return dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, ext) == 0;
    }

    static std::wstring X35ChangeExtension(const std::wstring& name, const wchar_t* ext)
    {
        size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos ? L"" : name.substr(0, dot) + ext;
    }

    static void X35AddCandidate(std::vector<std::wstring>& candidates, const std::wstring& name)
    {
        if (name.empty()) return;
        for (const auto& existing : candidates)
        {
            if (_wcsicmp(existing.c_str(), name.c_str()) == 0) return;
        }
        candidates.push_back(name);
    }

    static void X35AddOriginalNameCandidates(std::vector<std::wstring>& candidates, const std::wstring& originalName)
    {
        if (!X35IsSafeFlatName(originalName)) return;
        X35AddCandidate(candidates, originalName);
        if (X35HasExtension(originalName, L".QNT") || X35HasExtension(originalName, L".AJP") || X35HasExtension(originalName, L".BMP"))
        {
            X35AddCandidate(candidates, X35ChangeExtension(originalName, L".BMP"));
            X35AddCandidate(candidates, X35ChangeExtension(originalName, L".QNT"));
            X35AddCandidate(candidates, X35ChangeExtension(originalName, L".AJP"));
        }
    }

    static bool X35IsReadableMemory(const void* ptr, size_t size)
    {
        if (!ptr || size == 0) return false;
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(ptr, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t regionBegin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t regionEnd = regionBegin + mbi.RegionSize;
        return begin >= regionBegin && size <= regionEnd - begin;
    }

    static bool X35IsReadableCStringA(const char* text, size_t maxLen)
    {
        if (!text || !X35IsReadableMemory(text, 1)) return false;
        for (size_t i = 0; i < maxLen; ++i)
        {
            if (!X35IsReadableMemory(text + i, 1)) return false;
            if (text[i] == 0) return i > 0;
        }
        return false;
    }

    static std::wstring X35ExtractOriginalName(unsigned int type, int index)
    {
        if (!g_x35Managers || type > 6 || index < 0 || !X35IsReadableMemory(g_x35Managers, sizeof(void*) * 7)) return L"";
        auto* manager = static_cast<uint8_t*>(g_x35Managers[type]);
        if (!X35IsReadableMemory(manager, kX35ManagerOffsetsOffset + sizeof(void*))) return L"";
        int count = *reinterpret_cast<int*>(manager + kX35ManagerCountOffset);
        if (count <= 0 || count > 1000000 || index >= count) return L"";
        auto* directory = *reinterpret_cast<uint8_t**>(manager + kX35ManagerDirectoryOffset);
        auto* offsets = *reinterpret_cast<uint32_t**>(manager + kX35ManagerOffsetsOffset);
        if (!X35IsReadableMemory(directory, static_cast<size_t>(index + 1) * kX35AldOffsetEntrySize) || !X35IsReadableMemory(offsets, static_cast<size_t>(index + 1) * sizeof(uint32_t))) return L"";
        uint8_t diskNo = directory[kX35AldOffsetEntrySize * index];
        uint32_t entryOffset = offsets[index];
        if (!diskNo || !entryOffset) return L"";
        const char* archivePath = *reinterpret_cast<const char**>(manager + kX35ManagerPathBase + sizeof(char*) * static_cast<size_t>(diskNo - 1));
        if (!X35IsReadableCStringA(archivePath, MAX_PATH)) return L"";
        char nameBytes[kX35AldPayloadNameMax + 1] = {};
        if (!X35ReadAtA(archivePath, entryOffset + kX35AldPayloadNameOffset, nameBytes, kX35AldPayloadNameMax)) return L"";
        nameBytes[kX35AldPayloadNameMax] = 0;
        for (char& ch : nameBytes)
        {
            unsigned char c = static_cast<unsigned char>(ch);
            if (c == 0) break;
            if (c < 0x20)
            {
                ch = 0;
                break;
            }
        }
        std::wstring name = X35AnsiToWide(nameBytes);
        return X35IsSafeFlatName(name) ? name : L"";
    }

    static std::vector<std::wstring> X35BuildFallbackCandidates(unsigned int type, int index)
    {
        std::vector<std::wstring> candidates;
        unsigned int id = static_cast<unsigned int>(index + 1);
        const std::wstring dec4 = X35FormatId(id, 4);
        const std::wstring dec6 = X35FormatId(id, 6);
        switch (type)
        {
        case 1:
            X35AddCandidate(candidates, L"CG" + dec4 + L".BMP");
            X35AddCandidate(candidates, L"CG" + dec4 + L".QNT");
            X35AddCandidate(candidates, L"CG" + dec4 + L".AJP");
            break;
        case 0:
            X35AddCandidate(candidates, L"S" + dec6 + L".bin");
            break;
        case 2:
            X35AddCandidate(candidates, L"WAVE" + dec4 + L".WAV");
            X35AddCandidate(candidates, L"W" + dec6 + L".bin");
            break;
        case 3:
            X35AddCandidate(candidates, L"MIDI" + dec4 + L".MID");
            X35AddCandidate(candidates, L"M" + dec6 + L".bin");
            break;
        case 4:
            X35AddCandidate(candidates, L"DATA" + dec4 + L".bin");
            X35AddCandidate(candidates, L"D" + dec6 + L".bin");
            break;
        case 5:
            X35AddCandidate(candidates, L"RES" + dec4 + L".bin");
            X35AddCandidate(candidates, L"R" + dec6 + L".bin");
            break;
        default:
            X35AddCandidate(candidates, L"B" + dec6 + L".bin");
            break;
        }
        return candidates;
    }

    static bool X35FindLooseFile(unsigned int type, int index, X35LooseFile& out)
    {
        out = X35LooseFile{};
        if (type > 6 || index < 0) return false;
        std::vector<std::wstring> candidates;
        X35AddOriginalNameCandidates(candidates, X35ExtractOriginalName(type, index));
        for (const auto& fallback : X35BuildFallbackCandidates(type, index)) X35AddCandidate(candidates, fallback);
        std::wstring matchedName;
        if (TryFindLooseCandidate(candidates, out.path, out.size, out.data, &matchedName))
        {
            if (sg_settings.enableLog)
            {
                Log(L"INFO", L"x35 candidate hit type=%u index=%d id=%d name=%s", type, index, index + 1, matchedName.c_str());
            }
            return true;
        }
        return false;
    }

    static X35Resource* X35MakeLooseResource(const X35LooseFile& loose)
    {
        if (!loose.size) return nullptr;
        size_t recordSize = 0x10ull + loose.size;
        if (recordSize > 0x7fffffff) return nullptr;
        auto* record = static_cast<uint8_t*>(std::malloc(recordSize));
        if (!record) return nullptr;
        *reinterpret_cast<uint32_t*>(record + 0) = 0x10;
        *reinterpret_cast<uint32_t*>(record + 4) = loose.size;
        std::memset(record + 8, 0, 8);
        if (loose.data)
        {
            if (loose.data->size() != loose.size)
            {
                std::free(record);
                return nullptr;
            }
            std::memcpy(record + 0x10, loose.data->data(), loose.size);
        }
        else if (!X35ReadWholeFile(loose.path, record + 0x10, loose.size))
        {
            std::free(record);
            return nullptr;
        }
        auto* res = static_cast<X35Resource*>(std::calloc(1, sizeof(X35Resource)));
        if (!res)
        {
            std::free(record);
            return nullptr;
        }
        res->payloadSize = loose.size;
        res->recordBase = record;
        res->payload = record + 0x10;
        res->headerExtra = record + 0x10;
        res->refCount = 1;
        res->owner = &g_x35LooseOwner;
        return res;
    }

    static bool X35IsLooseResource(void* resource)
    {
        auto* res = static_cast<X35Resource*>(resource);
        return res && X35IsReadableMemory(res, sizeof(X35Resource)) && res->owner == &g_x35LooseOwner;
    }

    static void X35FreeLooseResource(void* resource)
    {
        auto* res = static_cast<X35Resource*>(resource);
        if (!res) return;
        std::free(res->recordBase);
        std::free(res);
    }

    static void* HookX35OpenResource(unsigned int type, int index)
    {
        if (g_x35InHook || !RealX35OpenResource)
        {
            return RealX35OpenResource ? RealX35OpenResource(type, index) : nullptr;
        }
        ScopedX35Reentry guard;
        if (!guard.active) return RealX35OpenResource(type, index);
        X35LooseFile loose;
        if (X35FindLooseFile(type, index, loose))
        {
            X35Resource* res = X35MakeLooseResource(loose);
            if (res)
            {
                Log(L"INFO", L"x35 read hit type=%u index=%d size=%u file=%s", type, index, loose.size, loose.path.c_str());
                return res;
            }
            Log(L"ERROR", L"x35 loose resource allocation failed type=%u index=%d file=%s", type, index, loose.path.c_str());
        }
        return RealX35OpenResource(type, index);
    }

    static void HookX35ReleaseResource(void* resource)
    {
        if (X35IsLooseResource(resource))
        {
            if (sg_settings.enableLog) Log(L"INFO", L"x35 release loose resource=%p", resource);
            X35FreeLooseResource(resource);
            return;
        }
        if (RealX35ReleaseResource) RealX35ReleaseResource(resource);
    }

    static int X35HexToInt(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    static std::vector<PatternByte> X35ParsePattern(std::string_view pattern)
    {
        std::vector<PatternByte> result;
        result.reserve((pattern.size() + 1) / 3);
        for (size_t i = 0; i + 1 < pattern.size(); ++i)
        {
            if (pattern[i] == ' ') continue;
            int high = X35HexToInt(pattern[i]);
            int low = X35HexToInt(pattern[i + 1]);
            ++i;
            uint8_t mask = 0;
            uint8_t data = 0;
            if (high != -1) { mask |= 0xF0; data |= static_cast<uint8_t>(high << 4); }
            if (low != -1) { mask |= 0x0F; data |= static_cast<uint8_t>(low); }
            result.push_back({ data, mask });
        }
        return result;
    }

    static bool X35PatternMatchesAt(const uint8_t* memory, size_t memorySize, size_t offset, const std::vector<PatternByte>& parsed)
    {
        if (!memory || parsed.empty() || offset > memorySize || memorySize - offset < parsed.size()) return false;
        for (size_t i = 0; i < parsed.size(); ++i)
        {
            if ((memory[offset + i] & parsed[i].mask) != parsed[i].data) return false;
        }
        return true;
    }

    static uint8_t* X35FindPattern(uint8_t* start, size_t size, std::string_view pattern)
    {
        std::vector<PatternByte> parsed = X35ParsePattern(pattern);
        if (parsed.empty() || !start || size < parsed.size()) return nullptr;
        size_t end = size - parsed.size();
        for (size_t i = 0; i <= end; ++i)
        {
            if (X35PatternMatchesAt(start, size, i, parsed)) return start + i;
        }
        return nullptr;
    }

    static size_t X35GetMainModuleSize(HMODULE module)
    {
        auto* base = reinterpret_cast<const uint8_t*>(module);
        if (!base) return 0;
        __try
        {
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
            auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
            if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
            {
                Log(L"WARN", L"X35 unsupported PE machine: 0x%04X", nt->FileHeader.Machine);
                return 0;
            }
            return nt->OptionalHeader.SizeOfImage;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    static bool X35IsInModuleRange(const uint8_t* base, size_t size, const void* ptr, size_t bytes = 1)
    {
        auto* p = static_cast<const uint8_t*>(ptr);
        return base && p >= base && static_cast<size_t>(p - base) < size && bytes <= size - static_cast<size_t>(p - base);
    }

    static uintptr_t X35RvaFromPtr(uint8_t* base, const void* ptr)
    {
        return ptr ? static_cast<uintptr_t>(static_cast<const uint8_t*>(ptr) - base) : 0;
    }

    static void* X35ResolveRipRelative(uint8_t* insn, int dispOffset, int insnSize, uint8_t* moduleBase, size_t moduleSize)
    {
        int32_t rel = 0;
        std::memcpy(&rel, insn + dispOffset, sizeof(rel));
        uint8_t* target = insn + insnSize + rel;
        return X35IsInModuleRange(moduleBase, moduleSize, target, 1) ? target : nullptr;
    }

    static bool X35ResolveByHeuristic(HMODULE mainModule, X35Targets& out)
    {
        auto* base = reinterpret_cast<uint8_t*>(mainModule);
        size_t size = X35GetMainModuleSize(mainModule);
        if (!base || !size) return false;
        uint8_t* open = X35FindPattern(base, size, "53 48 83 EC 40 83 F9 06 77 ?? 85 D2 78 ?? 4C 8D 0D ?? ?? ?? ?? 41 89 CA 4B 8B 04 D1 48 85 C0 74 ?? 80 38 00 75 ?? C1 E1 10");
        uint8_t* release = X35FindPattern(base, size, "48 85 C9 74 ?? 48 8B 41 28 80 38 00 75 ?? 83 69 20 01 C3");
        if (!open || !release)
        {
            Log(L"WARN", L"X35 heuristic failed open=%p release=%p", open, release);
            return false;
        }
        uint8_t* lea = X35FindPattern(open, 0x40, "4C 8D 0D ?? ?? ?? ??");
        void** managers = lea ? static_cast<void**>(X35ResolveRipRelative(lea, 3, 7, base, size)) : nullptr;
        if (!managers)
        {
            Log(L"WARN", L"X35 heuristic failed to resolve manager table");
            return false;
        }
        out.open = reinterpret_cast<FnX35OpenResource>(open);
        out.release = reinterpret_cast<FnX35ReleaseResource>(release);
        out.managers = managers;
        out.openRva = X35RvaFromPtr(base, open);
        out.releaseRva = X35RvaFromPtr(base, release);
        out.managersRva = X35RvaFromPtr(base, managers);
        out.source = L"Heuristic";
        return true;
    }

    static bool X35ResolveTargets(HMODULE mainModule, X35Targets& out)
    {
        if (X35ResolveByHeuristic(mainModule, out))
        {
            return true;
        }
        Log(L"ERROR", L"X35 heuristic resolver failed");
        return false;
    }
    static bool InstallAliceSystem3xHooks()
    {
        if (g_x35Installed)
        {
            Log(L"INFO", L"X35 AliceSystem3x hooks already installed");
            return true;
        }
        if (!sg_settings.enable)
        {
            Log(L"INFO", L"AliceSystem3x disabled by config");
            return false;
        }
        HMODULE mainModule = GetModuleHandleW(nullptr);
        if (!mainModule)
        {
            Log(L"ERROR", L"GetModuleHandleW(nullptr) failed");
            return false;
        }
        X35Targets targets;
        if (!X35ResolveTargets(mainModule, targets) || !targets.open || !targets.release || !targets.managers)
        {
            Log(L"ERROR", L"X35 AliceSystem3x skipped: could not resolve targets");
            return false;
        }
        RealX35OpenResource = targets.open;
        RealX35ReleaseResource = targets.release;
        g_x35Managers = targets.managers;
        Log(L"INFO", L"X35 resolver source=%s openRva=%04X releaseRva=%04X managersRva=%04X",
            targets.source, static_cast<unsigned>(targets.openRva), static_cast<unsigned>(targets.releaseRva), static_cast<unsigned>(targets.managersRva));

        LONG error = DetourTransactionBegin();
        if (error != NO_ERROR)
        {
            Log(L"ERROR", L"X35 DetourTransactionBegin failed: %ld", error);
            return false;
        }
        DetourUpdateThread(GetCurrentThread());
        bool ok = true;
        error = DetourAttach(reinterpret_cast<PVOID*>(&RealX35OpenResource), reinterpret_cast<PVOID>(HookX35OpenResource));
        if (error != NO_ERROR)
        {
            ok = false;
            Log(L"ERROR", L"X35 DetourAttach open failed: %ld", error);
        }
        error = DetourAttach(reinterpret_cast<PVOID*>(&RealX35ReleaseResource), reinterpret_cast<PVOID>(HookX35ReleaseResource));
        if (error != NO_ERROR)
        {
            ok = false;
            Log(L"ERROR", L"X35 DetourAttach release failed: %ld", error);
        }
        if (!ok)
        {
            DetourTransactionAbort();
            RealX35OpenResource = nullptr;
            RealX35ReleaseResource = nullptr;
            g_x35Managers = nullptr;
            return false;
        }
        error = DetourTransactionCommit();
        if (error != NO_ERROR)
        {
            RealX35OpenResource = nullptr;
            RealX35ReleaseResource = nullptr;
            g_x35Managers = nullptr;
            Log(L"ERROR", L"X35 DetourTransactionCommit failed: %ld", error);
            return false;
        }
        g_x35Installed = true;
        Log(L"INFO", L"X35 AliceSystem3x hook summary open=%p release=%p managers=%p result=success", targets.open, targets.release, targets.managers);
        return true;
    }
#else
    static bool InstallAliceSystem3xHooks()
    {
        Log(L"WARN", L"AliceSystem3x hooks are only supported in x86/x64 builds");
        return false;
    }
#endif
    }

    void Apply(const AliceSystem3xSettings& settings, const FilePatchSettings& filePatchSettings)
    {
        if (!settings.enable)
        {
            return;
        }

        sg_settings = settings;
        sg_gameDir = GetGameDirectory();
        InitializePatchFolders(settings, filePatchSettings);
        if (sg_patchFolders.empty())
        {
            Log(L"WARN", L"enabled but no valid patch folders, skip");
            return;
        }

        Log(L"INFO", L"apply enable=1 folders=%u maxFileSize=%u hookExists=%d", static_cast<unsigned>(sg_patchFolders.size()), sg_settings.maxFileSize, sg_settings.hookExistsCheck ? 1 : 0);
        InstallAliceSystem3xHooks();
    }
}
}
