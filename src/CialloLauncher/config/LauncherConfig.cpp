#include "LauncherConfig.h"

#include "../runtime/LauncherPaths.h"
#include "../../CialloHook/config/config_source.h"
#include "../../RuntimeCore/base/Str.h"
#include "../../RuntimeCore/io/INI.h"
#include "../../RuntimeCore/hook/Hook_API.h"

#include <cwctype>
#include <limits>
#include <stdexcept>

using namespace Rcf::INI;
using namespace Rut::StrX;
using namespace Rut::HookX;

namespace
{
	void AppendIniFallbackWarning(std::vector<std::wstring>& warnings, const wchar_t* section, const wchar_t* key, const std::wstring& rawValue, const std::wstring& fallbackValue)
	{
		std::wstring warning = std::wstring(section) + L"." + key;
		warning += L" = \"";
		warning += rawValue;
		warning += L"\" 无效，已回退为 ";
		warning += fallbackValue;
		warnings.push_back(warning);
	}

	bool TryParseBoolValue(const std::wstring& rawValue, bool& parsedValue)
	{
		std::wstring value = Trim(rawValue);
		if (value.empty())
		{
			return false;
		}
		if (value == L"1" || _wcsicmp(value.c_str(), L"true") == 0 || _wcsicmp(value.c_str(), L"yes") == 0 || _wcsicmp(value.c_str(), L"on") == 0)
		{
			parsedValue = true;
			return true;
		}
		if (value == L"0" || _wcsicmp(value.c_str(), L"false") == 0 || _wcsicmp(value.c_str(), L"no") == 0 || _wcsicmp(value.c_str(), L"off") == 0)
		{
			parsedValue = false;
			return true;
		}
		return false;
	}

