#pragma once
#include <cstddef>
#include <cstdint>

#include "Hook_API.h"

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		struct JumpPatchHandle
		{
			void* target = nullptr;
			size_t patchSize = 0;
			uint8_t originalBytes[16] = {};
			bool installed = false;
		};

		class ScopedDetourErrorDialogSuppression
		{
		public:
			ScopedDetourErrorDialogSuppression();
			~ScopedDetourErrorDialogSuppression();

			ScopedDetourErrorDialogSuppression(const ScopedDetourErrorDialogSuppression&) = delete;
			ScopedDetourErrorDialogSuppression& operator=(const ScopedDetourErrorDialogSuppression&) = delete;
		};

		bool BeginDetourBatch();
		bool EndDetourBatch(const wchar_t* operationName);
		bool TryDetourAttach(void* ppRawFunc, void* pNewFunc);
		bool TryDetourDetach(void* ppRawFunc, void* pNewFunc);
		bool InstallJumpPatch(void* target, void* detour, size_t patchSize, JumpPatchHandle& handle);
		bool RemoveJumpPatch(JumpPatchHandle& handle);
	}
}
