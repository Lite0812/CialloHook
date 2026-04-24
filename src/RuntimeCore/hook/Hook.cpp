#include "Hook.h"
#include "Mem.h"

#include <Windows.h>

#include "../../../third/detours/include/detours.h"
// Detours 库已在编译脚本中指定，不再使用 pragma comment
// #pragma comment(lib,"../../third/detours/lib.X86/detours.lib")


namespace Rut
{
	namespace HookX
	{
		bool WriteHookCode(uintptr_t uiRawAddress, uintptr_t uiNewAddress, uint32_t szHookCode)
		{
			UCHAR code[0xF];
			memset(code, 0x90, 0xF);

			int32_t relativeOffset = static_cast<int32_t>(static_cast<intptr_t>(uiNewAddress) - static_cast<intptr_t>(uiRawAddress) - 5);
			*(UCHAR*)(code + 0) = 0xE9;
			memcpy(code + 1, &relativeOffset, sizeof(relativeOffset));

			if (MemX::WriteMemory(reinterpret_cast<LPVOID>(uiRawAddress), &code, szHookCode)) return TRUE;

			MessageBoxW(NULL, L"WriteHookCode Failed!!", NULL, NULL);

			return FALSE;
		}

		bool WriteHookCode_RET(uintptr_t uiRawAddress, uintptr_t uiRetAddress, uintptr_t uiNewAddress)
		{
			UCHAR code[0xF];
			memset(code, 0x90, 0xF);

			int32_t relativeOffset = static_cast<int32_t>(static_cast<intptr_t>(uiNewAddress) - static_cast<intptr_t>(uiRawAddress) - 5);
			*(UCHAR*)(code + 0) = 0xE9;
			memcpy(code + 1, &relativeOffset, sizeof(relativeOffset));

			if (MemX::WriteMemory(reinterpret_cast<LPVOID>(uiRawAddress), &code, static_cast<uint32_t>(uiRetAddress - uiRawAddress))) return TRUE;

			MessageBoxW(NULL, L"WriteHookCode Failed!!", NULL, NULL);

			return FALSE;
		}

		bool SetHook(uintptr_t uiRawAddr, uintptr_t uiTarAddr, uint32_t szRawSize)
		{
			DWORD old = 0;
			int32_t rva = 0;
			BYTE rawJmp[] = { 0xE9,0x00,0x00,0x00,0x00 };
			BYTE retJmp[] = { 0xE9,0x00,0x00,0x00,0x00 };
			BYTE tarCal[] = { 0xE8,0x00,0x00,0x00,0x00 };

			BOOL protect = VirtualProtect(reinterpret_cast<LPVOID>(uiRawAddr), 0x1000, PAGE_EXECUTE_READWRITE, &old);
			PBYTE alloc = (PBYTE)VirtualAlloc(NULL, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (alloc && protect)
			{
				//Copy the Code for the original address to alloc address
				memcpy(alloc, reinterpret_cast<PVOID>(uiRawAddr), szRawSize);

				//Write Jmp Code
				rva = static_cast<int32_t>(reinterpret_cast<intptr_t>(alloc) - static_cast<intptr_t>(uiRawAddr) - sizeof(rawJmp));
				memcpy(&rawJmp[1], &rva, sizeof(rva));
				memcpy(reinterpret_cast<PBYTE>(uiRawAddr), rawJmp, sizeof(rawJmp));

				//Write Call TarFunc Code
				rva = static_cast<int32_t>(static_cast<intptr_t>(uiTarAddr) - reinterpret_cast<intptr_t>(&alloc[szRawSize]) - sizeof(tarCal));
				memcpy(&tarCal[1], &rva, sizeof(rva));
				memcpy(&alloc[szRawSize], tarCal, sizeof(tarCal));

				//Write Ret Code
				rva = static_cast<int32_t>(static_cast<intptr_t>(uiRawAddr + szRawSize) - reinterpret_cast<intptr_t>(&alloc[szRawSize + sizeof(tarCal)]) - sizeof(retJmp));
				memcpy(&retJmp[1], &rva, sizeof(rva));
				memcpy(&alloc[szRawSize + sizeof(tarCal)], retJmp, sizeof(retJmp));

				return TRUE;
			}
			else
			{
				MessageBoxW(NULL, L"SetHook Failed!!", NULL, NULL);
				return FALSE;
			}
		}

		bool DetourAttachFunc(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			LONG erroAttach = DetourAttach((PVOID*)ppRawFunc, pNewFunc);
			LONG erroCommit = DetourTransactionCommit();

			if (erroAttach == NO_ERROR && erroCommit == NO_ERROR) return false;

			MessageBoxW(NULL, L"DetourAttachFunc Failed!!", NULL, NULL);

			return true;
		}

		bool DetourDetachFunc(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			LONG erroDetach = DetourDetach((PVOID*)ppRawFunc, pNewFunc);
			LONG erroCommit = DetourTransactionCommit();

			if (erroDetach == NO_ERROR && erroCommit == NO_ERROR) return false;

			MessageBoxW(NULL, L"DetourDetachFunc Failed!!", NULL, NULL);

			return true;
		}
	}
}
