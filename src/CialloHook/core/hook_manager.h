#pragma once

#include <Windows.h>
#include "../config/settings.h"

namespace CialloHook
{
	class HookManager
	{
	public:
		static void RegisterLocaleEmulatorStagedFilesFromEnvironment();
		static void CleanupLocaleEmulatorStagedFilesOnShutdown();
		static bool TryEarlyLocaleEmulatorRelaunch(HMODULE dllModule);
		static bool TryHandleConsentInDllMain(HMODULE dllModule);
		static bool TryLoadStartupSettings(HMODULE dllModule, AppSettings& settings);
		static void Initialize(HMODULE dllModule);
		static void ShowSplashFromEntryPoint(HMODULE dllModule);
	};
}
