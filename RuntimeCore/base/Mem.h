#pragma once
#include <Windows.h>
#include <cstdint>

//Ria's Utility Library X
namespace Rut
{
	namespace MemX
	{
		BOOL WriteMemory(LPVOID lpAddress, LPCVOID lpBuffer, SIZE_T nSize);
		BOOL ReadMemory(LPVOID lpAddress, LPVOID lpBuffer, SIZE_T nSize);
		uintptr_t MemSearch(uintptr_t pFind, SIZE_T szFind, PBYTE pToFind, SIZE_T szToFind, BOOL backward = FALSE);
	}
}
