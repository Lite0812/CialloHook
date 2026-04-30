#pragma once
#include <cstdint>

#include "Hook_API.h"

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		bool TryDetourAttach(void* ppRawFunc, void* pNewFunc);
		bool TryDetourDetach(void* ppRawFunc, void* pNewFunc);
	}
}
