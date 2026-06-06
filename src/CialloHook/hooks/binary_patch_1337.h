#pragma once

#include "../config/settings.h"

namespace CialloHook
{
	namespace HookModules
	{
		void ApplyBinaryPatches(const BinaryPatchSettings& settings);
		void TryApplyBinaryPatchesBeforeEntry(const BinaryPatchSettings& settings);
		bool IsBinaryPatchPreEntryApplied();
		void TryRequestBinaryPatchOnFirstPatchHit(const BinaryPatchSettings& settings);
	}
}
