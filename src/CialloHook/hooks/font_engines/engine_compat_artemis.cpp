#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>
#include <wincrypt.h>
#include <Shlwapi.h>
#include <Psapi.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            struct ArtemisPfsEntry
            {
                std::wstring archivePath;
                DWORD offset = 0;
                DWORD size = 0;
                bool encrypted = false;
                BYTE xorKey[20] = {};
            };

            DWORD ReadLe32(const BYTE* data)
            {
                return static_cast<DWORD>(data[0])
                    | (static_cast<DWORD>(data[1]) << 8)
                    | (static_cast<DWORD>(data[2]) << 16)
                    | (static_cast<DWORD>(data[3]) << 24);
            }

            std::wstring GameRoot()
            {
                wchar_t path[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, path, MAX_PATH);
                PathRemoveFileSpecW(path);
                std::wstring root = path;
                if (!root.empty() && root.back() != L'\\') root += L'\\';
                return root;
            }

            std::wstring NormalizeResourceName(std::wstring value)
            {
                for (wchar_t& ch : value)
                {
                    if (ch == L'/') ch = L'\\';
                    ch = static_cast<wchar_t>(towlower(ch));
                }
                while (!value.empty() && (value[0] == L'\\' || value[0] == L'/')) value.erase(value.begin());
                return value;
            }

            bool HasFontFileExtension(const std::wstring& path)
            {
                std::wstring lower = LowerCopy(path);
                return lower.size() >= 4
                    && (lower.rfind(L".ttf") == lower.size() - 4
                        || lower.rfind(L".ttc") == lower.size() - 4
                        || lower.rfind(L".otf") == lower.size() - 4);
            }

            bool PathExistsFile(const std::wstring& path)
            {
                DWORD attr = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
                return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }

            bool IsAbsolutePath(const std::wstring& path)
            {
                return (path.size() >= 2 && path[1] == L':')
                    || (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
            }

            std::wstring WindowsFontsDir()
            {
                wchar_t windowsDir[MAX_PATH] = {};
                if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0) return L"C:\\Windows\\Fonts";
                std::wstring fontsDir = windowsDir;
                if (!fontsDir.empty() && fontsDir.back() != L'\\') fontsDir += L'\\';
                fontsDir += L"Fonts";
                return fontsDir;
            }

            std::wstring BuildFontPathFromRegistryValue(const wchar_t* value)
            {
                if (!value || value[0] == L'\0') return L"";
                std::wstring path = value;
                if (!IsAbsolutePath(path))
                {
                    std::wstring fullPath = WindowsFontsDir();
                    if (!fullPath.empty() && fullPath.back() != L'\\') fullPath += L'\\';
                    fullPath += path;
                    path = fullPath;
                }
                return HasFontFileExtension(path) && PathExistsFile(path) ? path : L"";
            }

            std::wstring NormalizeFontFaceKey(std::wstring value)
            {
                value = LowerCopy(TrimCopy(value));
                size_t suffix = value.find(L" (");
                if (suffix != std::wstring::npos) value = TrimCopy(value.substr(0, suffix));
                return value;
            }

            std::wstring FileNameLower(const std::wstring& path)
            {
                size_t pos = path.find_last_of(L"\\/");
                return LowerCopy(pos == std::wstring::npos ? path : path.substr(pos + 1));
            }

            void SearchSystemFontRegistry(HKEY rootKey, REGSAM wow64Flags, const std::wstring& target, std::wstring& bestPath, DWORD& bestScore)
            {
                HKEY hKey = nullptr;
                if (RegOpenKeyExW(rootKey, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ | wow64Flags, &hKey) != ERROR_SUCCESS) return;
                for (DWORD index = 0;; ++index)
                {
                    wchar_t valueName[512] = {};
                    wchar_t valueText[MAX_PATH * 2] = {};
                    DWORD nameLen = _countof(valueName);
                    DWORD dataLen = sizeof(valueText);
                    DWORD type = 0;
                    LONG status = RegEnumValueW(hKey, index, valueName, &nameLen, nullptr, &type, reinterpret_cast<LPBYTE>(valueText), &dataLen);
                    if (status == ERROR_NO_MORE_ITEMS) break;
                    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;
                    if (type == REG_EXPAND_SZ)
                    {
                        wchar_t expanded[MAX_PATH * 2] = {};
                        if (ExpandEnvironmentStringsW(valueText, expanded, _countof(expanded)) != 0) wcscpy_s(valueText, expanded);
                    }
                    std::wstring filePath = BuildFontPathFromRegistryValue(valueText);
                    if (filePath.empty()) continue;
                    std::wstring nameLower = NormalizeFontFaceKey(valueName);
                    DWORD score = 0;
                    if (nameLower == target) score = 300;
                    else if (nameLower.find(target + L" ") == 0) score = 240;
                    else if (nameLower.find(target) != std::wstring::npos) score = 180;
                    std::wstring fileLower = FileNameLower(filePath);
                    if (fileLower.find(target) != std::wstring::npos) score += 40;
                    if ((fileLower.find(L"bold") != std::wstring::npos || fileLower.find(L"italic") != std::wstring::npos) && score > 10) score -= 10;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestPath = filePath;
                    }
                }
                RegCloseKey(hKey);
            }

            std::wstring ResolveSystemFontFileByFace(const std::wstring& faceName)
            {
                std::wstring target = NormalizeFontFaceKey(faceName);
                if (target.empty()) return L"";
                std::wstring bestPath;
                DWORD bestScore = 0;
                SearchSystemFontRegistry(HKEY_CURRENT_USER, 0, target, bestPath, bestScore);
                SearchSystemFontRegistry(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, target, bestPath, bestScore);
                SearchSystemFontRegistry(HKEY_LOCAL_MACHINE, 0, target, bestPath, bestScore);
                return bestPath;
            }

            std::string WideToAnsiLocal(const std::wstring& value)
            {
                if (value.empty()) return "";
                int len = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) return "";
                std::string result(static_cast<size_t>(len), '\0');
                WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, &result[0], len, nullptr, nullptr);
                if (!result.empty() && result.back() == '\0') result.pop_back();
                return result;
            }

            char LowerAscii(char ch)
            {
                return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
            }

            bool Sha1Bytes(const BYTE* data, DWORD size, BYTE outHash[20])
            {
                HCRYPTPROV hProv = 0;
                HCRYPTHASH hHash = 0;
                bool ok = false;
                if (data && outHash
                    && CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
                    && CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)
                    && CryptHashData(hHash, data, size, 0))
                {
                    DWORD hashSize = 20;
                    ok = CryptGetHashParam(hHash, HP_HASHVAL, outHash, &hashSize, 0) && hashSize == 20;
                }
                if (hHash) CryptDestroyHash(hHash);
                if (hProv) CryptReleaseContext(hProv, 0);
                return ok;
            }

            bool ReadExact(HANDLE hFile, void* buffer, DWORD size)
            {
                DWORD read = 0;
                return ReadFile(hFile, buffer, size, &read, nullptr) && read == size;
            }

            bool SetFilePos(HANDLE hFile, unsigned long long pos)
            {
                LARGE_INTEGER li = {};
                li.QuadPart = static_cast<LONGLONG>(pos);
                return SetFilePointerEx(hFile, li, nullptr, FILE_BEGIN) != FALSE;
            }

            bool ReadPfsHeaderAndIndex(HANDLE hFile, DWORD* fileCount, char* packVersion,
                std::vector<BYTE>& indexBytes, BYTE xorKey[20])
            {
                BYTE header[19] = {};
                if (!SetFilePos(hFile, 0) || !ReadExact(hFile, header, sizeof(header))) return false;
                if (header[0] != 'p' || header[1] != 'f' || (header[2] != '2' && header[2] != '6' && header[2] != '8')) return false;

                DWORD indexSize = ReadLe32(header + 3);
                DWORD count = ReadLe32(header + 7);
                if (indexSize < 4 || indexSize > 64 * 1024 * 1024 || count > 1024 * 1024) return false;

                indexBytes.resize(indexSize);
                if (!SetFilePos(hFile, 7) || !ReadExact(hFile, indexBytes.data(), indexSize))
                {
                    indexBytes.clear();
                    return false;
                }

                if (header[2] == '8')
                {
                    if (!Sha1Bytes(indexBytes.data(), indexSize, xorKey))
                    {
                        indexBytes.clear();
                        return false;
                    }
                }
                else
                {
                    ZeroMemory(xorKey, 20);
                }

                if (fileCount) *fileCount = count;
                if (packVersion) *packVersion = static_cast<char>(header[2]);
                return true;
            }

            void XorPfsBytes(std::vector<BYTE>& bytes, const BYTE xorKey[20])
            {
                if (bytes.empty()) return;
                for (size_t i = 0; i < bytes.size(); ++i)
                {
                    bytes[i] ^= xorKey[i % 20];
                }
            }

            bool IsLikelyNameByte(BYTE value)
            {
                return (value >= '0' && value <= '9')
                    || (value >= 'A' && value <= 'Z')
                    || (value >= 'a' && value <= 'z')
                    || value == '\\' || value == '/' || value == '_' || value == '-' || value == '.' || value >= 0x80;
            }

            unsigned long long DetectPfsIndexStart(HANDLE hFile)
            {
                BYTE probe[128] = {};
                if (!SetFilePos(hFile, 0) || !ReadExact(hFile, probe, sizeof(probe))) return 12;
                for (DWORD pos = 0; pos + 8 < sizeof(probe); ++pos)
                {
                    DWORD nameLen = ReadLe32(probe + pos);
                    if (nameLen == 0 || nameLen > 512 || pos + 4 + nameLen >= sizeof(probe)) continue;
                    bool hasSlash = false;
                    bool valid = true;
                    for (DWORD i = 0; i < nameLen; ++i)
                    {
                        BYTE ch = probe[pos + 4 + i];
                        if (!IsLikelyNameByte(ch)) { valid = false; break; }
                        if (ch == '\\' || ch == '/') hasSlash = true;
                    }
                    if (valid && hasSlash) return pos;
                }
                return 12;
            }

            bool TryFindPfsEntryInArchive(const std::wstring& archivePath, const std::wstring& resourceName, ArtemisPfsEntry& entry)
            {
                HANDLE hFile = CreateFileW(archivePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE) return false;

                LARGE_INTEGER fileSize = {};
                if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 32)
                {
                    CloseHandle(hFile);
                    return false;
                }

                std::string target = WideToAnsiLocal(NormalizeResourceName(resourceName));
                for (char& ch : target) { if (ch == '/') ch = '\\'; ch = LowerAscii(ch); }

                DWORD fileCount = 0;
                char packVersion = 0;
                BYTE xorKey[20] = {};
                std::vector<BYTE> indexBytes;
                bool parsedHeader = ReadPfsHeaderAndIndex(hFile, &fileCount, &packVersion, indexBytes, xorKey);
                unsigned long long pos = parsedHeader ? 4 : DetectPfsIndexStart(hFile);
                unsigned long long indexEnd = parsedHeader ? indexBytes.size() : static_cast<unsigned long long>(fileSize.QuadPart);
                unsigned long long minDataOffset = static_cast<unsigned long long>(fileSize.QuadPart);
                DWORD parsedCount = 0;
                bool found = false;

                while (pos + 16 < indexEnd && pos + 16 < minDataOffset)
                {
                    DWORD nameLen = 0;
                    const BYTE* nameBytes = nullptr;
                    BYTE trailer[20] = {};
                    if (parsedHeader)
                    {
                        if (pos + 4 > indexBytes.size()) break;
                        nameLen = ReadLe32(indexBytes.data() + pos);
                        pos += 4;
                        if (nameLen == 0 || nameLen > 512 || pos + nameLen + 12 > indexBytes.size()) break;
                        nameBytes = indexBytes.data() + pos;
                        memcpy(trailer, indexBytes.data() + pos + nameLen, 12);
                        pos += nameLen + 12;
                    }
                    else
                    {
                        BYTE lenBytes[4] = {};
                        if (!SetFilePos(hFile, pos) || !ReadExact(hFile, lenBytes, sizeof(lenBytes))) break;
                        nameLen = ReadLe32(lenBytes);
                        pos += 4;
                        if (nameLen == 0 || nameLen > 512 || pos + nameLen + 12 > static_cast<unsigned long long>(fileSize.QuadPart)) break;
                        static BYTE fallbackName[512] = {};
                        if (!ReadExact(hFile, fallbackName, nameLen) || !ReadExact(hFile, trailer, 12)) break;
                        nameBytes = fallbackName;
                        pos += nameLen + 12;
                    }

                    ++parsedCount;
                    DWORD offset = ReadLe32(trailer + 4);
                    DWORD size = ReadLe32(trailer + 8);
                    if (offset > 0 && offset < minDataOffset) minDataOffset = offset;

                    std::string name(reinterpret_cast<const char*>(nameBytes), nameLen);
                    for (char& ch : name) { if (ch == '/') ch = '\\'; ch = LowerAscii(ch); }
                    if (name == target && static_cast<unsigned long long>(offset) + size <= static_cast<unsigned long long>(fileSize.QuadPart))
                    {
                        entry.archivePath = archivePath;
                        entry.offset = offset;
                        entry.size = size;
                        entry.encrypted = parsedHeader && packVersion == '8';
                        memcpy(entry.xorKey, xorKey, sizeof(entry.xorKey));
                        found = true;
                        break;
                    }
                    if (parsedHeader && parsedCount >= fileCount) break;
                }

                CloseHandle(hFile);
                return found;
            }

            bool TryFindPfsEntry(const std::wstring& resourceName, ArtemisPfsEntry& entry)
            {
                std::wstring search = GameRoot() + L"*.pfs";
                WIN32_FIND_DATAW data = {};
                HANDLE hFind = FindFirstFileW(search.c_str(), &data);
                if (hFind == INVALID_HANDLE_VALUE) return false;
                bool found = false;
                do
                {
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
                    std::wstring archivePath = GameRoot() + data.cFileName;
                    if (TryFindPfsEntryInArchive(archivePath, resourceName, entry)) { found = true; break; }
                } while (FindNextFileW(hFind, &data));
                FindClose(hFind);
                return found;
            }

            bool ReadPfsResource(const std::wstring& resourceName, std::vector<BYTE>& bytes)
            {
                ArtemisPfsEntry entry = {};
                if (!TryFindPfsEntry(resourceName, entry)) return false;
                HANDLE hFile = CreateFileW(entry.archivePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE) return false;
                bytes.resize(entry.size);
                bool ok = SetFilePos(hFile, entry.offset) && (entry.size == 0 || ReadExact(hFile, bytes.data(), entry.size));
                CloseHandle(hFile);
                if (!ok) { bytes.clear(); return false; }
                if (entry.encrypted) XorPfsBytes(bytes, entry.xorKey);
                return true;
            }

            void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
            {
                if (from.empty() || to.empty()) return;
                size_t pos = 0;
                while ((pos = text.find(from, pos)) != std::string::npos)
                {
                    text.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }

            std::string WideToUtf8Local(const std::wstring& value)
            {
                if (value.empty()) return "";
                int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
                if (len <= 0) return "";
                std::string result(static_cast<size_t>(len), '\0');
                WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], len, nullptr, nullptr);
                if (!result.empty() && result.back() == '\0') result.pop_back();
                return result;
            }

            bool PatchArtemisPfsTable(std::vector<BYTE>& bytes, const std::wstring& replacementFontFile, const std::wstring& replacementFaceName)
            {
                if (bytes.empty()) return false;
                std::string text(bytes.begin(), bytes.end());
                std::string fontPath = WideToUtf8Local(replacementFontFile.empty() ? replacementFaceName : replacementFontFile);
                std::string faceName = WideToUtf8Local(replacementFaceName.empty() ? replacementFontFile : replacementFaceName);
                if (!fontPath.empty())
                {
                    for (int i = 0; i <= 32; ++i)
                    {
                        char key[16] = {};
                        sprintf_s(key, "font%02d", i);
                        ReplaceAll(text, key, fontPath);
                    }
                }
                if (!faceName.empty())
                {
                    ReplaceAll(text, "MS Gothic", faceName);
                    ReplaceAll(text, "MS PGothic", faceName);
                    ReplaceAll(text, "ＭＳ ゴシック", faceName);
                }
                ReplaceAll(text, "spacemiddle=0", "spacemiddle=1");
                ReplaceAll(text, "kerning=0", "kerning=1");
                bytes.assign(text.begin(), text.end());
                return true;
            }

            void RegisterPfsTableIfPresent(const wchar_t* resourceName, const std::wstring& replacementFontFile, const std::wstring& replacementFaceName)
            {
                std::vector<BYTE> bytes;
                if (!ReadPfsResource(resourceName, bytes)) return;
                if (!PatchArtemisPfsTable(bytes, replacementFontFile, replacementFaceName)) return;
                AddEngineMemoryFileRule(resourceName, bytes.data(), bytes.size(), false, false);
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis PFS table virtualized resource=%s size=%u", resourceName, static_cast<unsigned>(bytes.size()));
            }

            struct ArtemisCacheTarget
            {
                DWORD* entryCount = nullptr;
                DWORD* bucketCount = nullptr;
                uintptr_t** buckets = nullptr;
            };

            struct ArtemisCacheClearPlan
            {
                BYTE* moduleBase = nullptr;
                DWORD moduleSize = 0;
                ArtemisCacheTarget first;
                ArtemisCacheTarget second;
                ArtemisCacheTarget glyph;
            };

            ArtemisCacheClearPlan sg_artemisCacheClearPlan = {};
            bool sg_artemisCacheClearResolved = false;
            bool sg_artemisCacheClearAvailable = false;
            volatile LONG sg_artemisCacheClearAttempted = 0;
            volatile LONG sg_artemisAtlasInvalidateAttempted = 0;
            volatile LONG sg_artemisFreeTypeReloadAttempted = 0;
            volatile LONG sg_artemisObjectScanDiagAttempted = 0;
            std::mutex sg_artemisAtlasMutex;
            std::vector<void*> sg_artemisKnownAtlases;
            std::mutex sg_artemisFreeTypeMutex;
            std::vector<void*> sg_artemisKnownFreeTypeFonts;
            std::string sg_artemisActiveReloadPathKey;

            constexpr uintptr_t kArtemisKnownImageBase = 0x400000;
            constexpr uintptr_t kArtemisCFontRendererAtlasVtableVa = 0x7E4BE4;
            constexpr uintptr_t kArtemisCFreeTypeFontVtableVa = 0x7C7504;
            constexpr uintptr_t kArtemisCFreeTypeFontReloadVa = 0x5D7950;

            bool ArtemisPointerInMainModule(const ArtemisCacheClearPlan& plan, const void* value)
            {
                uintptr_t ptr = reinterpret_cast<uintptr_t>(value);
                uintptr_t base = reinterpret_cast<uintptr_t>(plan.moduleBase);
                return ptr >= base && ptr < base + plan.moduleSize;
            }

            bool ArtemisWritableRange(void* ptr, size_t bytes)
            {
                if (!ptr || bytes == 0) return false;
                BYTE* current = static_cast<BYTE*>(ptr);
                BYTE* end = current + bytes;
                if (end < current) return false;
                while (current < end)
                {
                    MEMORY_BASIC_INFORMATION mbi = {};
                    if (VirtualQuery(current, &mbi, sizeof(mbi)) == 0) return false;
                    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
                    DWORD protect = mbi.Protect & 0xFF;
                    bool writable = protect == PAGE_READWRITE || protect == PAGE_WRITECOPY
                        || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
                    if (!writable) return false;
                    BYTE* regionEnd = static_cast<BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
                    if (regionEnd <= current) return false;
                    current = regionEnd < end ? regionEnd : end;
                }
                return true;
            }

            BYTE* ArtemisFindBytes(BYTE* start, size_t size, const BYTE* pattern, size_t patternSize)
            {
                if (!start || !pattern || patternSize == 0 || size < patternSize) return nullptr;
                for (BYTE* p = start; p <= start + size - patternSize; ++p)
                {
                    if (memcmp(p, pattern, patternSize) == 0) return p;
                }
                return nullptr;
            }

            BYTE* ArtemisFindPointer32(BYTE* start, size_t size, uintptr_t value)
            {
                DWORD needle = static_cast<DWORD>(value);
                return ArtemisFindBytes(start, size, reinterpret_cast<const BYTE*>(&needle), sizeof(needle));
            }

            BYTE* ArtemisFindHashClearPattern(BYTE* start, size_t size)
            {
                if (!start || size < 23) return nullptr;
                for (BYTE* p = start; p + 23 <= start + size; ++p)
                {
                    if (p[0] == 0x83 && p[1] == 0x3D && p[6] == 0x00 && p[7] == 0x74
                        && p[9] == 0x8B && p[10] == 0x0D && p[15] == 0xA1
                        && p[20] == 0x8D && p[21] == 0x04 && p[22] == 0x81)
                    {
                        return p;
                    }
                }
                return nullptr;
            }

            bool ArtemisParseCacheTarget(BYTE* hashPattern, ArtemisCacheTarget& target, const ArtemisCacheClearPlan& plan)
            {
                if (!hashPattern) return false;
                target.entryCount = reinterpret_cast<DWORD*>(static_cast<uintptr_t>(ReadLe32(hashPattern + 2)));
                target.buckets = reinterpret_cast<uintptr_t**>(static_cast<uintptr_t>(ReadLe32(hashPattern + 11)));
                target.bucketCount = reinterpret_cast<DWORD*>(static_cast<uintptr_t>(ReadLe32(hashPattern + 16)));
                return ArtemisPointerInMainModule(plan, target.entryCount)
                    && ArtemisPointerInMainModule(plan, target.bucketCount)
                    && ArtemisPointerInMainModule(plan, target.buckets);
            }

            bool ArtemisResolveCacheClearPlan()
            {
                if (sg_artemisCacheClearResolved) return sg_artemisCacheClearAvailable;
                sg_artemisCacheClearResolved = true;
#if defined(_M_IX86)
                HMODULE hExe = GetModuleHandleW(nullptr);
                MODULEINFO mi = {};
                if (!hExe || !GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) || mi.SizeOfImage == 0)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear unavailable reason=module-info-failed");
                    return false;
                }
                ArtemisCacheClearPlan plan = {};
                plan.moduleBase = static_cast<BYTE*>(mi.lpBaseOfDll);
                plan.moduleSize = mi.SizeOfImage;
                const BYTE marker[] = "clear_cache";
                BYTE* markerAddress = ArtemisFindBytes(plan.moduleBase, plan.moduleSize, marker, sizeof(marker));
                BYTE* xref = markerAddress ? ArtemisFindPointer32(plan.moduleBase, plan.moduleSize, reinterpret_cast<uintptr_t>(markerAddress)) : nullptr;
                if (!xref)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear unavailable reason=marker-xref-not-found");
                    return false;
                }
                BYTE* scanStart = xref;
                size_t scanSize = std::min<size_t>(1024, static_cast<size_t>(plan.moduleBase + plan.moduleSize - scanStart));
                BYTE* lockBlock = nullptr;
                for (BYTE* p = scanStart; p + 24 < scanStart + scanSize; ++p)
                {
                    if (p[0] == 0x68 && p[5] == 0xE8 && p[10] == 0x83 && p[11] == 0xC4
                        && p[12] == 0x04 && p[13] == 0x85 && p[14] == 0xC0 && p[15] == 0x74
                        && p[17] == 0x50 && p[18] == 0xE8)
                    {
                        lockBlock = p;
                        break;
                    }
                }
                BYTE* firstHash = lockBlock ? ArtemisFindHashClearPattern(lockBlock + 20, static_cast<size_t>(plan.moduleBase + plan.moduleSize - (lockBlock + 20))) : nullptr;
                BYTE* secondHash = firstHash ? ArtemisFindHashClearPattern(firstHash + 23, static_cast<size_t>(plan.moduleBase + plan.moduleSize - (firstHash + 23))) : nullptr;
                if (!firstHash || !secondHash || !ArtemisParseCacheTarget(firstHash, plan.first, plan) || !ArtemisParseCacheTarget(secondHash, plan.second, plan))
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear unavailable reason=target-parse-failed");
                    return false;
                }
                plan.glyph.bucketCount = reinterpret_cast<DWORD*>(plan.moduleBase + (0x878C28 - kArtemisKnownImageBase));
                plan.glyph.entryCount = reinterpret_cast<DWORD*>(plan.moduleBase + (0x878C2C - kArtemisKnownImageBase));
                plan.glyph.buckets = reinterpret_cast<uintptr_t**>(plan.moduleBase + (0x878C38 - kArtemisKnownImageBase));
                if (!ArtemisPointerInMainModule(plan, plan.glyph.entryCount) || !ArtemisPointerInMainModule(plan, plan.glyph.bucketCount) || !ArtemisPointerInMainModule(plan, plan.glyph.buckets))
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear unavailable reason=glyph-target-out-of-module");
                    return false;
                }
                sg_artemisCacheClearPlan = plan;
                sg_artemisCacheClearAvailable = true;
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear plan resolved");
                return true;
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear skipped on non-x86 build");
                return false;
