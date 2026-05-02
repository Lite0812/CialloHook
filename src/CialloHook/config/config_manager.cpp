#include "config_manager.h"
#include "config_source.h"

#include "../../RuntimeCore/io/INI.h"
#include "../../RuntimeCore/base/Str.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace Rcf::INI;

namespace CialloHook
{
	struct ConfigReadContext
	{
		INI_File& ini;
		std::vector<std::wstring> warnings;
	};

	static std::wstring MakeFieldName(const wchar_t* section, const wchar_t* key)
	{
		return std::wstring(section) + L"." + key;
	}

	static void AppendFallbackWarning(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, const std::wstring& rawValue, const std::wstring& fallbackValue)
	{
		std::wstring warning = MakeFieldName(section, key);
		warning += L" = \"";
		warning += rawValue;
		warning += L"\" 无效，已回退为 ";
		warning += fallbackValue;
		context.warnings.push_back(warning);
	}

	static void AppendWarning(ConfigReadContext& context, const std::wstring& warning)
	{
		context.warnings.push_back(warning);
	}

	static bool TryParseInt64(const std::wstring& rawValue, long long& parsedValue)
	{
		std::wstring value = Rut::StrX::Trim(rawValue);
		if (value.empty())
		{
			return false;
		}
		try
		{
			size_t index = 0;
			parsedValue = std::stoll(value, &index, 0);
			return index == value.size();
		}
		catch (...)
		{
			return false;
		}
	}

	static bool TryParseUInt64(const std::wstring& rawValue, unsigned long long& parsedValue)
	{
		std::wstring value = Rut::StrX::Trim(rawValue);
		if (value.empty() || value[0] == L'-')
		{
			return false;
		}
		try
		{
			size_t index = 0;
			parsedValue = std::stoull(value, &index, 0);
			return index == value.size();
		}
		catch (...)
		{
			return false;
		}
	}

	static bool TryParseFloat64(const std::wstring& rawValue, double& parsedValue)
	{
		std::wstring value = Rut::StrX::Trim(rawValue);
		if (value.empty())
		{
			return false;
		}
		try
		{
			size_t index = 0;
			parsedValue = std::stod(value, &index);
			return index == value.size() && std::isfinite(parsedValue);
		}
		catch (...)
		{
			return false;
		}
	}

