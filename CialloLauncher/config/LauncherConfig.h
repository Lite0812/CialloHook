#pragma once

#include "../LauncherTypes.h"

namespace CialloLauncher
{
	bool LoadLauncherConfig(const std::wstring& exeDir, const std::wstring& exeNameNoExt, LauncherConfig& config);
}