#endif
            }

            bool ArtemisClearHashTarget(const ArtemisCacheTarget& target, DWORD& clearedEntries, DWORD& detachedList)
            {
                clearedEntries = 0;
                detachedList = 0;
                if (!target.entryCount || !target.bucketCount || !target.buckets) return false;
                DWORD entries = *target.entryCount;
                if (entries == 0) return true;
                DWORD bucketCount = *target.bucketCount;
                uintptr_t* buckets = *target.buckets;
                if (bucketCount > 1024 * 1024 || !buckets
                    || !ArtemisWritableRange(buckets, (static_cast<size_t>(bucketCount) + 1) * sizeof(uintptr_t))
                    || !ArtemisWritableRange(target.entryCount, sizeof(DWORD)))
                {
                    return false;
                }
                uintptr_t* end = buckets + bucketCount;
                for (uintptr_t* p = buckets; p != end; ++p) *p = 0;
                detachedList = *end ? 1 : 0;
                *end = 0;
                *target.entryCount = 0;
                clearedEntries = entries;
                return true;
            }

            bool ArtemisWritableObjectScanProtect(DWORD protect)
            {
                if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
                DWORD baseProtect = protect & 0xFF;
                return baseProtect == PAGE_READWRITE || baseProtect == PAGE_WRITECOPY
                    || baseProtect == PAGE_EXECUTE_READWRITE || baseProtect == PAGE_EXECUTE_WRITECOPY;
            }

            bool ArtemisReadPointerProtected(void* address, uintptr_t& value)
            {
#if defined(_M_IX86)
                __try
                {
                    value = *reinterpret_cast<uintptr_t*>(address);
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return false;
                }
#else
                UNREFERENCED_PARAMETER(address);
                UNREFERENCED_PARAMETER(value);
                return false;
#endif
            }

            bool ArtemisIsLiveAtlasObject(void* object, uintptr_t atlasVtable)
            {
#if defined(_M_IX86)
                __try
                {
                    BYTE* obj = static_cast<BYTE*>(object);
                    if (*reinterpret_cast<uintptr_t*>(obj) != atlasVtable) return false;
                    DWORD width = *reinterpret_cast<DWORD*>(obj + 0x0C);
                    DWORD height = *reinterpret_cast<DWORD*>(obj + 0x10);
                    return width != 0 && height != 0 && width <= 16384 && height <= 16384;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return false;
                }
#else
                UNREFERENCED_PARAMETER(object);
                UNREFERENCED_PARAMETER(atlasVtable);
                return false;
#endif
            }

            void ArtemisRememberAtlas(std::vector<void*>& atlases, void* object)
            {
                for (void* existing : atlases)
                {
                    if (existing == object) return;
                }
                atlases.push_back(object);
            }

            void ArtemisDiscoverFontAtlases()
            {
#if defined(_M_IX86)
                HMODULE hExe = GetModuleHandleW(nullptr);
                MODULEINFO mi = {};
                if (!hExe || !GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) || mi.SizeOfImage == 0)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis atlas discovery unavailable reason=module-info-failed");
                    return;
                }

                uintptr_t moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                uintptr_t atlasVtable = moduleBase + (kArtemisCFontRendererAtlasVtableVa - kArtemisKnownImageBase);
                std::vector<void*> found;
                SYSTEM_INFO sysInfo = {};
                GetSystemInfo(&sysInfo);
                BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);
                BYTE* maxAddress = static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress);
                DWORD scannedRegions = 0;
                DWORD skippedReads = 0;

                while (address < maxAddress && found.size() < 64)
                {
                    MEMORY_BASIC_INFORMATION mbi = {};
                    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) break;
                    BYTE* regionBase = static_cast<BYTE*>(mbi.BaseAddress);
                    uintptr_t nextAddressValue = reinterpret_cast<uintptr_t>(regionBase) + mbi.RegionSize;
                    if (nextAddressValue <= reinterpret_cast<uintptr_t>(address)) break;
                    BYTE* nextAddress = reinterpret_cast<BYTE*>(nextAddressValue);

                    if (mbi.State == MEM_COMMIT && ArtemisWritableObjectScanProtect(mbi.Protect) && mbi.RegionSize >= sizeof(uintptr_t))
                    {
                        uintptr_t regionStart = reinterpret_cast<uintptr_t>(regionBase);
                        uintptr_t scan = (regionStart + sizeof(uintptr_t) - 1) & ~(static_cast<uintptr_t>(sizeof(uintptr_t)) - 1);
                        for (; scan + 0x28 <= nextAddressValue && found.size() < 64; scan += sizeof(uintptr_t))
                        {
                            uintptr_t value = 0;
                            if (!ArtemisReadPointerProtected(reinterpret_cast<void*>(scan), value))
                            {
                                ++skippedReads;
                                continue;
                            }
                            if (value == atlasVtable && ArtemisIsLiveAtlasObject(reinterpret_cast<void*>(scan), atlasVtable))
                            {
                                ArtemisRememberAtlas(found, reinterpret_cast<void*>(scan));
                            }
                        }
                        ++scannedRegions;
                    }
                    address = nextAddress;
                }

                {
                    std::lock_guard<std::mutex> lock(sg_artemisAtlasMutex);
                    sg_artemisKnownAtlases.swap(found);
                }
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis atlas discovery known=%u scannedRegions=%u skippedReads=%u",
                    static_cast<unsigned>(sg_artemisKnownAtlases.size()), scannedRegions, skippedReads);
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis atlas discovery skipped on non-x86 build");
#endif
            }

            bool ArtemisTryInvalidateAtlasObject(void* object, DWORD& matched, DWORD& invalidated, DWORD& failed)
            {
#if defined(_M_IX86)
                __try
                {
                    BYTE* obj = static_cast<BYTE*>(object);
                    DWORD width = *reinterpret_cast<DWORD*>(obj + 0x0C);
                    DWORD height = *reinterpret_cast<DWORD*>(obj + 0x10);
                    if (width == 0 || height == 0 || width > 16384 || height > 16384) return false;
                    ++matched;
                    *reinterpret_cast<DWORD*>(obj + 0x1C) = 0;
                    *reinterpret_cast<DWORD*>(obj + 0x20) = 0;
                    *reinterpret_cast<DWORD*>(obj + 0x24) = 0;
                    ++invalidated;
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    ++failed;
                    return false;
                }
#else
                UNREFERENCED_PARAMETER(object);
                UNREFERENCED_PARAMETER(matched);
                UNREFERENCED_PARAMETER(invalidated);
                UNREFERENCED_PARAMETER(failed);
                return false;
#endif
            }

            void ArtemisTryInvalidateFontAtlases(const EngineCompatState& state)
            {
                if (!state.allowAggressiveMemoryScan || InterlockedExchange(&sg_artemisAtlasInvalidateAttempted, 1) != 0)
                {
                    return;
                }
#if defined(_M_IX86)
                ArtemisDiscoverFontAtlases();
                std::vector<void*> atlases;
                {
                    std::lock_guard<std::mutex> lock(sg_artemisAtlasMutex);
                    atlases = sg_artemisKnownAtlases;
                }

                HMODULE hExe = GetModuleHandleW(nullptr);
                MODULEINFO mi = {};
                uintptr_t atlasVtable = 0;
                if (hExe && GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) && mi.SizeOfImage != 0)
                {
                    atlasVtable = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll) + (kArtemisCFontRendererAtlasVtableVa - kArtemisKnownImageBase);
                }

                DWORD matched = 0;
                DWORD invalidated = 0;
                DWORD failed = 0;
                for (void* atlas : atlases)
                {
                    if (atlasVtable && !ArtemisIsLiveAtlasObject(atlas, atlasVtable))
                    {
                        ++failed;
                        continue;
                    }
                    ArtemisTryInvalidateAtlasObject(atlas, matched, invalidated, failed);
                }
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis atlas invalidate known=%u matched=%u invalidated=%u failed=%u",
                    static_cast<unsigned>(atlases.size()), matched, invalidated, failed);
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis atlas invalidate requested but unavailable on non-x86 build");
#endif
            }

            std::string ArtemisNormalizeAnsiPathKey(std::string path)
            {
                for (char& ch : path)
                {
                    if (ch == '/') ch = '\\';
                    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                }
                return path;
            }

            bool ArtemisReadInlineAnsiString(void* stringObject, std::string& out)
            {
                out.clear();
#if defined(_M_IX86)
                if (!stringObject) return false;
                __try
                {
                    BYTE* obj = static_cast<BYTE*>(stringObject);
                    DWORD length = *reinterpret_cast<DWORD*>(obj + 0x10);
                    DWORD capacity = *reinterpret_cast<DWORD*>(obj + 0x14);
                    if (length > 1024 || capacity > 1024 * 1024) return false;
                    const char* data = capacity < 0x10 ? reinterpret_cast<const char*>(obj) : *reinterpret_cast<const char**>(obj);
                    if (!data && length != 0) return false;
                    out.assign(data ? data : "", data ? length : 0);
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return false;
                }
#else
                UNREFERENCED_PARAMETER(stringObject);
                return false;
#endif
            }

            bool ArtemisIsFreeTypeReloadCandidatePath(const std::string& oldPath)
            {
                std::string key = ArtemisNormalizeAnsiPathKey(oldPath);
                if (!key.empty() && !sg_artemisActiveReloadPathKey.empty() && key == sg_artemisActiveReloadPathKey)
                {
                    return true;
                }
                return key.find("\\font\\") != std::string::npos
                    || key.find("/font/") != std::string::npos
                    || (key.size() >= 4 && (key.rfind(".ttf") == key.size() - 4 || key.rfind(".ttc") == key.size() - 4 || key.rfind(".otf") == key.size() - 4));
            }

