#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

#include <cwctype>

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        std::wstring TrimCopy(const std::wstring& value)
        {
            size_t begin = 0;
            while (begin < value.size() && std::iswspace(value[begin]))
            {
                ++begin;
            }
            size_t end = value.size();
            while (end > begin && std::iswspace(value[end - 1]))
            {
                --end;
            }
            return value.substr(begin, end - begin);
        }

        std::wstring LowerCopy(std::wstring value)
        {
            for (wchar_t& ch : value)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }
            return value;
        }

        bool LooksLikeFontFilePath(const std::wstring& value)
        {
            const std::wstring trimmed = LowerCopy(TrimCopy(value));
            if (trimmed.empty())
            {
                return false;
            }
            const bool hasPathMark = trimmed.find(L'\\') != std::wstring::npos
                || trimmed.find(L'/') != std::wstring::npos
                || trimmed.find(L':') != std::wstring::npos;
            return hasPathMark
                && trimmed.size() >= 4
                && (trimmed.rfind(L".ttf") == trimmed.size() - 4
                    || trimmed.rfind(L".ttc") == trimmed.size() - 4
                    || trimmed.rfind(L".otf") == trimmed.size() - 4);
        }

        std::wstring ResolveReplacementFontFile(const FontSettings& font)
        {
            if (LooksLikeFontFilePath(font.font))
            {
                return TrimCopy(font.font);
            }
            return L"";
        }

        std::wstring ResolveReplacementFaceName(const FontSettings& font)
        {
            std::wstring faceName = TrimCopy(font.fontNameOverride);
            if (!faceName.empty())
            {
                return faceName;
            }
            if (!LooksLikeFontFilePath(font.font))
            {
                return TrimCopy(font.font);
            }
            return L"";
        }

        void ApplyEngineCompatHooks(const AppSettings& settings, const EngineCompatState& state)
        {
            if (!state.enabled)
            {
                ClearEngineFileCompatRules();
                SetEngineFileCompatLogEnabled(false);
                return;
            }

            ClearEngineFileCompatRules();
            SetEngineFileCompatLogEnabled(settings.engineCompat.enableLog || settings.filePatch.enableLog);

            ApplyDxLibFileCompat(settings, state);
            ApplyMajiroFileCompat(settings, state);
            ApplySoftpalFileCompat(settings, state);
            ApplyKrkrFileCompat(settings, state);
            ApplyArtemisFileCompat(settings, state);
            ApplyArtemisLegacyFileCompat(settings, state);
            ApplyMiraiFileCompat(settings, state);
            ApplyEscudeFileCompat(settings, state);
        }
    }
}
