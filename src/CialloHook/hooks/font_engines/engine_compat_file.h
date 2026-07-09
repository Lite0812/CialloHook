#pragma once

#include "engine_compat.h"

#include <string>

namespace CialloHook
{
    namespace EngineCompat
    {
        std::wstring TrimCopy(const std::wstring& value);
        std::wstring LowerCopy(std::wstring value);
        bool LooksLikeFontFilePath(const std::wstring& value);
        std::wstring ResolveReplacementFontFile(const FontSettings& font);
        std::wstring ResolveReplacementFaceName(const FontSettings& font);

        void ApplyDxLibFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyMajiroFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplySoftpalFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyKrkrFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyArtemisFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyMiraiFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyEscudeFileCompat(const AppSettings& settings, const EngineCompatState& state);
        void ApplyArtemisLegacyFileCompat(const AppSettings& settings, const EngineCompatState& state);
    }
}
