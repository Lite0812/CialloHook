#pragma once

#include <string>
#include <vector>

namespace CialloHook
{
	namespace HookModules
	{
		void ConfigureKrkrPluginPatchTargets(
			bool enableKrkrPatch,
			bool verboseLog,
			bool bootstrapBypass,
			const std::wstring& gameDir,
			const std::vector<std::wstring>& patchRoots,
			const std::vector<std::wstring>& customPakFiles,
			const std::vector<std::wstring>& patchBaseNames,
			const std::vector<std::wstring>& patchFolders,
			const std::vector<std::wstring>& patchArchives);
	}
}
