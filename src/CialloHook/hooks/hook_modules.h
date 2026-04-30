#pragma once

#include "../config/settings.h"

namespace CialloHook
{
	namespace HookModules
	{
		void CleanupLoadedFontTempFiles();
		void ApplyFontHooks(const FontSettings& settings);
		void ApplyTextHooks(const TextReplaceSettings& settings, const EnginePatchSettings& enginePatchSettings);
		void ApplyWindowTitleHooks(const WindowTitleSettings& settings);
		void ApplyPostStartupHooks(const AppSettings& settings);
		void ApplySiglusKeyExtract(const SiglusKeyExtractSettings& settings);
		void ApplyFilePatchHooks(const FilePatchSettings& patchSettings, const FileSpoofSettings& spoofSettings, const DirectoryRedirectSettings& directoryRedirectSettings, const EnginePatchSettings& enginePatchSettings);
		void ApplyRegistryHooks(const RegistrySettings& settings);
		void ApplyCodePageHooks(const CodePageSettings& settings);
	}
}
