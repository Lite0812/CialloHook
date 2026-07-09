#include "engine_compat_file.h"
#include "../../../RuntimeCore/font/font_patcher.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>

#include <cwctype>
#include <vector>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
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

            bool HasFontFileExtension(const std::wstring& path)
            {
                const std::wstring lower = LowerCopy(path);
                return lower.size() >= 4
                    && (lower.rfind(L".ttf") == lower.size() - 4
                        || lower.rfind(L".ttc") == lower.size() - 4
                        || lower.rfind(L".otf") == lower.size() - 4);
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

            std::wstring ResolveLegacyFontFile(const FontSettings& font, const std::wstring& faceName)
            {
                std::wstring fontFile = ResolveReplacementFontFile(font);
                if (!fontFile.empty())
                {
                    return fontFile;
                }
                return ResolveSystemFontFileByFace(faceName);
            }

            bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& bytes)
            {
                HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                LARGE_INTEGER fileSize = {};
                if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > 128ll * 1024ll * 1024ll)
                {
                    CloseHandle(hFile);
                    return false;
                }
                bytes.resize(static_cast<size_t>(fileSize.QuadPart));
                DWORD read = 0;
                bool ok = ReadFile(hFile, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
                CloseHandle(hFile);
                if (!ok)
                {
                    bytes.clear();
                }
                return ok;
            }

            bool PatchLegacyFontBytes(std::vector<uint8_t>& fontBytes, const std::wstring& faceName, DWORD charset)
            {
                if (fontBytes.empty())
                {
                    return false;
                }
                std::vector<BYTE> patched(fontBytes.begin(), fontBytes.end());
                std::vector<BYTE> extracted;
                if (FontPatcher::IsFontCollection(patched.data(), patched.size()))
                {
                    if (!FontPatcher::ExtractFontFromCollectionByName(patched, faceName.c_str(), extracted) || extracted.empty())
                    {
                        fontBytes.clear();
                        return false;
                    }
                    patched.swap(extracted);
                }
                bool changed = false;
                changed = FontPatcher::PatchNameTableFamily(patched, faceName.c_str()) || changed;
                changed = FontPatcher::PatchOS2CodePageRangeForCharset(patched, charset) || changed;
                changed = FontPatcher::PatchVerticalMetrics(patched, 880, -120, 0) || changed;
                fontBytes.assign(patched.begin(), patched.end());
                return changed;
            }

            void AddLegacyFontMemoryRules(const std::vector<uint8_t>& fontBytes)
            {
                if (fontBytes.empty())
                {
                    return;
                }
                AddEngineMemoryFileRule(L"_base\\font\\FontHook.ttf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\FontHook_????.ttf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\FontHook.rft", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\FontHook_????.rft", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\*.ttf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\*.otf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\*.ttc", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"_base\\font\\*.rft", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"font\\*.ttf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"font\\*.otf", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"font\\*.ttc", fontBytes.data(), fontBytes.size(), true, true);
                AddEngineMemoryFileRule(L"font\\*.rft", fontBytes.data(), fontBytes.size(), true, true);
            }
        }

        void ApplyArtemisLegacyFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!HasEngine(state, EngineArtemisLegacy))
            {
                return;
            }

            const std::wstring replacementFaceName = ResolveReplacementFaceName(settings.font);
            const std::wstring replacementFontFile = ResolveLegacyFontFile(settings.font, replacementFaceName);

            AddEnginePatchedTextFileRule(L"system\\*.iet", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"system\\*.txt", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"scenario\\*.iet", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"scenario\\*.txt", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"_base\\*.iet", replacementFontFile.c_str(), replacementFaceName.c_str(), true, true);
            AddEnginePatchedTextFileRule(L"*.iet", replacementFontFile.c_str(), replacementFaceName.c_str(), false, false);

            std::vector<uint8_t> legacyFontBytes;
            if (!replacementFontFile.empty() && ReadFileBytes(replacementFontFile, legacyFontBytes))
            {
                PatchLegacyFontBytes(legacyFontBytes, replacementFaceName, settings.localeEmulator.defaultCharset);
                AddLegacyFontMemoryRules(legacyFontBytes);
            }

            LogMessage(LogLevel::Info, L"EngineCompat: Artemis Legacy file/text compatibility enabled virtualFont=%d face='%s' fontFile='%s' aggressive=%d",
                replacementFontFile.empty() ? 0 : 1,
                replacementFaceName.c_str(),
                replacementFontFile.c_str(),
                state.allowAggressiveMemoryScan ? 1 : 0);
            LogMessage(LogLevel::Info, L"EngineCompat: Artemis Legacy notify: text substitution active for system/*.iet, system/*.txt, *.iet; _base/font/FontHook virtual rules active=%d",
                replacementFontFile.empty() ? 0 : 1);
        }
    }
}
