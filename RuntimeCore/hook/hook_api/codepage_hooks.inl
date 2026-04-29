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

		int WINAPI newMultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
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

		bool HookMultiByteToWideChar()
		{
			return DetourAttachFunc(&rawMultiByteToWideChar, newMultiByteToWideChar);
		}
		//*********END Hook MultiByteToWideChar*********

		//*********Hook WideCharToMultiByte*********
		static pWideCharToMultiByte rawWideCharToMultiByte = WideCharToMultiByte;

		int WINAPI newWideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
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

		bool HookWideCharToMultiByte()
		{
			return DetourAttachFunc(&rawWideCharToMultiByte, newWideCharToMultiByte);
		}
		//*********END Hook WideCharToMultiByte*********

		// 启用代码页转换 Hook
		bool HookCodePageAPIs()
		{
			if (sg_uiFromCodePage == 0 || sg_uiToCodePage == 0)
			{
				return false;
			}

			bool hasFailed = false;
			hasFailed |= HookMultiByteToWideChar();
			hasFailed |= HookWideCharToMultiByte();
			return !hasFailed;
		}
		//*********END CodePage Conversion*********