	bool TryParseUInt32(const std::wstring& rawValue, uint32_t& parsedValue)
	{
		std::wstring value = Trim(rawValue);
		if (value.empty() || value[0] == L'-')
		{
			return false;
		}
		try
		{
			size_t index = 0;
			unsigned long parsed = std::stoul(value, &index, 0);
			if (index != value.size() || parsed > (std::numeric_limits<uint32_t>::max)())
			{
				return false;
			}
			parsedValue = static_cast<uint32_t>(parsed);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool GetIniBoolOrDefault(INI_File& ini, std::vector<std::wstring>& warnings, const wchar_t* section, const wchar_t* key, bool fallback)
	{
		if (!ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = ini[section][key];
		bool parsedValue = fallback;
		if (TryParseBoolValue(rawValue, parsedValue))
		{
			return parsedValue;
		}
		AppendIniFallbackWarning(warnings, section, key, rawValue, fallback ? L"true" : L"false");
		return fallback;
	}

	uint32_t GetIniUIntOrDefault(INI_File& ini, std::vector<std::wstring>& warnings, const wchar_t* section, const wchar_t* key, uint32_t fallback, uint32_t minValue = 0, uint32_t maxValue = (std::numeric_limits<uint32_t>::max)())
	{
		if (!ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = ini[section][key];
		uint32_t parsedValue = 0;
		if (!TryParseUInt32(rawValue, parsedValue) || parsedValue < minValue || parsedValue > maxValue)
		{
			AppendIniFallbackWarning(warnings, section, key, rawValue, std::to_wstring(fallback));
			return fallback;
		}
		return parsedValue;
	}

	uint32_t GetIniUIntWithAliasOrDefault(
		INI_File& ini,
		std::vector<std::wstring>& warnings,
		const wchar_t* section,
		const wchar_t* primaryKey,
		const wchar_t* aliasKey,
		uint32_t fallback,
		uint32_t minValue = 0,
		uint32_t maxValue = (std::numeric_limits<uint32_t>::max)())
	{
		if (ini.Has(section, primaryKey))
		{
			return GetIniUIntOrDefault(ini, warnings, section, primaryKey, fallback, minValue, maxValue);
		}
		if (aliasKey && ini.Has(section, aliasKey))
		{
			return GetIniUIntOrDefault(ini, warnings, section, aliasKey, fallback, minValue, maxValue);
		}
		return fallback;
	}

	std::wstring GetIniStringOrDefault(INI_File& ini, const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
	{
		return ini.Has(section, key) ? static_cast<std::wstring>(ini[section][key]) : std::wstring(fallback);
	}

	std::wstring BuildIniWarningMessage(const std::vector<std::wstring>& warnings)
	{
		if (warnings.empty())
		{
			return L"";
		}
		std::wstring message = L"配置文件存在无效项，已自动回退到默认值：\n";
		for (const std::wstring& warning : warnings)
		{
			message += L"- ";
			message += warning;
			message += L"\n";
		}
		return message;
	}

	std::wstring DecodeEscapedControlChars(const std::wstring& rawValue)
	{
		std::wstring result;
		result.reserve(rawValue.size());

		for (size_t i = 0; i < rawValue.size(); ++i)
		{
			wchar_t ch = rawValue[i];
			if (ch != L'\\' || i + 1 >= rawValue.size())
			{
				result.push_back(ch);
				continue;
			}

			wchar_t next = rawValue[i + 1];
			switch (next)
			{
			case L'\\':
				result.push_back(L'\\');
				++i;
				break;
			case L'n':
				result.push_back(L'\n');
				++i;
				break;
			case L'r':
				result.push_back(L'\r');
				++i;
				break;
			case L't':
				result.push_back(L'\t');
				++i;
				break;
			default:
				result.push_back(ch);
				break;
			}
		}

		return result;
	}

	std::wstring BuildStartupMessageBody(uint32_t style, const std::wstring& rawAuthor, const std::wstring& rawText)
	{
		std::wstring author = Trim(rawAuthor);
		std::wstring text = Trim(rawText);
		if (style == 1)
		{
			return text;
		}

		std::wstring body;
		if (!author.empty())
		{
			body += L"【补丁作者】\r\n - ";
			body += author;
		}
		if (!text.empty())
		{
			if (!body.empty())
			{
				body += L"\r\n\r\n";
			}
			body += L"【补丁声明】\r\n";
			body += text;
		}
		return body;
	}

	bool LoadBuiltInLauncherConfig(CialloLauncher::LauncherConfig& config)
	{
		config = CialloLauncher::LauncherConfig{};
		CialloHook::ApplyBuiltInLauncherConfig(config);
		config.iniPath = CialloHook::GetBuiltInLauncherConfigSourceLabel();
		config.launcherSection = L"BuiltIn";
		config.targetDllCount = static_cast<uint32_t>(config.targetDllNames.size());

		if (config.patchFolders.empty())
		{
			config.patchFolders.emplace_back(L"patch");
		}
		if (Trim(config.startupMessage.title).empty())
		{
			config.startupMessage.title = L"CialloHook";
		}
		if (config.startupMessage.enable && Trim(config.startupMessage.body).empty())
		{
			config.startupMessage.enable = false;
		}
		if (config.customPakEnable && config.customPakFiles.empty())
		{
			config.customPakEnable = false;
			config.warningMessage = L"Built-in launcher config has CustomPak enabled but no customPakFiles; automatically disabled.";
		}
		if (Trim(config.targetExe).empty())
		{
			MessageBoxW(nullptr,
				L"Built-in loader 配置未设置 targetExe。\n请编辑 src/CialloHook/config/config_source.h 里的 ApplyBuiltInLauncherConfig。",
				L"CialloHook",
				MB_OK | MB_ICONERROR);
			return false;
		}

		InitLogger(L"CialloHook", config.debugMode, config.logToFile, config.logToConsole);
		LogMessage(LogLevel::Info, L"Config loaded: %s", config.iniPath.c_str());
		LogMessage(LogLevel::Info, L"Target executable: %s", config.targetExe.c_str());
		LogMessage(LogLevel::Info, L"Configured dll count: %u", config.targetDllCount);
		LogMessage(LogLevel::Info, L"LocaleEmulator enabled: %d", config.enableLocaleEmulator ? 1 : 0);
		if (!config.warningMessage.empty())
		{
			LogMessage(LogLevel::Warn, L"%s", config.warningMessage.c_str());
		}
		return true;
	}
}

namespace CialloLauncher
{
	bool LoadLauncherConfig(const std::wstring& exeDir, const std::wstring& exeNameNoExt, LauncherConfig& config)
	{
		const CialloHook::ConfigSourceSelection sourceSelection = CialloHook::GetConfigSourceSelection();
		if (sourceSelection.mode == CialloHook::ConfigSourceMode::BuiltIn)
		{
			(void)exeDir;
			(void)exeNameNoExt;
			return LoadBuiltInLauncherConfig(config);
		}

		config = LauncherConfig{};
		config.iniPath = ResolveLauncherIniPath(exeDir, exeNameNoExt);
		if (sourceSelection.iniPathOverride && sourceSelection.iniPathOverride[0] != L'\0')
		{
			config.iniPath = sourceSelection.iniPathOverride;
		}
		if (!FileExists(config.iniPath))
		{
			std::wstring message = L"未找到配置文件：\n";
			message += JoinPath(exeDir, exeNameNoExt + L".ini");
			message += L"\n或\n";
			message += JoinPath(exeDir, L"CialloHook.ini");
			MessageBoxW(nullptr, message.c_str(), L"CialloHook", MB_OK | MB_ICONERROR);
			return false;
		}

		INI_File ini(config.iniPath);
		std::vector<std::wstring> configWarnings;

		config.launcherSection = L"CialloLauncher";
		if (ini.Has(L"CialloHook", L"TargetEXE"))
		{
			config.launcherSection = L"CialloHook";
		}
		else if (!ini.Has(L"CialloLauncher", L"TargetEXE"))
		{
			config.launcherSection = L"CialloHook";
		}

		config.debugMode = GetIniBoolOrDefault(ini, configWarnings, config.launcherSection.c_str(), L"DebugMode", false);
		config.logToFile = GetIniBoolOrDefault(ini, configWarnings, L"Debug", L"LogToFile", false);
		config.logToConsole = GetIniBoolOrDefault(ini, configWarnings, L"Debug", L"LogToConsole", false);

		InitLogger(L"CialloHook", config.debugMode, config.logToFile, config.logToConsole);
		LogMessage(LogLevel::Info, L"Config loaded: %s", config.iniPath.c_str());

		if (config.debugMode)
		{
			wchar_t msg[512];
			swprintf_s(msg, 512, L"[Debug] Config file loaded successfully\nPath: %s", config.iniPath.c_str());
			MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		config.targetExe = GetIniStringOrDefault(ini, config.launcherSection.c_str(), L"TargetEXE", L"");
		config.targetDllCount = GetIniUIntOrDefault(ini, configWarnings, config.launcherSection.c_str(), L"TargetDLLCount", 0, 0, 4096);
		config.startupMessage.enable = GetIniBoolOrDefault(ini, configWarnings, L"StartupMessage", L"Enable", false);
		config.startupMessage.title = Trim(GetIniStringOrDefault(ini, L"StartupMessage", L"Title", L"CialloHook"));
		if (config.startupMessage.title.empty())
		{
			config.startupMessage.title = L"CialloHook";
		}
		const uint32_t startupStyle = GetIniUIntOrDefault(ini, configWarnings, L"StartupMessage", L"Style", 1, 1, 2);
		const std::wstring startupAuthor = GetIniStringOrDefault(ini, L"StartupMessage", L"Author", L"");
		const std::wstring startupText = DecodeEscapedControlChars(GetIniStringOrDefault(ini, L"StartupMessage", L"Text", L""));
		config.startupMessage.body = BuildStartupMessageBody(startupStyle, startupAuthor, startupText);
		if (config.startupMessage.body.empty())
		{
			config.startupMessage.enable = false;
		}
		if (Trim(config.targetExe).empty())
		{
			throw std::runtime_error("TargetEXE is empty");
		}
		config.targetDllNames.reserve(config.targetDllCount);
		for (uint32_t i = 0; i < config.targetDllCount; ++i)
		{
			std::wstring key = std::wstring(L"TargetDLLName_") + std::to_wstring(i);
			config.targetDllNames.emplace_back(GetIniStringOrDefault(ini, config.launcherSection.c_str(), key.c_str(), L""));
		}
		LogMessage(LogLevel::Info, L"Target executable: %s", config.targetExe.c_str());
		LogMessage(LogLevel::Info, L"Configured dll count: %u", config.targetDllCount);

		uint32_t patchFolderCount = GetIniUIntOrDefault(ini, configWarnings, L"FilePatch", L"PatchFolderCount", 0, 0, 4096);
		config.patchFolders.reserve(patchFolderCount > 0 ? patchFolderCount : 1);
		for (uint32_t i = 0; i < patchFolderCount; ++i)
		{
			std::wstring patchKey = std::wstring(L"PatchFolderName_") + std::to_wstring(i);
			std::wstring patchPath = ini.Has(L"FilePatch", patchKey) ? static_cast<std::wstring>(ini[L"FilePatch"][patchKey]) : L"";
			if (!patchPath.empty())
			{
				config.patchFolders.emplace_back(patchPath);
			}
		}
		if (config.patchFolders.empty())
		{
			config.patchFolders.emplace_back(L"patch");
		}

		config.customPakEnable = GetIniBoolOrDefault(ini, configWarnings, L"FilePatch", L"CustomPakEnable", false);
		if (config.customPakEnable)
		{
			uint32_t customPakCount = GetIniUIntOrDefault(ini, configWarnings, L"FilePatch", L"CustomPakCount", 0, 0, 4096);
			for (uint32_t i = 0; i < customPakCount; ++i)
			{
				std::wstring pakKey = std::wstring(L"CustomPakName_") + std::to_wstring(i);
				std::wstring pakPath = ini.Has(L"FilePatch", pakKey) ? static_cast<std::wstring>(ini[L"FilePatch"][pakKey]) : L"";
				if (!pakPath.empty())
				{
					config.customPakFiles.emplace_back(pakPath);
				}
			}
			if (config.customPakFiles.empty())
			{
				config.customPakEnable = false;
				std::wstring message = L"FilePatch.CustomPakEnable 已开启，但未配置有效的 CustomPakName_i，已自动关闭";
				LogMessage(LogLevel::Warn, L"%s", message.c_str());
				MessageBoxW(nullptr, message.c_str(), L"CialloHook", MB_OK | MB_ICONWARNING);
			}
		}

		config.enableLocaleEmulator = GetIniBoolOrDefault(ini, configWarnings, L"LocaleEmulator", L"Enable", false);
		LogMessage(LogLevel::Info, L"LocaleEmulator enabled: %d", config.enableLocaleEmulator ? 1 : 0);
		if (config.debugMode)
		{
			wchar_t msg[256];
			swprintf_s(msg, 256, L"[Debug] Locale Emulator: %s", config.enableLocaleEmulator ? L"Enabled" : L"Disabled");
			MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
		}

		if (config.enableLocaleEmulator)
		{
			LEB& leb = config.localeEmulatorBlock;
			leb.AnsiCodePage = GetIniUIntWithAliasOrDefault(ini, configWarnings, L"LocaleEmulator", L"AnsiCodePage", L"CodePage", 932);
			leb.OemCodePage = GetIniUIntWithAliasOrDefault(ini, configWarnings, L"LocaleEmulator", L"OemCodePage", L"CodePage", leb.AnsiCodePage);
			leb.LocaleID = GetIniUIntOrDefault(ini, configWarnings, L"LocaleEmulator", L"LocaleID", 0x411);
			leb.DefaultCharset = GetIniUIntWithAliasOrDefault(ini, configWarnings, L"LocaleEmulator", L"DefaultCharset", L"Charset", 128, 0, 0xFF);
			leb.HookUILanguageAPI = GetIniUIntOrDefault(ini, configWarnings, L"LocaleEmulator", L"HookUILanguageAPI", 0);
			LogMessage(LogLevel::Info, L"LE config: ACP=%u OEM=%u Locale=0x%X Charset=%u",
				leb.AnsiCodePage, leb.OemCodePage, leb.LocaleID, leb.DefaultCharset);

			if (config.debugMode)
			{
				wchar_t msg[512];
				swprintf_s(msg, 512, L"[Debug] LE Config:\nAnsiCodePage: %d\nOemCodePage: %d\nLocaleID: 0x%X\nDefaultCharset: %d",
					leb.AnsiCodePage, leb.OemCodePage, leb.LocaleID, leb.DefaultCharset);
				MessageBoxW(nullptr, msg, L"CialloHook - Debug", MB_OK | MB_ICONINFORMATION);
			}

			std::wstring timezone = GetIniStringOrDefault(ini, L"LocaleEmulator", L"Timezone", L"Tokyo Standard Time");
			if (Trim(timezone).empty())
			{
				AppendIniFallbackWarning(configWarnings, L"LocaleEmulator", L"Timezone", timezone, L"Tokyo Standard Time");
				timezone = L"Tokyo Standard Time";
			}

			TIME_ZONE_INFORMATION tzi = {};
			if (GetTimeZoneInformation(&tzi) != TIME_ZONE_ID_INVALID)
			{
				leb.Timezone.Bias = -540;
				leb.Timezone.StandardBias = 0;
				leb.Timezone.DaylightBias = 0;
				wcsncpy_s(reinterpret_cast<wchar_t*>(leb.Timezone.StandardName), 32, timezone.c_str(), _TRUNCATE);
				wcsncpy_s(reinterpret_cast<wchar_t*>(leb.Timezone.DaylightName), 32, timezone.c_str(), _TRUNCATE);
			}
			memset(leb.DefaultFaceName, 0, sizeof(leb.DefaultFaceName));
		}

		config.warningMessage = BuildIniWarningMessage(configWarnings);
		if (!config.warningMessage.empty())
		{
			LogMessage(LogLevel::Warn, L"%s", config.warningMessage.c_str());
			MessageBoxW(nullptr, config.warningMessage.c_str(), L"CialloHook", MB_OK | MB_ICONWARNING);
		}
		return true;
	}
}
