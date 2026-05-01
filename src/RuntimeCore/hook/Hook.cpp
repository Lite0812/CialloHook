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
			struct DetourBatchState
			{
				uint32_t depth = 0;
				LONG firstOperationError = NO_ERROR;
				bool transactionActive = false;
			};

			thread_local DetourBatchState sg_detourBatchState;
			thread_local uint32_t sg_detourErrorDialogSuppressionDepth = 0;

			void ReportDetourFailure(const wchar_t* message)
			{
				const wchar_t* safeMessage = message ? message : L"Detour operation failed";
				OutputDebugStringW(safeMessage);
				OutputDebugStringW(L"\r\n");
				if (sg_detourErrorDialogSuppressionDepth != 0)
				{
					LogMessage(LogLevel::Warn, L"%s", safeMessage);
					return;
				}
				MessageBoxW(nullptr, safeMessage, L"CialloHook - Detour Error", MB_OK | MB_ICONERROR);
			}

			bool StartDetourTransaction(const wchar_t* operationName)
			{
				DetourRestoreAfterWith();
				const LONG beginResult = DetourTransactionBegin();
				if (beginResult != NO_ERROR)
				{
					wchar_t message[160] = {};
					swprintf_s(message, L"%s failed (begin=%ld)",
						operationName ? operationName : L"Detour transaction",
						static_cast<long>(beginResult));
					ReportDetourFailure(message);
					return false;
				}

				const LONG updateResult = DetourUpdateThread(GetCurrentThread());
				if (updateResult == NO_ERROR)
				{
					return true;
				}

				const LONG abortResult = DetourTransactionAbort();
				wchar_t message[160] = {};
				swprintf_s(message, L"%s failed (update=%ld, abort=%ld)",
					operationName ? operationName : L"Detour transaction",
					static_cast<long>(updateResult),
					static_cast<long>(abortResult));
				ReportDetourFailure(message);
				return false;
			}

			void RecordBatchOperationResult(LONG operationResult)
			{
				if (operationResult != NO_ERROR && sg_detourBatchState.firstOperationError == NO_ERROR)
				{
					sg_detourBatchState.firstOperationError = operationResult;
				}
			}

			void ResetDetourBatchState()
			{
				sg_detourBatchState.depth = 0;
				sg_detourBatchState.firstOperationError = NO_ERROR;
				sg_detourBatchState.transactionActive = false;
			}

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
				ReportDetourFailure(message);
				return false;
			}
		}

		ScopedDetourErrorDialogSuppression::ScopedDetourErrorDialogSuppression()
		{
			++sg_detourErrorDialogSuppressionDepth;
		}

		ScopedDetourErrorDialogSuppression::~ScopedDetourErrorDialogSuppression()
		{
			if (sg_detourErrorDialogSuppressionDepth > 0)
			{
				--sg_detourErrorDialogSuppressionDepth;
			}
		}

		bool BeginDetourBatch()
		{
			if (sg_detourBatchState.depth == 0)
			{
				if (!StartDetourTransaction(L"Detour batch begin"))
				{
					ResetDetourBatchState();
					return false;
				}
				sg_detourBatchState.transactionActive = true;
				sg_detourBatchState.firstOperationError = NO_ERROR;
			}

			++sg_detourBatchState.depth;
			return true;
		}

		bool EndDetourBatch(const wchar_t* operationName)
		{
			if (sg_detourBatchState.depth == 0)
			{
				return true;
			}

			--sg_detourBatchState.depth;
			if (sg_detourBatchState.depth != 0)
			{
				return true;
			}

			const LONG operationResult = sg_detourBatchState.firstOperationError;
			const bool transactionActive = sg_detourBatchState.transactionActive;
			ResetDetourBatchState();

			if (!transactionActive)
			{
				return operationResult == NO_ERROR;
			}

			return FinalizeDetourTransaction(operationName ? operationName : L"Detour batch", operationResult);
		}

		bool TryDetourAttach(void* ppRawFunc, void* pNewFunc)
		{
			if (sg_detourBatchState.depth != 0 && sg_detourBatchState.transactionActive)
			{
				const LONG attachResult = DetourAttach((PVOID*)ppRawFunc, pNewFunc);
				RecordBatchOperationResult(attachResult);
				return attachResult == NO_ERROR;
			}

			if (!StartDetourTransaction(L"Detour attach"))
			{
				return false;
			}

			const LONG attachResult = DetourAttach((PVOID*)ppRawFunc, pNewFunc);
			return FinalizeDetourTransaction(L"Detour attach", attachResult);
		}

		bool TryDetourDetach(void* ppRawFunc, void* pNewFunc)
		{
			if (sg_detourBatchState.depth != 0 && sg_detourBatchState.transactionActive)
			{
				const LONG detachResult = DetourDetach((PVOID*)ppRawFunc, pNewFunc);
				RecordBatchOperationResult(detachResult);
				return detachResult == NO_ERROR;
			}

			if (!StartDetourTransaction(L"Detour detach"))
			{
				return false;
			}

			const LONG detachResult = DetourDetach((PVOID*)ppRawFunc, pNewFunc);
			return FinalizeDetourTransaction(L"Detour detach", detachResult);
		}
	}
}
