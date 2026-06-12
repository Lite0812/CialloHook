		//*********START CodePage Conversion*********
		static UINT sg_uiFromCodePage = 0;
		static UINT sg_uiToCodePage = 0;

		// 设置代码页映射
		void SetCodePageMapping(uint32_t fromCodePage, uint32_t toCodePage)
		{
			sg_uiFromCodePage = fromCodePage;
			sg_uiToCodePage = toCodePage;
		}

		//*********Hook MultiByteToWideChar*********
		static pMultiByteToWideChar rawMultiByteToWideChar = MultiByteToWideChar;

		int WINAPI newMultiByteToWideChar_SehImpl(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
			if (IsHookRuntimeShuttingDown())
			{
				return rawMultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
			}
			// 如果代码页匹配源代码页，则替换为目标代码页
			if (sg_uiFromCodePage != 0 && sg_uiToCodePage != 0 && CodePage == sg_uiFromCodePage)
			{
				return rawMultiByteToWideChar(sg_uiToCodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
			}
			
			return rawMultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
		}
		int WINAPI newMultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
		{
			__try { return newMultiByteToWideChar_SehImpl(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawMultiByteToWideChar(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar); }
		}


		bool HookMultiByteToWideChar()
		{
			return TryDetourAttach(&rawMultiByteToWideChar, newMultiByteToWideChar);
		}
		//*********END Hook MultiByteToWideChar*********

		//*********Hook WideCharToMultiByte*********
		static pWideCharToMultiByte rawWideCharToMultiByte = WideCharToMultiByte;

		int WINAPI newWideCharToMultiByte_SehImpl(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
			if (IsHookRuntimeShuttingDown())
			{
				return rawWideCharToMultiByte(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
			}
			// 如果代码页匹配源代码页，则替换为目标代码页
			if (sg_uiFromCodePage != 0 && sg_uiToCodePage != 0 && CodePage == sg_uiFromCodePage)
			{
				return rawWideCharToMultiByte(sg_uiToCodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
			}
			
			return rawWideCharToMultiByte(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar);
		}
		int WINAPI newWideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
		{
			__try { return newWideCharToMultiByte_SehImpl(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawWideCharToMultiByte(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte, lpDefaultChar, lpUsedDefaultChar); }
		}


		bool HookWideCharToMultiByte()
		{
			return TryDetourAttach(&rawWideCharToMultiByte, newWideCharToMultiByte);
		}
		//*********END Hook WideCharToMultiByte*********

		// 启用代码页转换 Hook
		bool HookCodePageAPIs()
		{
			if (sg_uiFromCodePage == 0 || sg_uiToCodePage == 0)
			{
				return false;
			}

			bool allSucceeded = true;
			allSucceeded &= HookMultiByteToWideChar();
			allSucceeded &= HookWideCharToMultiByte();
			return allSucceeded;
		}

		//*********START UI Language Hooks*********
		static LANGID sg_uiLocaleId = 0;

		void SetLocaleEmulatorLanguage(uint32_t localeID)
		{
			sg_uiLocaleId = static_cast<LANGID>(localeID & 0xFFFF);
		}

		static pGetUserDefaultUILanguage rawGetUserDefaultUILanguage = GetUserDefaultUILanguage;
		static pGetSystemDefaultUILanguage rawGetSystemDefaultUILanguage = GetSystemDefaultUILanguage;
		static pGetThreadUILanguage rawGetThreadUILanguage = GetThreadUILanguage;
		static pGetUserDefaultLangID rawGetUserDefaultLangID = GetUserDefaultLangID;
		static pGetSystemDefaultLangID rawGetSystemDefaultLangID = GetSystemDefaultLangID;
		static pGetUserDefaultLCID rawGetUserDefaultLCID = GetUserDefaultLCID;
		static pGetSystemDefaultLCID rawGetSystemDefaultLCID = GetSystemDefaultLCID;
		static pGetThreadLocale rawGetThreadLocale = GetThreadLocale;

		static LANGID GetEffectiveLocaleId()
		{
			return sg_uiLocaleId != 0 ? sg_uiLocaleId : static_cast<LANGID>(0x0411);
		}

		LANGID WINAPI newGetUserDefaultUILanguage()
		{
			return IsHookRuntimeShuttingDown() ? rawGetUserDefaultUILanguage() : GetEffectiveLocaleId();
		}

		LANGID WINAPI newGetSystemDefaultUILanguage()
		{
			return IsHookRuntimeShuttingDown() ? rawGetSystemDefaultUILanguage() : GetEffectiveLocaleId();
		}

		LANGID WINAPI newGetThreadUILanguage()
		{
			return IsHookRuntimeShuttingDown() ? rawGetThreadUILanguage() : GetEffectiveLocaleId();
		}

		LANGID WINAPI newGetUserDefaultLangID()
		{
			return IsHookRuntimeShuttingDown() ? rawGetUserDefaultLangID() : GetEffectiveLocaleId();
		}

		LANGID WINAPI newGetSystemDefaultLangID()
		{
			return IsHookRuntimeShuttingDown() ? rawGetSystemDefaultLangID() : GetEffectiveLocaleId();
		}

		LCID WINAPI newGetUserDefaultLCID()
		{
			return IsHookRuntimeShuttingDown() ? rawGetUserDefaultLCID() : MAKELCID(GetEffectiveLocaleId(), SORT_DEFAULT);
		}

		LCID WINAPI newGetSystemDefaultLCID()
		{
			return IsHookRuntimeShuttingDown() ? rawGetSystemDefaultLCID() : MAKELCID(GetEffectiveLocaleId(), SORT_DEFAULT);
		}

		LCID WINAPI newGetThreadLocale()
		{
			return IsHookRuntimeShuttingDown() ? rawGetThreadLocale() : MAKELCID(GetEffectiveLocaleId(), SORT_DEFAULT);
		}

		bool HookUILanguageAPIs()
		{
			if (sg_uiLocaleId == 0)
			{
				return false;
			}

			bool allSucceeded = true;
			allSucceeded &= TryDetourAttach(&rawGetUserDefaultUILanguage, newGetUserDefaultUILanguage);
			allSucceeded &= TryDetourAttach(&rawGetSystemDefaultUILanguage, newGetSystemDefaultUILanguage);
			allSucceeded &= TryDetourAttach(&rawGetThreadUILanguage, newGetThreadUILanguage);
			allSucceeded &= TryDetourAttach(&rawGetUserDefaultLangID, newGetUserDefaultLangID);
			allSucceeded &= TryDetourAttach(&rawGetSystemDefaultLangID, newGetSystemDefaultLangID);
			allSucceeded &= TryDetourAttach(&rawGetUserDefaultLCID, newGetUserDefaultLCID);
			allSucceeded &= TryDetourAttach(&rawGetSystemDefaultLCID, newGetSystemDefaultLCID);
			allSucceeded &= TryDetourAttach(&rawGetThreadLocale, newGetThreadLocale);
			return allSucceeded;
		}
		//*********END UI Language Hooks*********

		//*********END CodePage Conversion*********
