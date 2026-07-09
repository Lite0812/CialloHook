#pragma once

#include "../../config/settings.h"
#include "engine_compat.h"

namespace CialloHook
{
    namespace FontHookPolicy
    {
        enum class FontRiskApi
        {
            CreateFontW,
            CreateFontIndirectW,
        };

        enum class FontEngineProfile
        {
            Auto,
            None,
            Sensitive,
            TinkerBell,
            Cyberworks,
            AdvHD,
            DxLib,
            MED,
            Majiro,
            Softpal,
            Mirai,
            Artemis,
            Krkr,
            Unknown,
        };

        enum class FontPolicyReason
        {
            Allowed,
            DisabledByConfig,
            CompatModeDisabled,
            AutoDowngradeDisabled,
            DisabledBySensitiveEngine,
            UnknownEngineAllowed,
        };

        struct FontInstallDecision
        {
            bool install = false;
            FontPolicyReason reason = FontPolicyReason::DisabledByConfig;
        };

        struct FontInstallPlan
        {
            FontEngineProfile profile = FontEngineProfile::Auto;
            FontInstallDecision createFontW;
            FontInstallDecision createFontIndirectW;
        };

        FontEngineProfile ParseFontEngineProfile(const std::wstring& value);
        FontInstallPlan BuildFontInstallPlan(const FontSettings& settings);
        FontInstallPlan BuildFontInstallPlan(const FontSettings& settings, const EngineCompat::EngineCompatState& engineState);
        const wchar_t* FontRiskApiName(FontRiskApi api);
        const wchar_t* FontEngineProfileName(FontEngineProfile profile);
        const wchar_t* FontPolicyReasonName(FontPolicyReason reason);
    }
}
