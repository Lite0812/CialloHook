#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <Windows.h>

#include <vector>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            bool HasMiraiFontFileExtension(const std::wstring& path)
            {
                std::wstring lower = LowerCopy(path);
                return lower.size() >= 4
                    && (lower.rfind(L".ttf") == lower.size() - 4
                        || lower.rfind(L".ttc") == lower.size() - 4
                        || lower.rfind(L".otf") == lower.size() - 4);
            }

            bool MiraiPathExists(const std::wstring& path)
            {
                if (path.empty())
                {
                    return false;
                }
                DWORD attr = GetFileAttributesW(path.c_str());
                return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }

            bool MiraiIsAbsolutePath(const std::wstring& path)
            {
                return (path.size() >= 2 && path[1] == L':')
                    || (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
            }

            std::wstring MiraiWindowsFontsDir()
            {
                wchar_t windowsDir[MAX_PATH] = {};
                if (GetWindowsDirectoryW(windowsDir, MAX_PATH) == 0)
                {
                    return L"C:\\Windows\\Fonts";
                }
                std::wstring fontsDir = windowsDir;
                if (!fontsDir.empty() && fontsDir.back() != L'\\')
                {
                    fontsDir += L'\\';
                }
                fontsDir += L"Fonts";
                return fontsDir;
            }

            std::wstring BuildMiraiFontPathFromRegistryValue(const wchar_t* value)
            {
                if (!value || value[0] == L'\0')
                {
                    return L"";
                }
                std::wstring path = value;
                if (!MiraiIsAbsolutePath(path))
                {
                    std::wstring fullPath = MiraiWindowsFontsDir();
                    if (!fullPath.empty() && fullPath.back() != L'\\')
                    {
                        fullPath += L'\\';
                    }
                    fullPath += path;
                    path = fullPath;
                }
                return HasMiraiFontFileExtension(path) && MiraiPathExists(path) ? path : L"";
            }

            std::wstring NormalizeMiraiFontFaceKey(std::wstring value)
            {
                value = LowerCopy(TrimCopy(value));
                size_t suffix = value.find(L" (");
                if (suffix != std::wstring::npos)
                {
                    value = TrimCopy(value.substr(0, suffix));
                }
                return value;
            }

            std::wstring GetMiraiFileNameLower(const std::wstring& path)
            {
                size_t pos = path.find_last_of(L"\\/");
                std::wstring name = (pos == std::wstring::npos) ? path : path.substr(pos + 1);
                return LowerCopy(name);
            }

            void SearchMiraiSystemFontRegistry(HKEY rootKey, REGSAM wow64Flags, const std::wstring& target,
                std::wstring& bestPath, DWORD& bestScore)
            {
                HKEY hKey = nullptr;
                LONG openStatus = RegOpenKeyExW(rootKey,
                    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                    0, KEY_READ | wow64Flags, &hKey);
                if (openStatus != ERROR_SUCCESS)
                {
                    return;
                }

                for (DWORD index = 0;; ++index)
                {
                    wchar_t valueName[512] = {};
                    wchar_t valueText[MAX_PATH * 2] = {};
                    DWORD nameLen = _countof(valueName);
                    DWORD dataLen = sizeof(valueText);
                    DWORD type = 0;
                    LONG status = RegEnumValueW(hKey, index, valueName, &nameLen, nullptr, &type,
                        reinterpret_cast<LPBYTE>(valueText), &dataLen);
                    if (status == ERROR_NO_MORE_ITEMS)
                    {
                        break;
                    }
                    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
                    {
                        continue;
                    }

                    if (type == REG_EXPAND_SZ)
                    {
                        wchar_t expanded[MAX_PATH * 2] = {};
                        if (ExpandEnvironmentStringsW(valueText, expanded, _countof(expanded)) != 0)
                        {
                            wcscpy_s(valueText, expanded);
                        }
                    }

                    std::wstring filePath = BuildMiraiFontPathFromRegistryValue(valueText);
                    if (filePath.empty())
                    {
                        continue;
                    }

                    std::wstring nameLower = NormalizeMiraiFontFaceKey(valueName);
                    DWORD score = 0;
                    if (nameLower == target)
                    {
                        score = 300;
                    }
                    else if (nameLower.find(target + L" ") == 0)
                    {
                        score = 240;
                    }
                    else if (nameLower.find(target) != std::wstring::npos)
                    {
                        score = 180;
                    }

                    std::wstring fileLower = GetMiraiFileNameLower(filePath);
                    if (fileLower.find(target) != std::wstring::npos)
                    {
                        score += 40;
                    }
                    if ((fileLower.find(L"bold") != std::wstring::npos || fileLower.find(L"italic") != std::wstring::npos) && score > 10)
                    {
                        score -= 10;
                    }

                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestPath = filePath;
                    }
                }

                RegCloseKey(hKey);
            }

            std::wstring ResolveMiraiSystemFontFileByFace(const std::wstring& faceName)
            {
                std::wstring target = NormalizeMiraiFontFaceKey(faceName);
                if (target.empty())
                {
                    return L"";
                }

                std::wstring bestPath;
                DWORD bestScore = 0;
                SearchMiraiSystemFontRegistry(HKEY_CURRENT_USER, 0, target, bestPath, bestScore);
                SearchMiraiSystemFontRegistry(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, target, bestPath, bestScore);
                SearchMiraiSystemFontRegistry(HKEY_LOCAL_MACHINE, 0, target, bestPath, bestScore);
                return bestPath;
            }
        }

        void ApplyMiraiFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!HasEngine(state, EngineMirai))
            {
                return;
            }

            SetFontEngineDataCompatOptions(true, settings.engineCompat.enableLog);

            std::wstring replacementFontFile = ResolveReplacementFontFile(settings.font);
            if (replacementFontFile.empty())
            {
                const std::wstring replacementFaceName = ResolveReplacementFaceName(settings.font);
                replacementFontFile = ResolveMiraiSystemFontFileByFace(replacementFaceName);
                if (!replacementFontFile.empty())
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: Mirai resolved replacement face '%s' to '%s'",
                        replacementFaceName.c_str(), replacementFontFile.c_str());
                }
            }

            if (!replacementFontFile.empty())
            {
                SetEngineWindowsFontsRedirectFile(replacementFontFile.c_str());
                LogMessage(LogLevel::Info, L"EngineCompat: Mirai Windows Fonts redirect enabled");
            }
            else
            {
                LogMessage(LogLevel::Info, L"EngineCompat: Mirai pinned GetFontData enabled; Windows Fonts redirect skipped because replacement font file was not resolved");
            }
        }
    }
}
