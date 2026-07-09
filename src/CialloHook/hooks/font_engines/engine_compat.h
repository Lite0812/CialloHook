#pragma once

#include "../../config/settings.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CialloHook
{
    namespace EngineCompat
    {
        enum EngineFlag : uint32_t
        {
            EngineNone = 0,
            EngineTinkerBell = 1u << 0,
            EngineCyberworks = 1u << 1,
            EngineAdvHD = 1u << 2,
            EngineDxLib = 1u << 3,
            EngineMED = 1u << 4,
            EngineMajiro = 1u << 5,
            EngineSoftpal = 1u << 6,
            EngineMirai = 1u << 7,
            EngineArtemis = 1u << 8,
            EngineKrkr = 1u << 9,
            EngineEscude = 1u << 10,
            EngineArtemisLegacy = 1u << 11,
            EngineLsbSafeFont = 1u << 12,
            EngineUnknown = 1u << 31,
        };

        struct EngineCompatState
        {
            bool enabled = false;
            bool aggressive = false;
            uint32_t engines = EngineUnknown;
            std::wstring primaryProfile = L"unknown";
            std::vector<std::wstring> reasons;

            bool skipWideFontCreation = false;
            bool selectObjectTrackedOnly = true;
            bool hideFontCacheFiles = false;
            bool preferPinnedFontData = false;
            bool needsWindowsFontsRedirect = false;
            bool needsVirtualFontFiles = false;
            bool allowAggressiveMemoryScan = false;
        };

        bool HasEngine(const EngineCompatState& state, EngineFlag engine);
        const wchar_t* EngineFlagName(EngineFlag engine);
        std::wstring DescribeEngineFlags(uint32_t engines);

        EngineCompatState DetectEngineCompatState(const AppSettings& settings);
        void ApplyEngineCompatHooks(const AppSettings& settings, const EngineCompatState& state);
        void LogEngineCompatState(const EngineCompatState& state, bool verbose);
    }
}
