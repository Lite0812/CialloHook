#include "Mem.h"


namespace Rut
{
	namespace MemX
	{
		namespace
		{
			bool MakePageWritable(LPVOID address, SIZE_T size, DWORD& oldProtect)
			{
				if (!address || size == 0)
				{
					return false;
				}
				return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect) != FALSE;
			}

			void RestorePageProtection(LPVOID address, SIZE_T size, DWORD oldProtect)
			{
				if (!address || size == 0)
				{
					return;
				}
				DWORD ignored = 0;
				VirtualProtect(address, size, oldProtect, &ignored);
			}
		}

		BOOL WriteMemory(LPVOID lpAddress, LPCVOID lpBuffer, SIZE_T nSize)
		{
			if (!lpAddress || !lpBuffer || nSize == 0)
			{
				return FALSE;
			}

			DWORD oldProtect = 0;
			if (!MakePageWritable(lpAddress, nSize, oldProtect))
			{
				return FALSE;
			}

			memcpy(lpAddress, lpBuffer, nSize);
			FlushInstructionCache(GetCurrentProcess(), lpAddress, nSize);
			RestorePageProtection(lpAddress, nSize, oldProtect);
			return TRUE;
		}

		BOOL ReadMemory(LPVOID lpAddress, LPVOID lpBuffer, SIZE_T nSize)
		{
			if (!lpAddress || !lpBuffer || nSize == 0)
			{
				return FALSE;
			}

			__try
			{
				memcpy(lpBuffer, lpAddress, nSize);
				return TRUE;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return FALSE;
			}
		}

		uintptr_t MemSearch(uintptr_t pFind, SIZE_T szFind, PBYTE pToFind, SIZE_T szToFind, BOOL backward)
		{
			if (!pFind || !pToFind || !szFind || !szToFind || szFind < szToFind) return NULL;

			__try
			{
				if (!backward)
				{
					uintptr_t end = pFind + szFind - szToFind;
					for (uintptr_t current = pFind; current <= end; ++current)
					{
						if (!memcmp(pToFind, reinterpret_cast<void*>(current), szToFind)) return current;
					}
				}
				else
				{
					uintptr_t end = pFind - szFind + szToFind;
					for (uintptr_t current = pFind; current >= end; --current)
					{
						if (!memcmp(pToFind, reinterpret_cast<void*>(current), szToFind)) return current;
						if (current == 0)
						{
							break;
						}
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return NULL;
			}

			return NULL;
		}
	}
}