#if defined(_M_IX86)
            typedef unsigned char(__thiscall* ArtemisFreeTypeFontReloadFn)(void* self, const char* path, int unused0, int size, int flag0, int flag1, int unused1);
#endif

            bool ArtemisCallFreeTypeReloadProtected(void* object, uintptr_t reloadFnAddress, const char* reloadPath,
                unsigned char sizeArg, unsigned char flag0Arg, unsigned char flag1Arg, unsigned char& result)
            {
#if defined(_M_IX86)
                if (!object || !reloadFnAddress || !reloadPath || !reloadPath[0]) return false;
                __try
                {
                    ArtemisFreeTypeFontReloadFn reloadFn = reinterpret_cast<ArtemisFreeTypeFontReloadFn>(reloadFnAddress);
                    result = reloadFn(object, reloadPath, 0, sizeArg, flag0Arg, flag1Arg, 0);
                    return true;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    return false;
                }
#else
                UNREFERENCED_PARAMETER(object);
                UNREFERENCED_PARAMETER(reloadFnAddress);
                UNREFERENCED_PARAMETER(reloadPath);
                UNREFERENCED_PARAMETER(sizeArg);
                UNREFERENCED_PARAMETER(flag0Arg);
                UNREFERENCED_PARAMETER(flag1Arg);
                UNREFERENCED_PARAMETER(result);
                return false;
#endif
            }

            void ArtemisRememberFreeTypeFont(void* object)
            {
                if (!object) return;
                std::lock_guard<std::mutex> lock(sg_artemisFreeTypeMutex);
                for (void* existing : sg_artemisKnownFreeTypeFonts)
                {
                    if (existing == object) return;
                }
                if (sg_artemisKnownFreeTypeFonts.size() < 32) sg_artemisKnownFreeTypeFonts.push_back(object);
            }

            bool ArtemisTryReloadFreeTypeFontObject(void* object, uintptr_t reloadFnAddress, const char* requestedVirtualPath,
                const char* reloadPath, DWORD& matched, DWORD& reloaded, DWORD& failed)
            {
#if defined(_M_IX86)
                if (!object || !reloadFnAddress || !requestedVirtualPath || !reloadPath) return false;
                std::string oldPath;
                if (!ArtemisReadInlineAnsiString(static_cast<BYTE*>(object) + 0x24, oldPath)) return false;
                if (!ArtemisIsFreeTypeReloadCandidatePath(oldPath)) return false;
                ++matched;

                BYTE* obj = static_cast<BYTE*>(object);
                unsigned char size = *reinterpret_cast<unsigned char*>(obj + 0x08);
                unsigned char flag0 = *reinterpret_cast<unsigned char*>(obj + 0x3C);
                unsigned char flag1 = *reinterpret_cast<unsigned char*>(obj + 0x3D);
                unsigned char probeSize = size == 255 ? static_cast<unsigned char>(254) : static_cast<unsigned char>(size + 1);
                bool sameReloadPath = ArtemisNormalizeAnsiPathKey(oldPath) == ArtemisNormalizeAnsiPathKey(reloadPath);
                unsigned char finalResult = 0xFF;
                unsigned char probeResult = 0xFF;
                bool finalOk = ArtemisCallFreeTypeReloadProtected(object, reloadFnAddress, reloadPath, size, flag0, flag1, finalResult);
                bool probeOk = false;
                if (sameReloadPath && finalOk && finalResult == 0)
                {
                    probeOk = ArtemisCallFreeTypeReloadProtected(object, reloadFnAddress, reloadPath, probeSize, flag0, flag1, probeResult);
                    if (probeOk && probeResult == 0)
                    {
                        finalOk = ArtemisCallFreeTypeReloadProtected(object, reloadFnAddress, reloadPath, size, flag0, flag1, finalResult);
                    }
                }
                bool reloadOk = finalOk && finalResult == 0 && (!sameReloadPath || (probeOk && probeResult == 0));
                if (reloadOk)
                {
                    ++reloaded;
                    ArtemisRememberFreeTypeFont(object);
                }
                else
                {
                    ++failed;
                }
                return reloadOk;
#else
                UNREFERENCED_PARAMETER(object);
                UNREFERENCED_PARAMETER(reloadFnAddress);
                UNREFERENCED_PARAMETER(requestedVirtualPath);
                UNREFERENCED_PARAMETER(reloadPath);
                UNREFERENCED_PARAMETER(matched);
                UNREFERENCED_PARAMETER(reloaded);
                UNREFERENCED_PARAMETER(failed);
                return false;
#endif
            }

            void ArtemisScanFreeTypeReloadRegion(BYTE* regionBase, uintptr_t regionEnd, uintptr_t freeTypeVtable,
                uintptr_t reloadFnAddress, const char* requestedVirtualPath, const char* reloadPath,
                DWORD& matched, DWORD& reloaded, DWORD& failed)
            {
#if defined(_M_IX86)
                __try
                {
                    uintptr_t regionStart = reinterpret_cast<uintptr_t>(regionBase);
                    uintptr_t scan = (regionStart + sizeof(uintptr_t) - 1) & ~(static_cast<uintptr_t>(sizeof(uintptr_t)) - 1);
                    for (; scan + 0x4C <= regionEnd && matched < 16; scan += sizeof(uintptr_t))
                    {
                        if (*reinterpret_cast<uintptr_t*>(scan) != freeTypeVtable) continue;
                        ArtemisTryReloadFreeTypeFontObject(reinterpret_cast<void*>(scan), reloadFnAddress, requestedVirtualPath, reloadPath, matched, reloaded, failed);
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    ++failed;
                }
#else
                UNREFERENCED_PARAMETER(regionBase);
                UNREFERENCED_PARAMETER(regionEnd);
                UNREFERENCED_PARAMETER(freeTypeVtable);
                UNREFERENCED_PARAMETER(reloadFnAddress);
                UNREFERENCED_PARAMETER(requestedVirtualPath);
                UNREFERENCED_PARAMETER(reloadPath);
                UNREFERENCED_PARAMETER(matched);
                UNREFERENCED_PARAMETER(reloaded);
                UNREFERENCED_PARAMETER(failed);
#endif
            }

            void ArtemisLogObjectScanDiagnostics(const EngineCompatState& state)
            {
                if (!state.allowAggressiveMemoryScan || InterlockedExchange(&sg_artemisObjectScanDiagAttempted, 1) != 0)
                {
                    return;
                }
#if defined(_M_IX86)
                HMODULE hExe = GetModuleHandleW(nullptr);
                MODULEINFO mi = {};
                if (!hExe || !GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) || mi.SizeOfImage == 0)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis object-scan diag unavailable reason=module-info-failed");
                    return;
                }

                uintptr_t moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                uintptr_t atlasVtable = moduleBase + (kArtemisCFontRendererAtlasVtableVa - kArtemisKnownImageBase);
                uintptr_t freeTypeVtable = moduleBase + (kArtemisCFreeTypeFontVtableVa - kArtemisKnownImageBase);
                DWORD writableRegions = 0;
                DWORD atlasCandidates = 0;
                DWORD liveAtlases = 0;
                DWORD freeTypeCandidates = 0;
                DWORD skippedReads = 0;

                SYSTEM_INFO sysInfo = {};
                GetSystemInfo(&sysInfo);
                BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);
                BYTE* maxAddress = static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress);
                while (address < maxAddress)
                {
                    MEMORY_BASIC_INFORMATION mbi = {};
                    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) break;
                    BYTE* regionBase = static_cast<BYTE*>(mbi.BaseAddress);
                    uintptr_t nextAddressValue = reinterpret_cast<uintptr_t>(regionBase) + mbi.RegionSize;
                    if (nextAddressValue <= reinterpret_cast<uintptr_t>(address)) break;
                    BYTE* nextAddress = reinterpret_cast<BYTE*>(nextAddressValue);

                    if (mbi.State == MEM_COMMIT && ArtemisWritableObjectScanProtect(mbi.Protect) && mbi.RegionSize >= sizeof(uintptr_t))
                    {
                        ++writableRegions;
                        uintptr_t regionStart = reinterpret_cast<uintptr_t>(regionBase);
                        uintptr_t scan = (regionStart + sizeof(uintptr_t) - 1) & ~(static_cast<uintptr_t>(sizeof(uintptr_t)) - 1);
                        for (; scan + 0x4C <= nextAddressValue; scan += sizeof(uintptr_t))
                        {
                            uintptr_t value = 0;
                            if (!ArtemisReadPointerProtected(reinterpret_cast<void*>(scan), value))
                            {
                                ++skippedReads;
                                continue;
                            }
                            if (value == atlasVtable)
                            {
                                ++atlasCandidates;
                                if (ArtemisIsLiveAtlasObject(reinterpret_cast<void*>(scan), atlasVtable)) ++liveAtlases;
                            }
                            else if (value == freeTypeVtable)
                            {
                                ++freeTypeCandidates;
                            }
                        }
                    }
                    address = nextAddress;
                }

                LogMessage(LogLevel::Info, L"EngineCompat: Artemis object-scan diag writableRegions=%u atlasCandidates=%u liveAtlases=%u freeTypeCandidates=%u skippedReads=%u",
                    writableRegions, atlasCandidates, liveAtlases, freeTypeCandidates, skippedReads);
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis object-scan diag skipped on non-x86 build");
#endif
            }

            void ArtemisTryReloadActiveFreeTypeFonts(const EngineCompatState& state, const std::wstring& replacementFontFile)
            {
                if (!state.allowAggressiveMemoryScan || replacementFontFile.empty() || InterlockedExchange(&sg_artemisFreeTypeReloadAttempted, 1) != 0)
                {
                    return;
                }
#if defined(_M_IX86)
                HMODULE hExe = GetModuleHandleW(nullptr);
                MODULEINFO mi = {};
                if (!hExe || !GetModuleInformation(GetCurrentProcess(), hExe, &mi, sizeof(mi)) || mi.SizeOfImage == 0)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis FreeType reload unavailable reason=module-info-failed");
                    return;
                }
                uintptr_t moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                uintptr_t freeTypeVtable = moduleBase + (kArtemisCFreeTypeFontVtableVa - kArtemisKnownImageBase);
                uintptr_t reloadFnAddress = moduleBase + (kArtemisCFreeTypeFontReloadVa - kArtemisKnownImageBase);
                std::string reloadPath = WideToAnsiLocal(replacementFontFile);
                std::string virtualPath = reloadPath;
                for (char& ch : virtualPath) if (ch == '\\') ch = '/';
                if (reloadPath.empty()) return;
                std::string reloadPathKey = ArtemisNormalizeAnsiPathKey(reloadPath);

                DWORD matched = 0;
                DWORD reloaded = 0;
                DWORD failed = 0;
                std::vector<void*> known;
                {
                    std::lock_guard<std::mutex> lock(sg_artemisFreeTypeMutex);
                    known = sg_artemisKnownFreeTypeFonts;
                }
                for (void* object : known)
                {
                    if (matched >= 16) break;
                    ArtemisTryReloadFreeTypeFontObject(object, reloadFnAddress, virtualPath.c_str(), reloadPath.c_str(), matched, reloaded, failed);
                }

                DWORD scannedRegions = 0;
                if (reloaded == 0)
                {
                    SYSTEM_INFO sysInfo = {};
                    GetSystemInfo(&sysInfo);
                    BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);
                    BYTE* maxAddress = static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress);
                    while (address < maxAddress && matched < 16)
                    {
                        MEMORY_BASIC_INFORMATION mbi = {};
                        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) break;
                        BYTE* regionBase = static_cast<BYTE*>(mbi.BaseAddress);
                        uintptr_t nextAddressValue = reinterpret_cast<uintptr_t>(regionBase) + mbi.RegionSize;
                        if (nextAddressValue <= reinterpret_cast<uintptr_t>(address)) break;
                        BYTE* nextAddress = reinterpret_cast<BYTE*>(nextAddressValue);
                        if (mbi.State == MEM_COMMIT && ArtemisWritableObjectScanProtect(mbi.Protect) && mbi.RegionSize >= sizeof(uintptr_t))
                        {
                            ArtemisScanFreeTypeReloadRegion(regionBase, nextAddressValue, freeTypeVtable,
                                reloadFnAddress, virtualPath.c_str(), reloadPath.c_str(), matched, reloaded, failed);
                            ++scannedRegions;
                        }
                        address = nextAddress;
                    }
                }
                if (reloaded > 0) sg_artemisActiveReloadPathKey = reloadPathKey;
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis FreeType reload known=%u scannedRegions=%u matched=%u reloaded=%u failed=%u",
                    static_cast<unsigned>(known.size()), scannedRegions, matched, reloaded, failed);
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis FreeType reload requested but unavailable on non-x86 build");
#endif
            }

            void ArtemisTryClearInternalFontCache(const EngineCompatState& state)
            {
                if (!state.allowAggressiveMemoryScan || InterlockedExchange(&sg_artemisCacheClearAttempted, 1) != 0)
                {
                    return;
                }
#if defined(_M_IX86)
                if (!ArtemisResolveCacheClearPlan()) return;
                DWORD firstEntries = 0, firstDetached = 0, secondEntries = 0, secondDetached = 0, glyphEntries = 0, glyphDetached = 0;
                bool firstOk = ArtemisClearHashTarget(sg_artemisCacheClearPlan.first, firstEntries, firstDetached);
                bool secondOk = ArtemisClearHashTarget(sg_artemisCacheClearPlan.second, secondEntries, secondDetached);
                bool glyphOk = ArtemisClearHashTarget(sg_artemisCacheClearPlan.glyph, glyphEntries, glyphDetached);
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis cache-clear first=%u/%u second=%u/%u glyph=%u/%u ok=%d%d%d",
                    firstEntries, firstDetached, secondEntries, secondDetached, glyphEntries, glyphDetached,
                    firstOk ? 1 : 0, secondOk ? 1 : 0, glyphOk ? 1 : 0);
