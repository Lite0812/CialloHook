#pragma once

#include "../LauncherTypes.h"

#include <string>
#include <vector>

namespace CialloLauncher
{
	bool LaunchWithLocaleEmulator(
		const LauncherConfig& config,
		const std::wstring& selfPath,
		const std::vector<std::string>& dllList,
		bool& cleanupPending);

	bool LaunchWithDetours(
		const LauncherConfig& config,
		const std::wstring& selfPath,
		const std::wstring& exeDir,
		const std::vector<std::string>& dllList,
		bool& cleanupPending);
}
