#pragma once
#include <cstdint>

#include "Hook_API.h"

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		bool WriteHookCode(uintptr_t uiRawAddress, uintptr_t uiNewAddress, uint32_t szHookCode);
		bool WriteHookCode_RET(uintptr_t uiRawAddress, uintptr_t uiRetAddress, uintptr_t uiNewAddress);
		bool SetHook(uintptr_t uiRawAddr, uintptr_t uiTarAddr, uint32_t szRawSize);
		bool DetourAttachFunc(void* ppRawFunc, void* pNewFunc);
		bool DetourDetachFunc(void* ppRawFunc, void* pNewFunc);
	}
}
