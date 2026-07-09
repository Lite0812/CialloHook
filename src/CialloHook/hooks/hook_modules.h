#pragma once

#include "../config/settings.h"
#include "font_engines/engine_compat.h"

namespace CialloHook
{
    namespace HookModules
    {
        void CleanupLoadedFontTempFiles();
        void ApplyFontHooks(const FontSettings& settings, const EngineCompat::EngineCompatState* engineState = nullptr);
        void ApplyTextHooks(const TextReplaceSettings& settings, const EnginePatchSettings& enginePatchSettings);
        void ApplyWindowTitleHooks(const WindowTitleSettings& settings);
        void ApplyScreenCaptureProtectionHooks(const ScreenCaptureProtectionSettings& settings);
        void ApplyEarlyStartupHooks(const AppSettings& settings, uint32_t bypassThreadId);
        void ApplyPostStartupHooks(const AppSettings& settings, bool releaseStartupWindowGate = true);
        void TryApplyBinaryPatchesBeforeEntry(const BinaryPatchSettings& settings);
        bool IsBinaryPatchPreEntryApplied();
        void TryRequestBinaryPatchOnFirstPatchHit(const BinaryPatchSettings& settings);
        void ApplySiglusKeyExtract(const SiglusKeyExtractSettings& settings);
        void ApplyAliceSystem3xHooks(const AliceSystem3xSettings& settings, const FilePatchSettings& filePatchSettings);
        void ApplyRioShiinaHooks(const RioShiinaSettings& settings, const FilePatchSettings& filePatchSettings);
        void ApplyEngineCompatFileHooks(const AppSettings& settings, const EngineCompat::EngineCompatState& engineCompatState);
        void ApplyFilePatchHooks(const FilePatchSettings& patchSettings, const FileSpoofSettings& spoofSettings, const DirectoryRedirectSettings& directoryRedirectSettings, const EnginePatchSettings& enginePatchSettings);
        void ApplyRegistryBootstrap(const RegistryBootstrapSettings& settings);
        void CleanupRegistryBootstrap();
        void ApplyRegistryHooks(const RegistrySettings& settings);
        void ApplyCodePageHooks(const CodePageSettings& settings);
        void ApplyLocaleEmulatorHooks(const LocaleEmulatorSettings& settings);
    }
}
