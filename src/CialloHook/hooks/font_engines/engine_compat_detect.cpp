#include "engine_compat.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>

namespace CialloHook
{
    namespace EngineCompat
    {
        namespace
        {
            std::wstring NormalizeSlashes(std::wstring value)
            {
                for (wchar_t& ch : value)
                {
                    if (ch == L'/')
                    {
                        ch = L'\\';
                    }
                }
                return value;
            }

            std::wstring NormalizeProfileName(const std::wstring& value)
            {
                std::wstring normalized;
                normalized.reserve(value.size());
                for (wchar_t ch : value)
                {
                    if (ch == L'-' || ch == L'_')
                    {
                        continue;
                    }
                    normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
                }
                return normalized;
            }

            std::wstring GetGameDir()
            {
                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                std::wstring dir = NormalizeSlashes(exePath);
                size_t pos = dir.find_last_of(L'\\');
                if (pos != std::wstring::npos)
                {
                    dir.resize(pos + 1);
                }
                return dir;
            }

            bool FileExists(const std::wstring& path)
            {
                DWORD attr = GetFileAttributesW(path.c_str());
                return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }

            bool DirectoryExists(const std::wstring& path)
            {
                DWORD attr = GetFileAttributesW(path.c_str());
                return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
            }

            bool GlobExists(const std::wstring& pattern)
            {
                WIN32_FIND_DATAW data = {};
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &data);
                if (hFind == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                FindClose(hFind);
                return true;
            }

