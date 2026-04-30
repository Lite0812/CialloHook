#include "Hook.h"

#include <Windows.h>

#include "../../../third/detours/include/detours.h"
// Detours 库已在编译脚本中指定，不再使用 pragma comment
// #pragma comment(lib,"../../third/detours/lib.X86/detours.lib")


namespace Rut
{
	namespace HookX
	{
		namespace
		{
			bool FinalizeDetourTransaction(const wchar_t* operationName, LONG operationResult)
			{
				const LONG commitResult = DetourTransactionCommit();
				if (operationResult == NO_ERROR && commitResult == NO_ERROR)
				{
					return true;
				}

				wchar_t message[160] = {};
				swprintf_s(message, L"%s failed (operation=%ld, commit=%ld)",
					operationName ? operationName : L"Detour transaction",
					static_cast<long>(operationResult),
					static_cast<long>(commitResult));
				MessageBoxW(nullptr, message, L"CialloHook - Detour Error", MB_OK | MB_ICONERROR);
				return false;
			}
		}

		bool TryDetourAttach(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			const LONG attachResult = DetourAttach((PVOID*)ppRawFunc, pNewFunc);
			return FinalizeDetourTransaction(L"Detour attach", attachResult);
		}

		bool TryDetourDetach(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			const LONG detachResult = DetourDetach((PVOID*)ppRawFunc, pNewFunc);
			return FinalizeDetourTransaction(L"Detour detach", detachResult);
		}
	}
}
