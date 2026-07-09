#include "engine_compat.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        bool HasEngine(const EngineCompatState& state, EngineFlag engine)
        {
            return (state.engines & static_cast<uint32_t>(engine)) != 0;
        }

        const wchar_t* EngineFlagName(EngineFlag engine)
        {
            switch (engine)
            {
            case EngineTinkerBell: return L"tinkerbell";
            case EngineCyberworks: return L"cyberworks";
            case EngineAdvHD: return L"advhd";
            case EngineDxLib: return L"dxlib";
            case EngineMED: return L"med";
            case EngineMajiro: return L"majiro";
            case EngineSoftpal: return L"softpal";
            case EngineMirai: return L"mirai";
            case EngineArtemis: return L"artemis";
            case EngineArtemisLegacy: return L"artemis_legacy";
            case EngineKrkr: return L"krkr";
            case EngineEscude: return L"escude";
            case EngineLsbSafeFont: return L"lsb_safe_font";
            default: return L"unknown";
            }
        }

        std::wstring DescribeEngineFlags(uint32_t engines)
        {
            if (engines == EngineUnknown || engines == EngineNone)
            {
                return L"unknown";
            }
            std::wstring result;
            const EngineFlag flags[] = { EngineTinkerBell, EngineCyberworks, EngineAdvHD, EngineDxLib, EngineMED, EngineMajiro, EngineSoftpal, EngineMirai, EngineArtemis, EngineKrkr, EngineEscude, EngineArtemisLegacy, EngineLsbSafeFont };
            for (EngineFlag flag : flags)
            {
                if ((engines & static_cast<uint32_t>(flag)) == 0)
                {
                    continue;
                }
                if (!result.empty())
                {
                    result += L"|";
                }
                result += EngineFlagName(flag);
            }
            return result.empty() ? L"unknown" : result;
        }

        void LogEngineCompatState(const EngineCompatState& state, bool verbose)
        {
            if (!state.enabled && !verbose)
            {
                return;
            }
            std::wstring engines = DescribeEngineFlags(state.engines);
            LogMessage(LogLevel::Info,
                L"EngineCompat: enabled=%d aggressive=%d engines=%s primary=%s skipWide=%d trackedSelect=%d hideCache=%d pinnedFontData=%d windowsFontsRedirect=%d virtualFonts=%d aggressiveScan=%d",
                state.enabled ? 1 : 0,
                state.aggressive ? 1 : 0,
                engines.c_str(),
                state.primaryProfile.c_str(),
                state.skipWideFontCreation ? 1 : 0,
                state.selectObjectTrackedOnly ? 1 : 0,
                state.hideFontCacheFiles ? 1 : 0,
                state.preferPinnedFontData ? 1 : 0,
                state.needsWindowsFontsRedirect ? 1 : 0,
                state.needsVirtualFontFiles ? 1 : 0,
                state.allowAggressiveMemoryScan ? 1 : 0);

            if (verbose)
            {
                for (size_t i = 0; i < state.reasons.size(); ++i)
                {
                    LogMessage(LogLevel::Info, L"EngineCompat: reason[%u]=%s", static_cast<uint32_t>(i), state.reasons[i].c_str());
                }
            }
        }
    }
}