            bool IsAsciiAlphaDigit8Name(const wchar_t* name)
            {
                if (!name)
                {
                    return false;
                }
                for (size_t i = 0; i < 8; ++i)
                {
                    wchar_t ch = name[i];
                    if (!((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')))
                    {
                        return false;
                    }
                }
                return name[8] == L'\0';
            }

            bool HasEightAlphaDigitLsbFile(const std::wstring& gameDir)
            {
                WIN32_FIND_DATAW data = {};
                HANDLE hFind = FindFirstFileW((gameDir + L"*.lsb").c_str(), &data);
                if (hFind == INVALID_HANDLE_VALUE)
                {
                    hFind = FindFirstFileW((gameDir + L"*.LSB").c_str(), &data);
                    if (hFind == INVALID_HANDLE_VALUE)
                    {
                        return false;
                    }
                }
                do
                {
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    {
                        continue;
                    }
                    std::wstring fileName = data.cFileName;
                    size_t dot = fileName.find_last_of(L'.');
                    if (dot != 8 || fileName.size() != 12)
                    {
                        continue;
                    }
                    if (_wcsicmp(fileName.c_str() + dot, L".lsb") == 0 && IsAsciiAlphaDigit8Name(fileName.substr(0, dot).c_str()))
                    {
                        FindClose(hFind);
                        return true;
                    }
                } while (FindNextFileW(hFind, &data));
                FindClose(hFind);
                return false;
            }

            bool ReadMainModuleBytes(std::string& bytesOut)
            {
                bytesOut.clear();
                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);
                HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                LARGE_INTEGER size = {};
                if (!GetFileSizeEx(hFile, &size) || size.QuadPart <= 0)
                {
                    CloseHandle(hFile);
                    return false;
                }
                constexpr DWORD kMaxScanSize = 16u * 1024u * 1024u;
                DWORD readSize = static_cast<DWORD>(size.QuadPart > kMaxScanSize ? kMaxScanSize : size.QuadPart);
                bytesOut.resize(readSize);
                DWORD bytesRead = 0;
                BOOL ok = ReadFile(hFile, bytesOut.data(), readSize, &bytesRead, nullptr);
                CloseHandle(hFile);
                if (!ok || bytesRead == 0)
                {
                    bytesOut.clear();
                    return false;
                }
                bytesOut.resize(bytesRead);
                return true;
            }

            bool ContainsAsciiNoCase(const std::string& haystack, const char* needle)
            {
                if (!needle || !needle[0] || haystack.empty())
                {
                    return false;
                }
                std::string lowerHaystack = haystack;
                std::transform(lowerHaystack.begin(), lowerHaystack.end(), lowerHaystack.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                std::string lowerNeedle = needle;
                std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                return lowerHaystack.find(lowerNeedle) != std::string::npos;
            }

            bool HasDxLibMarkers(const std::string& exeBytes)
            {
                return ContainsAsciiNoCase(exeBytes, "DxLib")
                    && (ContainsAsciiNoCase(exeBytes, "_FONTSET")
                        || ContainsAsciiNoCase(exeBytes, "FONTSET")
                        || ContainsAsciiNoCase(exeBytes, "GetGlyphOutlineA")
                        || ContainsAsciiNoCase(exeBytes, "CreateFontToHandle"));
            }

            bool HasMajiroMarkers(const std::string& exeBytes)
            {
                return ContainsAsciiNoCase(exeBytes, "MajiroObj")
                    || ContainsAsciiNoCase(exeBytes, "MajiroArcV")
                    || ContainsAsciiNoCase(exeBytes, "MajiroSavV")
                    || ContainsAsciiNoCase(exeBytes, "savedata\\fc_%s_%03dx%03d.fcd");
            }

            bool HasMiraiMarkers(const std::string& exeBytes)
            {
                const bool hasFreetype = ContainsAsciiNoCase(exeBytes, "FT_Init_FreeType") || ContainsAsciiNoCase(exeBytes, "FT_New_Face");
                const bool hasMiraiFontConfig = ContainsAsciiNoCase(exeBytes, "DEVICE_FONT_NAME")
                    || ContainsAsciiNoCase(exeBytes, "getFontFile")
                    || ContainsAsciiNoCase(exeBytes, "FontFile =");
                return hasFreetype && hasMiraiFontConfig;
            }

            bool HasTinkerBellMarkers(const std::string& exeBytes)
            {
                return ContainsAsciiNoCase(exeBytes, "Software\\TinkerBell\\")
                    || ContainsAsciiNoCase(exeBytes, "Cyberworks \"TinkerBell\"")
                    || ContainsAsciiNoCase(exeBytes, "TinkerBell")
                    || ContainsAsciiNoCase(exeBytes, "Tinkerbell");
            }

            bool LooksLikeEscudeRoot(const std::wstring& gameDir)
            {
                std::wstring configPath = gameDir + L"configure.cfg";
                wchar_t company[64] = {};
                wchar_t product[128] = {};
                GetPrivateProfileStringW(L"General", L"Company", L"", company, _countof(company), configPath.c_str());
                GetPrivateProfileStringW(L"General", L"Product", L"", product, _countof(product), configPath.c_str());
                return _wcsicmp(company, L"ESCUDE") == 0 && product[0] != L'\0';
            }

            void AddEngine(EngineCompatState& state, EngineFlag engine, const std::wstring& reason)
            {
                if (engine == EngineUnknown)
                {
                    return;
                }
                if (state.engines == EngineUnknown)
                {
                    state.engines = EngineNone;
                }
                state.engines |= engine;
                if (!reason.empty())
                {
                    state.reasons.push_back(reason);
                }
            }

            EngineFlag EngineFromProfileName(const std::wstring& profile)
            {
                const std::wstring normalized = NormalizeProfileName(profile);
                if (normalized == L"tinkerbell" || normalized == L"tinker") return EngineTinkerBell;
                if (normalized == L"cyberworks" || normalized == L"cyberwork") return EngineCyberworks;
                if (normalized == L"advhd" || normalized == L"adv") return EngineAdvHD;
                if (normalized == L"dxlib") return EngineDxLib;
                if (normalized == L"med") return EngineMED;
                if (normalized == L"majiro") return EngineMajiro;
                if (normalized == L"softpal") return EngineSoftpal;
                if (normalized == L"mirai") return EngineMirai;
                if (normalized == L"artemis") return EngineArtemis;
                if (normalized == L"artemislegacy") return EngineArtemisLegacy;
                if (normalized == L"krkr" || normalized == L"kirikiri") return EngineKrkr;
                if (normalized == L"escude" || normalized == L"escu:de") return EngineEscude;
                if (normalized == L"lsbsafefont" || normalized == L"lsb") return EngineLsbSafeFont;
                if (normalized == L"sensitive" || normalized == L"safe") return EngineTinkerBell;
                return EngineUnknown;
            }

            void ApplyDerivedPolicy(const AppSettings& settings, EngineCompatState& state)
            {
                state.skipWideFontCreation = HasEngine(state, EngineTinkerBell) || HasEngine(state, EngineCyberworks) || HasEngine(state, EngineLsbSafeFont);
                state.selectObjectTrackedOnly = settings.font.compatSelectObjectTrackedOnly || state.skipWideFontCreation;
                state.hideFontCacheFiles = HasEngine(state, EngineDxLib) || HasEngine(state, EngineMED) || HasEngine(state, EngineMajiro) || HasEngine(state, EngineSoftpal) || HasEngine(state, EngineKrkr);
                state.preferPinnedFontData = HasEngine(state, EngineMirai) || HasEngine(state, EngineDxLib) || HasEngine(state, EngineMED) || HasEngine(state, EngineMajiro) || HasEngine(state, EngineSoftpal);
                state.needsWindowsFontsRedirect = HasEngine(state, EngineMirai);
                state.needsVirtualFontFiles = HasEngine(state, EngineArtemis);
                state.allowAggressiveMemoryScan = state.aggressive && settings.engineCompat.artemisAggressiveCacheScan && HasEngine(state, EngineArtemis);
                if (HasEngine(state, EngineEscude))
                {
                    state.skipWideFontCreation = true;
                    state.selectObjectTrackedOnly = true;
                }

                if (HasEngine(state, EngineTinkerBell)) state.primaryProfile = L"tinkerbell";
                else if (HasEngine(state, EngineCyberworks)) state.primaryProfile = L"cyberworks";
                else if (HasEngine(state, EngineAdvHD)) state.primaryProfile = L"advhd";
                else if (HasEngine(state, EngineDxLib)) state.primaryProfile = L"dxlib";
                else if (HasEngine(state, EngineMED)) state.primaryProfile = L"med";
                else if (HasEngine(state, EngineMajiro)) state.primaryProfile = L"majiro";
                else if (HasEngine(state, EngineSoftpal)) state.primaryProfile = L"softpal";
                else if (HasEngine(state, EngineMirai)) state.primaryProfile = L"mirai";
                else if (HasEngine(state, EngineArtemisLegacy)) state.primaryProfile = L"artemis_legacy";
                else if (HasEngine(state, EngineArtemis)) state.primaryProfile = L"artemis";
                else if (HasEngine(state, EngineKrkr)) state.primaryProfile = L"krkr";
                else if (HasEngine(state, EngineEscude)) state.primaryProfile = L"escude";
                else state.primaryProfile = L"unknown";
            }
        }

        EngineCompatState DetectEngineCompatState(const AppSettings& settings)
        {
            EngineCompatState state;
            state.enabled = settings.engineCompat.enable;
            std::wstring mode = NormalizeProfileName(settings.engineCompat.mode);
            if (mode == L"off" || mode == L"disabled" || !settings.engineCompat.enable)
            {
                state.enabled = false;
                state.engines = EngineUnknown;
                state.primaryProfile = L"unknown";
                state.reasons.push_back(L"disabled-by-config");
                return state;
            }
            state.aggressive = mode == L"aggressive";

            const EngineFlag forced = EngineFromProfileName(settings.engineCompat.forceEngine);
            if (forced != EngineUnknown)
            {
                AddEngine(state, forced, L"force-engine=" + settings.engineCompat.forceEngine);
                ApplyDerivedPolicy(settings, state);
                return state;
            }

            const std::wstring gameDir = GetGameDir();
            std::string exeBytes;
            ReadMainModuleBytes(exeBytes);

            const bool tinkerBellRoot = FileExists(gameDir + L"Arc00.dat")
                && FileExists(gameDir + L"Arc01.dat")
                && FileExists(gameDir + L"render.dll")
                && HasTinkerBellMarkers(exeBytes);
            if (settings.engineCompat.enableTinkerBell && tinkerBellRoot)
            {
                AddEngine(state, EngineTinkerBell, L"tinkerbell-root");
            }
            if (settings.engineCompat.enableCyberworks && tinkerBellRoot && ContainsAsciiNoCase(exeBytes, "Cyberworks \"TinkerBell\""))
            {
                AddEngine(state, EngineCyberworks, L"cyberworks-tinkerbell-root");
            }

            const bool dxlibRoot = HasDxLibMarkers(exeBytes)
                && (FileExists(gameDir + L"_FONTSET.MED")
                    || (FileExists(gameDir + L"_CONFIG.MED") && (GlobExists(gameDir + L"*.med") || GlobExists(gameDir + L"*.MED"))));
            if ((settings.engineCompat.enableDxLibFontCache || settings.engineCompat.enableMedFontCache) && dxlibRoot)
            {
                if (settings.engineCompat.enableDxLibFontCache)
                {
                    AddEngine(state, EngineDxLib, L"dxlib-root");
                }
                if (settings.engineCompat.enableMedFontCache)
                {
                    AddEngine(state, EngineMED, L"med-root");
                }
            }

            if (settings.engineCompat.enableMajiroFontCache && GlobExists(gameDir + L"*.arc") && HasMajiroMarkers(exeBytes))
            {
                AddEngine(state, EngineMajiro, L"majiro-root");
            }
            if (settings.engineCompat.enableSoftpalFont && FileExists(gameDir + L"dll\\Pal.dll") && GlobExists(gameDir + L"*.pac"))
            {
                AddEngine(state, EngineSoftpal, L"softpal-root");
            }
            if (settings.engineCompat.enableMiraiFontData
                && FileExists(gameDir + L"arc0.dat")
                && FileExists(gameDir + L"script.dat")
                && FileExists(gameDir + L"Setting.exe")
                && HasMiraiMarkers(exeBytes))
            {
                AddEngine(state, EngineMirai, L"mirai-root");
            }
            if (settings.engineCompat.enableArtemisFont && (GlobExists(gameDir + L"*.pfs") || DirectoryExists(gameDir + L"system\\table")))
            {
                AddEngine(state, EngineArtemis, L"artemis-root");
            }
            if (settings.engineCompat.enableArtemisFont
                && (GlobExists(gameDir + L"system\\*.iet") || FileExists(gameDir + L"system\\msg.iet"))
                && (GlobExists(gameDir + L"*.pfs") || GlobExists(gameDir + L"_base\\font\\*.ttf") || GlobExists(gameDir + L"_base\\font\\*.otf") || GlobExists(gameDir + L"_base\\font\\*.ttc") || GlobExists(gameDir + L"_base\\font\\*.rft")))
            {
                AddEngine(state, EngineArtemisLegacy, L"artemis-legacy-root");
            }
            if (settings.engineCompat.enableKrkrFont && (GlobExists(gameDir + L"*.xp3") || GlobExists(gameDir + L"font\\*.tft")))
            {
                AddEngine(state, EngineKrkr, L"krkr-root");
            }
            if (settings.engineCompat.enableAdvHD && (ContainsAsciiNoCase(exeBytes, "AdvHD") || ContainsAsciiNoCase(exeBytes, "AdvHD.exe")))
            {
                AddEngine(state, EngineAdvHD, L"advhd-marker");
            }
            if (settings.engineCompat.enableEscudeFontConfig && LooksLikeEscudeRoot(gameDir))
            {
                AddEngine(state, EngineEscude, L"escude-root");
            }
            if (HasEightAlphaDigitLsbFile(gameDir))
            {
                AddEngine(state, EngineLsbSafeFont, L"eight-alpha-digit-lsb-root");
            }

            if (state.engines == EngineUnknown)
            {
                state.reasons.push_back(L"no-engine-marker");
            }
            ApplyDerivedPolicy(settings, state);
            return state;
        }
    }
}