	static bool TryParseBoolValue(const std::wstring& rawValue, bool& parsedValue)
	{
		std::wstring value = Rut::StrX::Trim(rawValue);
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

	static std::wstring GetStringOrDefault(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
	{
		return context.ini.Has(section, key) ? (std::wstring)context.ini[section][key] : std::wstring(fallback);
	}

	static std::wstring DecodeEscapedControlChars(const std::wstring& rawValue)
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

	static std::wstring UnwrapQuotedRuleValue(const std::wstring& rawValue)
	{
		if (rawValue.size() < 2 || rawValue.front() != L'"' || rawValue.back() != L'"')
		{
			return rawValue;
		}

		std::wstring result;
		result.reserve(rawValue.size() - 2);
		for (size_t i = 1; i + 1 < rawValue.size(); ++i)
		{
			wchar_t ch = rawValue[i];
			if (ch == L'"' && i + 2 < rawValue.size() && rawValue[i + 1] == L'"')
			{
				result.push_back(L'"');
				++i;
				continue;
			}
			result.push_back(ch);
		}
		return result;
	}

	static std::wstring DecodeRuleTextValue(const std::wstring& rawValue)
	{
		return DecodeEscapedControlChars(UnwrapQuotedRuleValue(rawValue));
	}

	static bool GetBoolOrDefault(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, bool fallback)
	{
		if (!context.ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = (std::wstring)context.ini[section][key];
		bool parsedValue = fallback;
		if (TryParseBoolValue(rawValue, parsedValue))
		{
			return parsedValue;
		}
		AppendFallbackWarning(context, section, key, rawValue, fallback ? L"true" : L"false");
		return fallback;
	}

	static bool GetBoolWithAliasOrDefault(
		ConfigReadContext& context,
		const wchar_t* section,
		const wchar_t* primaryKey,
		const wchar_t* aliasKey,
		bool fallback)
	{
		if (context.ini.Has(section, primaryKey))
		{
			return GetBoolOrDefault(context, section, primaryKey, fallback);
		}
		if (aliasKey && context.ini.Has(section, aliasKey))
		{
			return GetBoolOrDefault(context, section, aliasKey, fallback);
		}
		return fallback;
	}

	static int GetIntOrDefault(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, int fallback, int minValue = (std::numeric_limits<int>::min)(), int maxValue = (std::numeric_limits<int>::max)())
	{
		if (!context.ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = (std::wstring)context.ini[section][key];
		long long parsedValue = 0;
		if (!TryParseInt64(rawValue, parsedValue) || parsedValue < minValue || parsedValue > maxValue)
		{
			AppendFallbackWarning(context, section, key, rawValue, std::to_wstring(fallback));
			return fallback;
		}
		return static_cast<int>(parsedValue);
	}

	static uint32_t GetUIntOrDefault(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, uint32_t fallback, uint32_t minValue = 0, uint32_t maxValue = (std::numeric_limits<uint32_t>::max)())
	{
		if (!context.ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = (std::wstring)context.ini[section][key];
		unsigned long long parsedValue = 0;
		if (!TryParseUInt64(rawValue, parsedValue) || parsedValue < minValue || parsedValue > maxValue)
		{
			AppendFallbackWarning(context, section, key, rawValue, std::to_wstring(fallback));
			return fallback;
		}
		return static_cast<uint32_t>(parsedValue);
	}

	static uint32_t GetUIntWithAliasOrDefault(
		ConfigReadContext& context,
		const wchar_t* section,
		const wchar_t* primaryKey,
		const wchar_t* aliasKey,
		uint32_t fallback,
		uint32_t minValue = 0,
		uint32_t maxValue = (std::numeric_limits<uint32_t>::max)())
	{
		if (context.ini.Has(section, primaryKey))
		{
			return GetUIntOrDefault(context, section, primaryKey, fallback, minValue, maxValue);
		}
		if (aliasKey && context.ini.Has(section, aliasKey))
		{
			return GetUIntOrDefault(context, section, aliasKey, fallback, minValue, maxValue);
		}
		return fallback;
	}

	static float GetFloatOrDefault(ConfigReadContext& context, const wchar_t* section, const wchar_t* key, float fallback, float minValue = -FLT_MAX, float maxValue = FLT_MAX)
	{
		if (!context.ini.Has(section, key))
		{
			return fallback;
		}
		std::wstring rawValue = (std::wstring)context.ini[section][key];
		double parsedValue = 0.0;
		if (!TryParseFloat64(rawValue, parsedValue) || parsedValue < minValue || parsedValue > maxValue)
		{
			AppendFallbackWarning(context, section, key, rawValue, std::to_wstring(fallback));
			return fallback;
		}
		return static_cast<float>(parsedValue);
	}

	static std::vector<std::wstring> GetIndexedList(ConfigReadContext& context, const wchar_t* section, const wchar_t* countKey, const wchar_t* itemPrefix)
	{
		std::vector<std::wstring> result;
		if (!context.ini.Has(section, countKey))
		{
			return result;
		}

		int count = GetIntOrDefault(context, section, countKey, 0, 0, 4096);
		for (int i = 0; i < count; ++i)
		{
			std::wstring key = std::wstring(itemPrefix) + std::to_wstring(i);
			if (!context.ini.Has(section, key))
			{
				continue;
			}
			std::wstring value = (std::wstring)context.ini[section][key];
			if (!value.empty())
			{
				result.push_back(value);
			}
		}
		return result;
	}

	static size_t FindRedirectRuleSeparator(const std::wstring& value)
	{
		for (size_t i = 0; i < value.size(); ++i)
		{
			if (value[i] != L':')
			{
				continue;
			}
			if (i == 1 && iswalpha(static_cast<wint_t>(value[0])) != 0)
			{
				continue;
			}
			return i;
		}
		return std::wstring::npos;
	}

	static std::vector<std::pair<std::wstring, std::wstring>> GetIndexedRedirectRuleList(
		ConfigReadContext& context,
		const wchar_t* section,
		const wchar_t* countKey,
		const wchar_t* itemPrefix)
	{
		std::vector<std::pair<std::wstring, std::wstring>> result;
		if (!context.ini.Has(section, countKey))
		{
			return result;
		}

		int count = GetIntOrDefault(context, section, countKey, 0, 0, 4096);
		for (int i = 0; i < count; ++i)
		{
			std::wstring key = std::wstring(itemPrefix) + std::to_wstring(i);
			if (!context.ini.Has(section, key))
			{
				continue;
			}

			std::wstring rawValue = static_cast<std::wstring>(context.ini[section][key]);
			std::wstring value = Rut::StrX::Trim(rawValue);
			if (value.empty())
			{
				continue;
			}

			size_t separator = FindRedirectRuleSeparator(value);
			if (separator == std::wstring::npos)
			{
				AppendWarning(context, MakeFieldName(section, key.c_str()) + L" 缺少 \":\" 分隔符，已跳过");
				continue;
			}

			std::wstring source = Rut::StrX::Trim(value.substr(0, separator));
			std::wstring target = Rut::StrX::Trim(value.substr(separator + 1));
			if (source.empty() || target.empty())
			{
				AppendWarning(context, MakeFieldName(section, key.c_str()) + L" 的原路径或目标目录为空，已跳过");
				continue;
			}

			result.emplace_back(std::move(source), std::move(target));
		}
		return result;
	}

	static std::vector<FontRedirectRule> GetIndexedFontRedirectRules(
		ConfigReadContext& context,
		const wchar_t* section,
		const wchar_t* countKey)
	{
		std::vector<FontRedirectRule> result;
		if (!context.ini.Has(section, countKey))
		{
			return result;
		}

		int count = GetIntOrDefault(context, section, countKey, 0, 0, 4096);
		for (int i = 0; i < count; ++i)
		{
			std::wstring index = std::to_wstring(i);
			std::wstring sourceKey = L"RedirectFromFont_" + index;
			std::wstring targetKey = L"RedirectToFont_" + index;
			std::wstring targetNameKey = L"RedirectToFontName_" + index;

			if (!context.ini.Has(section, sourceKey) || !context.ini.Has(section, targetKey))
			{
				AppendWarning(context, MakeFieldName(section, countKey) + L" 中的第 " + index + L" 条缺少 RedirectFromFont/RedirectToFont，已跳过");
				continue;
			}

			FontRedirectRule rule;
			rule.sourceFont = Rut::StrX::Trim((std::wstring)context.ini[section][sourceKey]);
			rule.targetFont = Rut::StrX::Trim((std::wstring)context.ini[section][targetKey]);
			rule.targetFontNameOverride = context.ini.Has(section, targetNameKey)
				? Rut::StrX::Trim((std::wstring)context.ini[section][targetNameKey])
				: L"";

			if (rule.sourceFont.empty() || rule.targetFont.empty())
			{
				AppendWarning(context, MakeFieldName(section, sourceKey.c_str()) + L" 或 " + MakeFieldName(section, targetKey.c_str()) + L" 为空，已跳过");
				continue;
			}

			result.push_back(std::move(rule));
		}
		return result;
	}

	static std::string BuildWarningMessage(const std::vector<std::wstring>& warnings)
	{
		if (warnings.empty())
		{
			return std::string();
		}
		std::wstring text = L"配置文件存在无效项，已自动回退到默认值：\n";
		for (const std::wstring& warning : warnings)
		{
			text += L"- ";
			text += warning;
			text += L"\n";
		}
		return Rut::StrX::WStrToStr(text, 65001);
	}

	static ConfigSourceSelection GetEffectiveConfigSourceSelection()
	{
		return GetConfigSourceSelection();
	}

	static std::wstring ResolveEffectiveIniPath(const std::wstring& iniPath)
	{
		const ConfigSourceSelection selection = GetEffectiveConfigSourceSelection();
		if (selection.iniPathOverride && selection.iniPathOverride[0] != L'\0')
		{
			return selection.iniPathOverride;
		}
		return iniPath;
	}

	std::wstring ConfigManager::DescribeSource(const std::wstring& iniPath)
	{
		const ConfigSourceSelection selection = GetEffectiveConfigSourceSelection();
		if (selection.mode == ConfigSourceMode::BuiltIn)
		{
			return GetBuiltInConfigSourceLabel();
		}
		return ResolveEffectiveIniPath(iniPath);
	}

	bool ConfigManager::Load(const std::wstring& iniPath, AppSettings& settings, std::string& errorMessage, std::string* warningMessage)
	{
		const ConfigSourceSelection selection = GetEffectiveConfigSourceSelection();
		if (selection.mode == ConfigSourceMode::BuiltIn)
		{
			settings = AppSettings{};
			ApplyBuiltInConfig(settings);
			errorMessage.clear();
			if (warningMessage)
			{
				warningMessage->clear();
			}
			return true;
		}

		try
		{
			INI_File ini(ResolveEffectiveIniPath(iniPath));
			ConfigReadContext context{ ini };

			const wchar_t* fontSection = L"CialloHook";
			settings.font.charset = GetUIntOrDefault(context, fontSection, L"Charset", 0x00, 0x00, 0xFF);
			settings.font.enableCharsetSpoof = GetBoolWithAliasOrDefault(context, fontSection, L"EnableCharsetSpoof", L"EnableCodepageSpoof", false);
			settings.font.spoofFromCharset = GetUIntOrDefault(context, fontSection, L"SpoofFromCharset", 0x80, 0x00, 0xFF);
			settings.font.spoofToCharset = GetUIntOrDefault(context, fontSection, L"SpoofToCharset", 0x01, 0x00, 0xFF);
			settings.font.font = GetStringOrDefault(context, fontSection, L"Font", L"");
			settings.font.fontNameOverride = GetStringOrDefault(context, fontSection, L"FontName", L"");
			settings.font.skipFonts = GetIndexedList(context, fontSection, L"SkipFontCount", L"SkipFontName_");
			settings.font.redirectRules = GetIndexedFontRedirectRules(context, fontSection, L"RedirectFontCount");
			const bool hookGroupCreate = GetBoolOrDefault(context, fontSection, L"HookGroupCreate", true);
			const bool hookGroupEnumerate = GetBoolOrDefault(context, fontSection, L"HookGroupEnumerate", true);
			const bool hookGroupMetrics = GetBoolOrDefault(context, fontSection, L"HookGroupMetrics", true);
			const bool hookGroupResource = GetBoolOrDefault(context, fontSection, L"HookGroupResource", true);
			const bool hookGroupModern = GetBoolOrDefault(context, fontSection, L"HookGroupModern", true);
			const bool hookGroupLateLoad = GetBoolOrDefault(context, fontSection, L"HookGroupLateLoad", true);

			settings.font.hookCreateFontA = GetBoolOrDefault(context, fontSection, L"HookCreateFontA", hookGroupCreate);
			settings.font.hookCreateFontIndirectA = GetBoolOrDefault(context, fontSection, L"HookCreateFontIndirectA", hookGroupCreate);
			settings.font.hookCreateFontW = GetBoolOrDefault(context, fontSection, L"HookCreateFontW", hookGroupCreate);
			settings.font.hookCreateFontIndirectW = GetBoolOrDefault(context, fontSection, L"HookCreateFontIndirectW", hookGroupCreate);
			settings.font.hookEnumFontFamiliesExA = GetBoolOrDefault(context, fontSection, L"HookEnumFontFamiliesExA", hookGroupEnumerate);
			settings.font.hookEnumFontFamiliesExW = GetBoolOrDefault(context, fontSection, L"HookEnumFontFamiliesExW", hookGroupEnumerate);
			settings.font.hookCreateFontIndirectExA = GetBoolOrDefault(context, fontSection, L"HookCreateFontIndirectExA", hookGroupCreate);
			settings.font.hookCreateFontIndirectExW = GetBoolOrDefault(context, fontSection, L"HookCreateFontIndirectExW", hookGroupCreate);
			settings.font.hookGetObjectA = GetBoolOrDefault(context, fontSection, L"HookGetObjectA", hookGroupMetrics);
			settings.font.hookGetObjectW = GetBoolOrDefault(context, fontSection, L"HookGetObjectW", hookGroupMetrics);
			settings.font.hookGetTextFaceA = GetBoolOrDefault(context, fontSection, L"HookGetTextFaceA", hookGroupMetrics);
			settings.font.hookGetTextFaceW = GetBoolOrDefault(context, fontSection, L"HookGetTextFaceW", hookGroupMetrics);
			settings.font.hookGetTextMetricsA = GetBoolOrDefault(context, fontSection, L"HookGetTextMetricsA", hookGroupMetrics);
			settings.font.hookGetTextMetricsW = GetBoolOrDefault(context, fontSection, L"HookGetTextMetricsW", hookGroupMetrics);
			settings.font.hookGetCharABCWidthsA = GetBoolOrDefault(context, fontSection, L"HookGetCharABCWidthsA", hookGroupMetrics);
			settings.font.hookGetCharABCWidthsW = GetBoolOrDefault(context, fontSection, L"HookGetCharABCWidthsW", hookGroupMetrics);
			settings.font.hookGetCharABCWidthsFloatA = GetBoolOrDefault(context, fontSection, L"HookGetCharABCWidthsFloatA", hookGroupMetrics);
			settings.font.hookGetCharABCWidthsFloatW = GetBoolOrDefault(context, fontSection, L"HookGetCharABCWidthsFloatW", hookGroupMetrics);
			settings.font.hookGetCharWidthA = GetBoolOrDefault(context, fontSection, L"HookGetCharWidthA", hookGroupMetrics);
			settings.font.hookGetCharWidthW = GetBoolOrDefault(context, fontSection, L"HookGetCharWidthW", hookGroupMetrics);
			settings.font.hookGetCharWidth32A = GetBoolOrDefault(context, fontSection, L"HookGetCharWidth32A", hookGroupMetrics);
			settings.font.hookGetCharWidth32W = GetBoolOrDefault(context, fontSection, L"HookGetCharWidth32W", hookGroupMetrics);
			settings.font.hookGetKerningPairsA = GetBoolOrDefault(context, fontSection, L"HookGetKerningPairsA", hookGroupMetrics);
			settings.font.hookGetKerningPairsW = GetBoolOrDefault(context, fontSection, L"HookGetKerningPairsW", hookGroupMetrics);
			settings.font.hookGetOutlineTextMetricsA = GetBoolOrDefault(context, fontSection, L"HookGetOutlineTextMetricsA", hookGroupMetrics);
			settings.font.hookGetOutlineTextMetricsW = GetBoolOrDefault(context, fontSection, L"HookGetOutlineTextMetricsW", hookGroupMetrics);
			settings.font.hookAddFontResourceA = GetBoolOrDefault(context, fontSection, L"HookAddFontResourceA", hookGroupResource);
			settings.font.hookAddFontResourceW = GetBoolOrDefault(context, fontSection, L"HookAddFontResourceW", hookGroupResource);
			settings.font.hookAddFontResourceExA = GetBoolOrDefault(context, fontSection, L"HookAddFontResourceExA", hookGroupResource);
			settings.font.hookAddFontMemResourceEx = GetBoolOrDefault(context, fontSection, L"HookAddFontMemResourceEx", hookGroupResource);
			settings.font.hookRemoveFontResourceA = GetBoolOrDefault(context, fontSection, L"HookRemoveFontResourceA", hookGroupResource);
			settings.font.hookRemoveFontResourceW = GetBoolOrDefault(context, fontSection, L"HookRemoveFontResourceW", hookGroupResource);
			settings.font.hookRemoveFontResourceExA = GetBoolOrDefault(context, fontSection, L"HookRemoveFontResourceExA", hookGroupResource);
			settings.font.hookRemoveFontMemResourceEx = GetBoolOrDefault(context, fontSection, L"HookRemoveFontMemResourceEx", hookGroupResource);
			settings.font.hookEnumFontsA = GetBoolOrDefault(context, fontSection, L"HookEnumFontsA", hookGroupEnumerate);
			settings.font.hookEnumFontsW = GetBoolOrDefault(context, fontSection, L"HookEnumFontsW", hookGroupEnumerate);
			settings.font.hookEnumFontFamiliesA = GetBoolOrDefault(context, fontSection, L"HookEnumFontFamiliesA", hookGroupEnumerate);
			settings.font.hookEnumFontFamiliesW = GetBoolOrDefault(context, fontSection, L"HookEnumFontFamiliesW", hookGroupEnumerate);
			settings.font.hookGetCharWidthFloatA = GetBoolOrDefault(context, fontSection, L"HookGetCharWidthFloatA", hookGroupMetrics);
			settings.font.hookGetCharWidthFloatW = GetBoolOrDefault(context, fontSection, L"HookGetCharWidthFloatW", hookGroupMetrics);
			settings.font.hookGetCharWidthI = GetBoolOrDefault(context, fontSection, L"HookGetCharWidthI", hookGroupMetrics);
			settings.font.hookGetCharABCWidthsI = GetBoolOrDefault(context, fontSection, L"HookGetCharABCWidthsI", hookGroupMetrics);
			settings.font.hookGetTextExtentPointI = GetBoolOrDefault(context, fontSection, L"HookGetTextExtentPointI", hookGroupMetrics);
			settings.font.hookGetTextExtentExPointI = GetBoolOrDefault(context, fontSection, L"HookGetTextExtentExPointI", hookGroupMetrics);
			settings.font.hookGetFontData = GetBoolOrDefault(context, fontSection, L"HookGetFontData", hookGroupMetrics);
			settings.font.hookGetFontLanguageInfo = GetBoolOrDefault(context, fontSection, L"HookGetFontLanguageInfo", hookGroupMetrics);
			settings.font.hookGetFontUnicodeRanges = GetBoolOrDefault(context, fontSection, L"HookGetFontUnicodeRanges", hookGroupMetrics);
			settings.font.hookDWriteCreateFactory = GetBoolOrDefault(context, fontSection, L"HookDWriteCreateFactory", hookGroupModern);
			settings.font.hookGdipCreateFontFamilyFromName = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFontFamilyFromName", hookGroupModern);
			settings.font.hookGdipCreateFontFromLogfontW = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFontFromLogfontW", hookGroupModern);
			settings.font.hookGdipCreateFontFromLogfontA = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFontFromLogfontA", hookGroupModern);
			settings.font.hookGdipCreateFontFromHFONT = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFontFromHFONT", false);
			settings.font.hookGdipCreateFontFromDC = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFontFromDC", hookGroupModern);
			settings.font.hookGdipCreateFont = GetBoolOrDefault(context, fontSection, L"HookGdipCreateFont", hookGroupModern);
			settings.font.hookGdipDrawString = GetBoolOrDefault(context, fontSection, L"HookGdipDrawString", hookGroupModern);
			settings.font.hookGdipDrawDriverString = GetBoolOrDefault(context, fontSection, L"HookGdipDrawDriverString", hookGroupModern);
			settings.font.hookGdipMeasureString = GetBoolOrDefault(context, fontSection, L"HookGdipMeasureString", hookGroupModern);
			settings.font.hookGdipMeasureCharacterRanges = GetBoolOrDefault(context, fontSection, L"HookGdipMeasureCharacterRanges", hookGroupModern);
			settings.font.hookGdipMeasureDriverString = GetBoolOrDefault(context, fontSection, L"HookGdipMeasureDriverString", hookGroupModern);
			settings.font.hookLoadLibraryW = GetBoolOrDefault(context, fontSection, L"HookLoadLibraryW", hookGroupLateLoad);
			settings.font.hookLoadLibraryExW = GetBoolOrDefault(context, fontSection, L"HookLoadLibraryExW", hookGroupLateLoad);
			settings.font.unlockFontSelection = GetBoolOrDefault(context, fontSection, L"UnlockFontSelection", true);
			if (context.ini.Has(fontSection, L"EnableCnJpMap"))
			{
				settings.font.enableCnJpMap = GetBoolOrDefault(context, fontSection, L"EnableCnJpMap", false);
			}
			else
			{
				settings.font.enableCnJpMap = GetBoolOrDefault(context, L"TextReplace", L"EnableCnJpMap", false);
			}
			if (context.ini.Has(fontSection, L"CnJpMapVerboseLog"))
			{
				settings.font.cnJpMapVerboseLog = GetBoolOrDefault(context, fontSection, L"CnJpMapVerboseLog", false);
			}
			else
			{
				settings.font.cnJpMapVerboseLog = GetBoolOrDefault(context, L"TextReplace", L"CnJpMapVerboseLog", false);
			}
			if (context.ini.Has(fontSection, L"CnJpMapJson"))
			{
				settings.font.cnJpMapJson = GetStringOrDefault(context, fontSection, L"CnJpMapJson", L"subs_cn_jp.json");
			}
			else
			{
				settings.font.cnJpMapJson = GetStringOrDefault(context, L"TextReplace", L"CnJpMapJson", L"subs_cn_jp.json");
			}
			if (context.ini.Has(fontSection, L"CnJpMapEncoding"))
			{
				settings.font.cnJpMapEncoding = GetUIntOrDefault(context, fontSection, L"CnJpMapEncoding", 0);
			}
			else
			{
				settings.font.cnJpMapEncoding = GetUIntOrDefault(context, L"TextReplace", L"CnJpMapEncoding", 0);
			}
			uint32_t defaultCnJpReadEncoding = settings.font.cnJpMapEncoding;
			if (context.ini.Has(fontSection, L"CnJpMapReadEncoding") || context.ini.Has(fontSection, L"CnJpMapWriteEncoding"))
			{
				settings.font.cnJpMapReadEncoding = GetUIntWithAliasOrDefault(
					context,
					fontSection,
					L"CnJpMapReadEncoding",
					L"CnJpMapWriteEncoding",
					defaultCnJpReadEncoding);
			}
			else
			{
				settings.font.cnJpMapReadEncoding = GetUIntWithAliasOrDefault(
					context,
					L"TextReplace",
					L"CnJpMapReadEncoding",
					L"CnJpMapWriteEncoding",
					defaultCnJpReadEncoding);
			}
			settings.font.fontHeight = GetIntOrDefault(context, fontSection, L"FontHeight", 0);
			settings.font.fontWidth = GetIntOrDefault(context, fontSection, L"FontWidth", 0);
			settings.font.fontWeight = GetIntOrDefault(context, fontSection, L"FontWeight", 0);
			settings.font.fontScale = GetFloatOrDefault(context, fontSection, L"FontScale", 1.0f, 0.01f, 100.0f);
			settings.font.fontSpacingScale = GetFloatOrDefault(context, fontSection, L"FontSpacingScale", 1.0f, 0.01f, 100.0f);
			settings.font.glyphAspectRatio = GetFloatOrDefault(context, fontSection, L"GlyphAspectRatio", 1.0f, 0.01f, 100.0f);
			settings.font.glyphOffsetX = GetIntOrDefault(context, fontSection, L"GlyphOffsetX", 0);
			settings.font.glyphOffsetY = GetIntOrDefault(context, fontSection, L"GlyphOffsetY", 0);
			settings.font.metricsOffsetLeft = GetIntOrDefault(context, fontSection, L"MetricsOffsetLeft", 0);
			settings.font.metricsOffsetRight = GetIntOrDefault(context, fontSection, L"MetricsOffsetRight", 0);
			settings.font.metricsOffsetTop = GetIntOrDefault(context, fontSection, L"MetricsOffsetTop", 0);
			settings.font.metricsOffsetBottom = GetIntOrDefault(context, fontSection, L"MetricsOffsetBottom", 0);

			settings.textReplace.rules.clear();
			settings.textReplace.encoding = GetUIntOrDefault(context, L"TextReplace", L"Encoding", 0);
			settings.textReplace.readEncoding = GetUIntOrDefault(context, L"TextReplace", L"ReadEncoding", settings.textReplace.encoding);
			uint32_t defaultTextWriteEncoding = settings.textReplace.readEncoding == 0
				? settings.textReplace.encoding
				: settings.textReplace.readEncoding;
			settings.textReplace.writeEncoding = GetUIntOrDefault(context, L"TextReplace", L"WriteEncoding", defaultTextWriteEncoding);
			settings.textReplace.enableVerboseLog = GetBoolOrDefault(context, L"TextReplace", L"EnableVerboseLog", false);
			if (settings.font.cnJpMapReadEncoding == 0)
			{
				settings.font.cnJpMapReadEncoding = settings.textReplace.readEncoding;
			}
			if (context.ini.Has(L"TextReplace", L"ReplaceCount"))
			{
				int replaceCount = GetIntOrDefault(context, L"TextReplace", L"ReplaceCount", 0, 0, 4096);
				for (int i = 0; i < replaceCount; i++)
				{
					std::wstring originalKey = L"Original_" + std::to_wstring(i);
					std::wstring replacementKey = L"Replacement_" + std::to_wstring(i);
					if (context.ini.Has(L"TextReplace", originalKey) && context.ini.Has(L"TextReplace", replacementKey))
					{
						std::wstring original = DecodeRuleTextValue((std::wstring)context.ini[L"TextReplace"][originalKey]);
						std::wstring replacement = DecodeRuleTextValue((std::wstring)context.ini[L"TextReplace"][replacementKey]);
						settings.textReplace.rules.emplace_back(original, replacement);
					}
				}
			}
			if (settings.font.enableCnJpMap && Rut::StrX::Trim(settings.font.cnJpMapJson).empty())
			{
				settings.font.enableCnJpMap = false;
				settings.font.cnJpMapJson = L"subs_cn_jp.json";
				AppendWarning(context, L"CialloHook.EnableCnJpMap 已开启，但 CnJpMapJson 为空，已自动关闭");
			}

			settings.textReplace.hookTextOutA = GetBoolOrDefault(context, L"TextReplace", L"HookTextOutA", true);
			settings.textReplace.hookTextOutW = GetBoolOrDefault(context, L"TextReplace", L"HookTextOutW", true);
			settings.textReplace.hookExtTextOutA = GetBoolOrDefault(context, L"TextReplace", L"HookExtTextOutA", true);
			settings.textReplace.hookExtTextOutW = GetBoolOrDefault(context, L"TextReplace", L"HookExtTextOutW", true);
			settings.textReplace.hookDrawTextA = GetBoolOrDefault(context, L"TextReplace", L"HookDrawTextA", true);
			settings.textReplace.hookDrawTextW = GetBoolOrDefault(context, L"TextReplace", L"HookDrawTextW", true);
			settings.textReplace.hookDrawTextExA = GetBoolOrDefault(context, L"TextReplace", L"HookDrawTextExA", true);
			settings.textReplace.hookDrawTextExW = GetBoolOrDefault(context, L"TextReplace", L"HookDrawTextExW", true);
			settings.textReplace.hookPolyTextOutA = GetBoolOrDefault(context, L"TextReplace", L"HookPolyTextOutA", true);
			settings.textReplace.hookPolyTextOutW = GetBoolOrDefault(context, L"TextReplace", L"HookPolyTextOutW", true);
			settings.textReplace.hookTabbedTextOutA = GetBoolOrDefault(context, L"TextReplace", L"HookTabbedTextOutA", true);
			settings.textReplace.hookTabbedTextOutW = GetBoolOrDefault(context, L"TextReplace", L"HookTabbedTextOutW", true);
			settings.textReplace.hookGetTabbedTextExtentA = GetBoolOrDefault(context, L"TextReplace", L"HookGetTabbedTextExtentA", true);
			settings.textReplace.hookGetTabbedTextExtentW = GetBoolOrDefault(context, L"TextReplace", L"HookGetTabbedTextExtentW", true);
			settings.textReplace.hookGetTextExtentPoint32A = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentPoint32A", true);
			settings.textReplace.hookGetTextExtentPoint32W = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentPoint32W", true);
			settings.textReplace.hookGetTextExtentExPointA = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentExPointA", true);
			settings.textReplace.hookGetTextExtentExPointW = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentExPointW", true);
			settings.textReplace.hookGetTextExtentPointA = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentPointA", true);
			settings.textReplace.hookGetTextExtentPointW = GetBoolOrDefault(context, L"TextReplace", L"HookGetTextExtentPointW", true);
			settings.textReplace.hookGetCharacterPlacementA = GetBoolOrDefault(context, L"TextReplace", L"HookGetCharacterPlacementA", true);
			settings.textReplace.hookGetCharacterPlacementW = GetBoolOrDefault(context, L"TextReplace", L"HookGetCharacterPlacementW", true);
			settings.textReplace.hookGetGlyphIndicesA = GetBoolOrDefault(context, L"TextReplace", L"HookGetGlyphIndicesA", true);
			settings.textReplace.hookGetGlyphIndicesW = GetBoolOrDefault(context, L"TextReplace", L"HookGetGlyphIndicesW", true);
			settings.textReplace.hookGetGlyphOutlineA = GetBoolOrDefault(context, L"TextReplace", L"HookGetGlyphOutlineA", true);
			settings.textReplace.hookGetGlyphOutlineW = GetBoolOrDefault(context, L"TextReplace", L"HookGetGlyphOutlineW", true);

			settings.windowTitle.rules.clear();
			settings.windowTitle.titleMode = GetIntOrDefault(context, L"WindowTitle", L"TitleMode",
				GetIntOrDefault(context, L"Window", L"TitleMode", 2, 0, 2), 0, 2);
			settings.windowTitle.encoding = GetUIntOrDefault(context, L"WindowTitle", L"Encoding", 0);
			settings.windowTitle.readEncoding = GetUIntOrDefault(context, L"WindowTitle", L"ReadEncoding", settings.windowTitle.encoding);
			uint32_t defaultWindowWriteEncoding = settings.windowTitle.readEncoding == 0
				? settings.windowTitle.encoding
				: settings.windowTitle.readEncoding;
			settings.windowTitle.writeEncoding = GetUIntOrDefault(context, L"WindowTitle", L"WriteEncoding", defaultWindowWriteEncoding);
			settings.windowTitle.enableVerboseLog = GetBoolOrDefault(context, L"WindowTitle", L"EnableVerboseLog", false);
			if (context.ini.Has(L"WindowTitle", L"ReplaceCount"))
			{
				int replaceCount = GetIntOrDefault(context, L"WindowTitle", L"ReplaceCount", 0, 0, 4096);
				for (int i = 0; i < replaceCount; i++)
				{
					std::wstring originalKey = L"Original_" + std::to_wstring(i);
					std::wstring newKey = L"New_" + std::to_wstring(i);
					if (context.ini.Has(L"WindowTitle", originalKey) && context.ini.Has(L"WindowTitle", newKey))
					{
						std::wstring original = DecodeRuleTextValue((std::wstring)context.ini[L"WindowTitle"][originalKey]);
						std::wstring replacement = DecodeRuleTextValue((std::wstring)context.ini[L"WindowTitle"][newKey]);
						if (!original.empty() && !replacement.empty())
						{
							settings.windowTitle.rules.emplace_back(original, replacement);
						}
					}
				}
			}
			if (settings.windowTitle.rules.empty()
				&& context.ini.Has(L"WindowTitle", L"OriginalTitle")
				&& context.ini.Has(L"WindowTitle", L"NewTitle"))
			{
				std::wstring original = DecodeRuleTextValue((std::wstring)context.ini[L"WindowTitle"][L"OriginalTitle"]);
				std::wstring replacement = DecodeRuleTextValue((std::wstring)context.ini[L"WindowTitle"][L"NewTitle"]);
				if (!original.empty() && !replacement.empty())
				{
					settings.windowTitle.rules.emplace_back(original, replacement);
				}
			}
			if (settings.windowTitle.rules.empty()
				&& context.ini.Has(L"WindowTitle", L"Title"))
			{
				std::wstring title = DecodeRuleTextValue((std::wstring)context.ini[L"WindowTitle"][L"Title"]);
				if (!title.empty())
				{
					settings.windowTitle.rules.emplace_back(L"*", title);
				}
			}
			if (settings.windowTitle.rules.empty()
				&& context.ini.Has(L"Window", L"Title"))
			{
				std::wstring title = DecodeRuleTextValue((std::wstring)context.ini[L"Window"][L"Title"]);
				if (!title.empty())
				{
					settings.windowTitle.rules.emplace_back(L"*", title);
				}
			}

			settings.startupMessage.enable = GetBoolOrDefault(context, L"StartupMessage", L"Enable", false);
			settings.startupMessage.style = GetIntOrDefault(context, L"StartupMessage", L"Style", 1, 1, 2);
			settings.startupMessage.title = DecodeEscapedControlChars(GetStringOrDefault(context, L"StartupMessage", L"Title", L"CialloHook"));
			settings.startupMessage.author = DecodeEscapedControlChars(GetStringOrDefault(context, L"StartupMessage", L"Author", L""));
			settings.startupMessage.text = DecodeEscapedControlChars(GetStringOrDefault(context, L"StartupMessage", L"Text", L""));
			if (Rut::StrX::Trim(settings.startupMessage.title).empty())
			{
				settings.startupMessage.title = L"CialloHook";
				AppendWarning(context, L"StartupMessage.Title 为空，已回退为 CialloHook");
			}
			if (settings.startupMessage.enable
				&& Rut::StrX::Trim(settings.startupMessage.author).empty()
				&& Rut::StrX::Trim(settings.startupMessage.text).empty())
			{
				settings.startupMessage.enable = false;
				AppendWarning(context, L"StartupMessage.Enable 已开启，但 Author 和 Text 都为空，已自动关闭");
			}

			settings.filePatch.enable = GetBoolOrDefault(context, L"FilePatch", L"Enable", false);
			settings.filePatch.patchFolders = GetIndexedList(context, L"FilePatch", L"PatchFolderCount", L"PatchFolderName_");
			if (settings.filePatch.patchFolders.empty())
			{
				settings.filePatch.patchFolders.push_back(L"patch");
			}
			settings.filePatch.enableLog = GetBoolOrDefault(context, L"FilePatch", L"EnableLog", false);
			settings.filePatch.debugMode = GetBoolOrDefault(context, L"FilePatch", L"DebugMode", false);
			settings.filePatch.customPakEnable = GetBoolOrDefault(context, L"FilePatch", L"CustomPakEnable", false);
			settings.filePatch.customPakFiles = GetIndexedList(context, L"FilePatch", L"CustomPakCount", L"CustomPakName_");
			settings.filePatch.vfsMode = GetIntOrDefault(context, L"FilePatch", L"VFSMode", 1, 0, 1);
			if (settings.filePatch.customPakEnable && settings.filePatch.customPakFiles.empty())
			{
				settings.filePatch.customPakEnable = false;
				AppendWarning(context, L"FilePatch.CustomPakEnable 已开启，但未配置有效的 CustomPakName_i，已自动关闭");
			}

			settings.fileSpoof.enable = GetBoolOrDefault(context, L"FileSpoof", L"Enable", false);
			settings.fileSpoof.spoofFiles = GetIndexedList(context, L"FileSpoof", L"SpoofFileCount", L"SpoofFileName_");
			if (settings.fileSpoof.spoofFiles.empty())
			{
				settings.fileSpoof.spoofFiles = GetIndexedList(context, L"FileSpoof", L"SpoofFileCount", L"SpoofFile_");
			}
			settings.fileSpoof.spoofDirectories = GetIndexedList(context, L"FileSpoof", L"SpoofDirectoryCount", L"SpoofDirectoryName_");
			if (settings.fileSpoof.spoofDirectories.empty())
			{
				settings.fileSpoof.spoofDirectories = GetIndexedList(context, L"FileSpoof", L"SpoofDirectoryCount", L"SpoofDirectory_");
			}
			settings.fileSpoof.enableLog = GetBoolOrDefault(context, L"FileSpoof", L"EnableLog", false);

			settings.directoryRedirect.enable = GetBoolOrDefault(context, L"DirectoryRedirect", L"Enable", false);
			settings.directoryRedirect.rules = GetIndexedRedirectRuleList(context, L"DirectoryRedirect", L"RuleCount", L"Rule_");
			if (settings.directoryRedirect.rules.empty())
			{
				settings.directoryRedirect.rules = GetIndexedRedirectRuleList(context, L"DirectoryRedirect", L"RedirectCount", L"RedirectRule_");
			}
			if (settings.directoryRedirect.rules.empty())
			{
				settings.directoryRedirect.rules = GetIndexedRedirectRuleList(context, L"DirectoryRedirect", L"SavePathCount", L"SavePath_");
			}
			settings.directoryRedirect.enableLog = GetBoolOrDefault(context, L"DirectoryRedirect", L"EnableLog", false);
			if (settings.directoryRedirect.enable && settings.directoryRedirect.rules.empty())
			{
				settings.directoryRedirect.enable = false;
				AppendWarning(context, L"DirectoryRedirect.Enable 已开启，但未配置有效的 Rule_i / RedirectRule_i / SavePath_i，已自动关闭");
			}

			settings.registry.enable = GetBoolOrDefault(context, L"Registry", L"Enable", false);
			settings.registry.files = GetIndexedList(context, L"Registry", L"FileCount", L"FileName_");
			if (settings.registry.files.empty())
			{
				std::wstring legacyFile = GetStringOrDefault(context, L"Registry", L"File", L"game.reg");
				if (!Rut::StrX::Trim(legacyFile).empty())
				{
					settings.registry.files.push_back(legacyFile);
				}
			}
			settings.registry.enableLog = GetBoolOrDefault(context, L"Registry", L"EnableLog", false);
			if (settings.registry.enable && settings.registry.files.empty())
			{
				settings.registry.enable = false;
				AppendWarning(context, L"Registry.Enable 已开启，但未配置有效的 FileName_i / File，已自动关闭");
			}

			settings.siglusKeyExtract.enable = GetBoolOrDefault(context, L"SiglusKeyExtract", L"Enable", false);
			settings.siglusKeyExtract.gameexePath = GetStringOrDefault(context, L"SiglusKeyExtract", L"GameexePath", L"Gameexe.dat");
			settings.siglusKeyExtract.keyOutputPath = GetStringOrDefault(context, L"SiglusKeyExtract", L"KeyOutputPath", L"siglus_key.txt");
			settings.siglusKeyExtract.showMessageBox = GetBoolOrDefault(context, L"SiglusKeyExtract", L"ShowMessageBox", true);
			settings.siglusKeyExtract.debugMode = GetBoolOrDefault(context, L"SiglusKeyExtract", L"DebugMode", false);
			if (Rut::StrX::Trim(settings.siglusKeyExtract.gameexePath).empty())
			{
				settings.siglusKeyExtract.gameexePath = L"Gameexe.dat";
				AppendWarning(context, L"SiglusKeyExtract.GameexePath 为空，已回退为 Gameexe.dat");
			}
			if (Rut::StrX::Trim(settings.siglusKeyExtract.keyOutputPath).empty())
			{
				settings.siglusKeyExtract.keyOutputPath = L"siglus_key.txt";
				AppendWarning(context, L"SiglusKeyExtract.KeyOutputPath 为空，已回退为 siglus_key.txt");
			}

			settings.codePage.enable = GetBoolOrDefault(context, L"CodePage", L"Enable", false);
			settings.codePage.fromCodePage = GetUIntOrDefault(context, L"CodePage", L"FromCodePage", 932);
			settings.codePage.toCodePage = GetUIntOrDefault(context, L"CodePage", L"ToCodePage", 936);

			settings.debug.enable = GetBoolOrDefault(context, L"Debug", L"Enable", false);
			settings.debug.logToFile = GetBoolOrDefault(context, L"Debug", L"LogToFile", false);
			settings.debug.logToConsole = GetBoolOrDefault(context, L"Debug", L"LogToConsole", false);

			settings.loadMode.mode = GetStringOrDefault(context, L"LoadMode", L"Mode", L"proxy");
			if (Rut::StrX::Trim(settings.loadMode.mode).empty())
			{
				settings.loadMode.mode = L"proxy";
				AppendWarning(context, L"LoadMode.Mode 为空，已回退为 proxy");
			}

			settings.localeEmulator.enable = GetBoolOrDefault(context, L"LocaleEmulator", L"Enable", false);
			settings.localeEmulator.ansiCodePage = GetUIntWithAliasOrDefault(context, L"LocaleEmulator", L"AnsiCodePage", L"CodePage", 932);
			settings.localeEmulator.oemCodePage = GetUIntWithAliasOrDefault(context, L"LocaleEmulator", L"OemCodePage", L"CodePage", settings.localeEmulator.ansiCodePage);
			settings.localeEmulator.localeID = GetUIntOrDefault(context, L"LocaleEmulator", L"LocaleID", 0x411);
			settings.localeEmulator.defaultCharset = GetUIntWithAliasOrDefault(context, L"LocaleEmulator", L"DefaultCharset", L"Charset", 128, 0, 0xFF);
			settings.localeEmulator.hookUILanguageAPI = GetUIntOrDefault(context, L"LocaleEmulator", L"HookUILanguageAPI", 0);
			settings.localeEmulator.timezone = GetStringOrDefault(context, L"LocaleEmulator", L"Timezone", L"Tokyo Standard Time");
			if (Rut::StrX::Trim(settings.localeEmulator.timezone).empty())
			{
				settings.localeEmulator.timezone = L"Tokyo Standard Time";
				AppendWarning(context, L"LocaleEmulator.Timezone 为空，已回退为 Tokyo Standard Time");
			}
			settings.engineCache.med = GetBoolOrDefault(context, L"GLOBAL", L"MED", false);
			settings.engineCache.majiro = GetBoolOrDefault(context, L"GLOBAL", L"MAJIRO", false);
			settings.enginePatches.enableKrkrPatch = GetBoolWithAliasOrDefault(
				context, L"EnginePatches", L"EnableKrkrPatch", L"KrkrPatch",
				GetBoolWithAliasOrDefault(context, L"GLOBAL", L"EnableKrkrPatch", L"KrkrPatch", false));
			settings.enginePatches.krkrPatchVerboseLog = GetBoolOrDefault(
				context, L"EnginePatches", L"KrkrPatchVerboseLog",
				GetBoolOrDefault(context, L"GLOBAL", L"KrkrPatchVerboseLog", false));
			settings.enginePatches.krkrBootstrapBypass = GetBoolOrDefault(
				context, L"EnginePatches", L"KrkrBootstrapBypass",
				GetBoolOrDefault(context, L"GLOBAL", L"KrkrBootstrapBypass", false));
			settings.enginePatches.krkrPatchNames = GetIndexedList(context, L"EnginePatches", L"KrkrPatchCount", L"KrkrPatchName_");
			if (settings.enginePatches.krkrPatchNames.empty())
			{
				settings.enginePatches.krkrPatchNames = GetIndexedList(context, L"GLOBAL", L"KrkrPatchCount", L"KrkrPatchName_");
			}
			if (settings.enginePatches.krkrPatchNames.empty())
			{
				std::wstring singleKrkrPatchName = GetStringOrDefault(context, L"EnginePatches", L"KrkrPatchName", L"");
				if (Rut::StrX::Trim(singleKrkrPatchName).empty())
				{
					singleKrkrPatchName = GetStringOrDefault(context, L"GLOBAL", L"KrkrPatchName", L"");
				}
				singleKrkrPatchName = Rut::StrX::Trim(singleKrkrPatchName);
				if (!singleKrkrPatchName.empty())
				{
					settings.enginePatches.krkrPatchNames.push_back(singleKrkrPatchName);
				}
			}
			settings.enginePatches.enableWafflePatch = GetBoolWithAliasOrDefault(
				context, L"EnginePatches", L"EnableWafflePatch", L"WafflePatch",
				GetBoolWithAliasOrDefault(context, L"GLOBAL", L"EnableWafflePatch", L"WafflePatch", false));
			settings.enginePatches.waffleFixGetTextCrash = GetBoolOrDefault(
				context, L"EnginePatches", L"WaffleFixGetTextCrash",
				GetBoolOrDefault(context, L"GLOBAL", L"WaffleFixGetTextCrash", true));

			if (warningMessage)
			{
				*warningMessage = BuildWarningMessage(context.warnings);
			}

			return true;
		}
		catch (const std::exception& err)
		{
			errorMessage = err.what();
			return false;
		}
	}
}
