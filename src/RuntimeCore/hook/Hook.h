#pragma once
#include <cstdint>

#include "Hook_API.h"

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
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
	}
}
