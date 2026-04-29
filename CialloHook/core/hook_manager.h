#pragma once

#include <Windows.h>

namespace CialloHook
{
	class HookManager
	{
	public:
		static void RegisterLocaleEmulatorStagedFilesFromEnvironment();
		static void CleanupLocaleEmulatorStagedFilesOnShutdown();
		static bool TryEarlyLocaleEmulatorRelaunch(HMODULE dllModule);
		static bool ShouldSuspendProcessForStartupMessage(HMODULE dllModule);
		static bool TryHandleStartupMessageBeforeInitialization(HMODULE dllModule);
		static void Initialize(HMODULE dllModule);
	};
}
