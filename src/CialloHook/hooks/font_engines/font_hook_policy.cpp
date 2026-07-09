#include "font_hook_policy.h"

#include <cwctype>

namespace CialloHook
{
    namespace FontHookPolicy
    {
        namespace
        {
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

            bool IsSensitiveProfile(FontEngineProfile profile)
            {
                return profile == FontEngineProfile::Sensitive
                    || profile == FontEngineProfile::TinkerBell
                    || profile == FontEngineProfile::Cyberworks;
            }

            FontEngineProfile ProfileFromEngineState(const EngineCompat::EngineCompatState& state)
            {
                if (!state.enabled)
                {
                    return FontEngineProfile::Unknown;
                }
                if (EngineCompat::HasEngine(state, EngineCompat::EngineTinkerBell)) return FontEngineProfile::TinkerBell;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineCyberworks)) return FontEngineProfile::Cyberworks;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineAdvHD)) return FontEngineProfile::AdvHD;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineDxLib)) return FontEngineProfile::DxLib;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineMED)) return FontEngineProfile::MED;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineMajiro)) return FontEngineProfile::Majiro;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineSoftpal)) return FontEngineProfile::Softpal;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineMirai)) return FontEngineProfile::Mirai;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineArtemis)) return FontEngineProfile::Artemis;
                if (EngineCompat::HasEngine(state, EngineCompat::EngineKrkr)) return FontEngineProfile::Krkr;
                return FontEngineProfile::Unknown;
            }

            FontInstallDecision BuildWideCreateDecision(bool configAllows, const FontSettings& settings, FontEngineProfile profile, bool engineStateSkipsWide)
            {
                if (!configAllows)
                {
                    return { false, FontPolicyReason::DisabledByConfig };
                }
                if (!settings.fontEngineCompatMode)
                {
                    return { true, FontPolicyReason::CompatModeDisabled };
                }
                if (!settings.fontRiskAutoDowngrade)
                {
                    return { true, FontPolicyReason::AutoDowngradeDisabled };
                }
                if (settings.compatSkipWideFontCreationOnSensitiveEngine && (IsSensitiveProfile(profile) || engineStateSkipsWide))
                {
                    return { false, FontPolicyReason::DisabledBySensitiveEngine };
                }
                if (profile == FontEngineProfile::Auto || profile == FontEngineProfile::Unknown)
                {
                    return { true, FontPolicyReason::UnknownEngineAllowed };
                }
                return { true, FontPolicyReason::Allowed };
            }
        }

        FontEngineProfile ParseFontEngineProfile(const std::wstring& value)
        {
            const std::wstring normalized = NormalizeProfileName(value);
            if (normalized.empty() || normalized == L"auto")
            {
                return FontEngineProfile::Auto;
            }
            if (normalized == L"none" || normalized == L"off" || normalized == L"disabled")
            {
                return FontEngineProfile::None;
            }
            if (normalized == L"sensitive" || normalized == L"safe")
            {
                return FontEngineProfile::Sensitive;
            }
            if (normalized == L"tinkerbell" || normalized == L"tinker") return FontEngineProfile::TinkerBell;
            if (normalized == L"cyberworks" || normalized == L"cyberwork") return FontEngineProfile::Cyberworks;
            if (normalized == L"advhd" || normalized == L"adv") return FontEngineProfile::AdvHD;
            if (normalized == L"dxlib") return FontEngineProfile::DxLib;
            if (normalized == L"med") return FontEngineProfile::MED;
            if (normalized == L"majiro") return FontEngineProfile::Majiro;
            if (normalized == L"softpal") return FontEngineProfile::Softpal;
            if (normalized == L"mirai") return FontEngineProfile::Mirai;
            if (normalized == L"artemis") return FontEngineProfile::Artemis;
            if (normalized == L"krkr" || normalized == L"kirikiri") return FontEngineProfile::Krkr;
            if (normalized == L"unknown") return FontEngineProfile::Unknown;
            return FontEngineProfile::Unknown;
        }

        FontInstallPlan BuildFontInstallPlan(const FontSettings& settings)
        {
            FontInstallPlan plan;
            plan.profile = ParseFontEngineProfile(settings.fontEngineProfile);
            plan.createFontW = BuildWideCreateDecision(settings.hookCreateFontW, settings, plan.profile, false);
            plan.createFontIndirectW = BuildWideCreateDecision(settings.hookCreateFontIndirectW, settings, plan.profile, false);
            return plan;
        }

        FontInstallPlan BuildFontInstallPlan(const FontSettings& settings, const EngineCompat::EngineCompatState& engineState)
        {
            FontInstallPlan plan;
            plan.profile = ParseFontEngineProfile(settings.fontEngineProfile);
            bool engineStateSkipsWide = false;
            if (plan.profile == FontEngineProfile::Auto)
            {
                plan.profile = ProfileFromEngineState(engineState);
                engineStateSkipsWide = engineState.skipWideFontCreation;
            }
            else if (plan.profile == FontEngineProfile::None)
            {
                engineStateSkipsWide = false;
            }
            plan.createFontW = BuildWideCreateDecision(settings.hookCreateFontW, settings, plan.profile, engineStateSkipsWide);
            plan.createFontIndirectW = BuildWideCreateDecision(settings.hookCreateFontIndirectW, settings, plan.profile, engineStateSkipsWide);
            return plan;
        }

        const wchar_t* FontRiskApiName(FontRiskApi api)
        {
            switch (api)
            {
            case FontRiskApi::CreateFontW:
                return L"CreateFontW";
            case FontRiskApi::CreateFontIndirectW:
                return L"CreateFontIndirectW";
            default:
                return L"unknown";
            }
        }

        const wchar_t* FontEngineProfileName(FontEngineProfile profile)
        {
            switch (profile)
            {
            case FontEngineProfile::Auto: return L"auto";
            case FontEngineProfile::None: return L"none";
            case FontEngineProfile::Sensitive: return L"sensitive";
            case FontEngineProfile::TinkerBell: return L"tinkerbell";
            case FontEngineProfile::Cyberworks: return L"cyberworks";
            case FontEngineProfile::AdvHD: return L"advhd";
            case FontEngineProfile::DxLib: return L"dxlib";
            case FontEngineProfile::MED: return L"med";
            case FontEngineProfile::Majiro: return L"majiro";
            case FontEngineProfile::Softpal: return L"softpal";
            case FontEngineProfile::Mirai: return L"mirai";
            case FontEngineProfile::Artemis: return L"artemis";
            case FontEngineProfile::Krkr: return L"krkr";
            case FontEngineProfile::Unknown: return L"unknown";
            default: return L"unknown";
            }
        }

        const wchar_t* FontPolicyReasonName(FontPolicyReason reason)
        {
            switch (reason)
            {
            case FontPolicyReason::Allowed:
                return L"allowed";
            case FontPolicyReason::DisabledByConfig:
                return L"disabled-by-config";
            case FontPolicyReason::CompatModeDisabled:
                return L"compat-mode-disabled";
            case FontPolicyReason::AutoDowngradeDisabled:
                return L"auto-downgrade-disabled";
            case FontPolicyReason::DisabledBySensitiveEngine:
                return L"disabled-by-sensitive-engine";
            case FontPolicyReason::UnknownEngineAllowed:
                return L"unknown-engine-allowed";
            default:
                return L"unknown";
            }
        }
    }
}