#else
                LogMessage(LogLevel::Info, L"EngineCompat: Artemis aggressive runtime cache clear requested but unavailable on non-x86 build");
#endif
            }
        }

        void ApplyArtemisFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!HasEngine(state, EngineArtemis))
            {
                return;
            }

            std::wstring replacementFontFile = ResolveReplacementFontFile(settings.font);
            const std::wstring replacementFaceName = ResolveReplacementFaceName(settings.font);
            if (replacementFontFile.empty())
            {
                replacementFontFile = ResolveSystemFontFileByFace(replacementFaceName);
                if (!replacementFontFile.empty())
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Artemis resolved replacement face '%s' to '%s'",
                        replacementFaceName.c_str(), replacementFontFile.c_str());
                }
            }

            AddEnginePatchedTextFileRule(L"system\\table\\*.tbl", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"system\\table\\*.txt", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"*.tbl", replacementFontFile.c_str(), replacementFaceName.c_str(), false, false);
            if (!replacementFontFile.empty())
            {
                AddEngineVirtualFileRule(L"font\\*.ttf", replacementFontFile.c_str(), true, true);
                AddEngineVirtualFileRule(L"font\\*.ttc", replacementFontFile.c_str(), true, true);
                AddEngineVirtualFileRule(L"font\\*.otf", replacementFontFile.c_str(), true, true);
            }
            ArtemisTryClearInternalFontCache(state);
            ArtemisTryInvalidateFontAtlases(state);
            ArtemisTryReloadActiveFreeTypeFonts(state, replacementFontFile);
            ArtemisLogObjectScanDiagnostics(state);
            LogMessage(LogLevel::Info, L"EngineCompat: Artemis table patch enabled virtualFont=%d", replacementFontFile.empty() ? 0 : 1);
        }
    }
}
