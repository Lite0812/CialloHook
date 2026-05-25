		//*********START UI Text Replace*********
		static pMessageBoxA rawMessageBoxA_UiText = MessageBoxA;
		static pSetDlgItemTextA rawSetDlgItemTextA_UiText = SetDlgItemTextA;
		static pSendDlgItemMessageA rawSendDlgItemMessageA_UiText = SendDlgItemMessageA;
		static pSendDlgItemMessageW rawSendDlgItemMessageW_UiText = SendDlgItemMessageW;
		static pSendMessageA rawSendMessageA_UiText = SendMessageA;
		static pSendMessageW rawSendMessageW_UiText = SendMessageW;
		static pDefWindowProcA rawDefWindowProcA_UiText = DefWindowProcA;
		static pDefWindowProcW rawDefWindowProcW_UiText = DefWindowProcW;
		static pAppendMenuA rawAppendMenuA_UiText = AppendMenuA;
		static pModifyMenuA rawModifyMenuA_UiText = ModifyMenuA;
		static pInsertMenuA rawInsertMenuA_UiText = InsertMenuA;
		static pInsertMenuItemA rawInsertMenuItemA_UiText = InsertMenuItemA;
		static pSetMenuItemInfoA rawSetMenuItemInfoA_UiText = SetMenuItemInfoA;
		static pDialogBoxParamA rawDialogBoxParamA_UiText = DialogBoxParamA;
		static pDialogBoxParamW rawDialogBoxParamW_UiText = DialogBoxParamW;
		static pCreateDialogParamA rawCreateDialogParamA_UiText = CreateDialogParamA;
		static pCreateDialogParamW rawCreateDialogParamW_UiText = CreateDialogParamW;
		static pDialogBoxIndirectParamA rawDialogBoxIndirectParamA_UiText = DialogBoxIndirectParamA;
		static pDialogBoxIndirectParamW rawDialogBoxIndirectParamW_UiText = DialogBoxIndirectParamW;
		static pCreateDialogIndirectParamA rawCreateDialogIndirectParamA_UiText = CreateDialogIndirectParamA;
		static pCreateDialogIndirectParamW rawCreateDialogIndirectParamW_UiText = CreateDialogIndirectParamW;
		static pMessageBoxIndirectA rawMessageBoxIndirectA_UiText = MessageBoxIndirectA;
		static pDrawThemeText rawDrawThemeText_UiText = nullptr;
		static pDrawThemeTextEx rawDrawThemeTextEx_UiText = nullptr;
		static pPropertySheetA rawPropertySheetA_UiText = nullptr;
		static pExitProcess rawExitProcessGuard_UiText = ExitProcess;
		static volatile LONG sg_exitProcessGuardEntered = 0;

		struct UiDialogProcContext
		{
			DLGPROC originalProc = nullptr;
			LPARAM originalInitParam = 0;
			bool translated = false;
		};

		static bool TryBuildUiReplacementA(const char* text, int length, const wchar_t* apiName, std::string& replacedAnsi, std::wstring& replacedWide)
		{
			replacedAnsi.clear();
			replacedWide.clear();
			if (!text || length < 0)
			{
				return false;
			}

			replacedAnsi = ProcessTextReplaceA(text, length);
			if (replacedAnsi.size() == static_cast<size_t>(length)
				&& memcmp(replacedAnsi.data(), text, static_cast<size_t>(length)) == 0)
			{
				replacedAnsi.clear();
				return false;
			}

			LogTextReplaceApiHitA(apiName, std::string(text, length), replacedAnsi);
			replacedWide = MultiByteToWideWithCodePage(replacedAnsi, sg_textWriteCodePage);
			return true;
		}

		static bool TryBuildUiReplacementW(const wchar_t* text, int length, const wchar_t* apiName, std::wstring& replacedWide)
		{
			replacedWide.clear();
			if (!text || length < 0)
			{
				return false;
			}

			replacedWide = ProcessTextReplaceW(text, length);
			if (replacedWide.size() == static_cast<size_t>(length)
				&& wmemcmp(replacedWide.data(), text, static_cast<size_t>(length)) == 0)
			{
				replacedWide.clear();
				return false;
			}

			LogTextReplaceApiHitW(apiName, std::wstring(text, length), replacedWide);
			return true;
		}

		static bool IsUiSendMessageTextMessage(UINT msg)
		{
			switch (msg)
			{
			case WM_SETTEXT:
			case CB_ADDSTRING:
			case CB_INSERTSTRING:
			case LB_ADDSTRING:
			case LB_INSERTSTRING:
				return true;
			default:
				return false;
			}
		}

		static bool IsUiSendMessageTextQuery(UINT msg)
		{
			switch (msg)
			{
			case WM_GETTEXT:
			case LB_GETTEXT:
			case CB_GETLBTEXT:
				return true;
			default:
				return false;
			}
		}

		static bool IsUiSendMessageTextLengthQuery(UINT msg)
		{
			switch (msg)
			{
			case WM_GETTEXTLENGTH:
			case LB_GETTEXTLEN:
			case CB_GETLBTEXTLEN:
				return true;
			default:
				return false;
			}
		}

		static bool IsAnsiCallbackText(const char* text)
		{
			return text == LPSTR_TEXTCALLBACKA || reinterpret_cast<INT_PTR>(text) == reinterpret_cast<INT_PTR>(LPSTR_TEXTCALLBACKA);
		}

		static bool IsWideCallbackText(const wchar_t* text)
		{
			return text == LPSTR_TEXTCALLBACKW || reinterpret_cast<INT_PTR>(text) == reinterpret_cast<INT_PTR>(LPSTR_TEXTCALLBACKW);
		}

		static bool TryReplaceTreeViewItemTextA(TVITEMA& item, const wchar_t* apiName, std::string& replacedA)
		{
			if ((item.mask & TVIF_TEXT) == 0 || !item.pszText || IsAnsiCallbackText(item.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			const int length = item.cchTextMax > 0 ? item.cchTextMax : lstrlenA(item.pszText);
			if (!TryBuildUiReplacementA(item.pszText, length, apiName, replacedA, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPSTR>(replacedA.c_str());
			item.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceTreeViewItemTextW(TVITEMW& item, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((item.mask & TVIF_TEXT) == 0 || !item.pszText || IsWideCallbackText(item.pszText))
			{
				return false;
			}
			const int length = item.cchTextMax > 0 ? item.cchTextMax : lstrlenW(item.pszText);
			if (!TryBuildUiReplacementW(item.pszText, length, apiName, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPWSTR>(replacedW.c_str());
			item.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryReplaceListViewItemTextA(LVITEMA& item, const wchar_t* apiName, std::string& replacedA)
		{
			if ((item.mask & LVIF_TEXT) == 0 || !item.pszText || IsAnsiCallbackText(item.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			const int length = item.cchTextMax > 0 ? item.cchTextMax : lstrlenA(item.pszText);
			if (!TryBuildUiReplacementA(item.pszText, length, apiName, replacedA, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPSTR>(replacedA.c_str());
			item.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceListViewItemTextW(LVITEMW& item, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((item.mask & LVIF_TEXT) == 0 || !item.pszText || IsWideCallbackText(item.pszText))
			{
				return false;
			}
			const int length = item.cchTextMax > 0 ? item.cchTextMax : lstrlenW(item.pszText);
			if (!TryBuildUiReplacementW(item.pszText, length, apiName, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPWSTR>(replacedW.c_str());
			item.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryReplaceListViewColumnTextA(LVCOLUMNA& column, const wchar_t* apiName, std::string& replacedA)
		{
			if ((column.mask & LVCF_TEXT) == 0 || !column.pszText || IsAnsiCallbackText(column.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			const int length = column.cchTextMax > 0 ? column.cchTextMax : lstrlenA(column.pszText);
			if (!TryBuildUiReplacementA(column.pszText, length, apiName, replacedA, replacedW))
			{
				return false;
			}
			column.pszText = const_cast<LPSTR>(replacedA.c_str());
			column.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceListViewColumnTextW(LVCOLUMNW& column, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((column.mask & LVCF_TEXT) == 0 || !column.pszText || IsWideCallbackText(column.pszText))
			{
				return false;
			}
			const int length = column.cchTextMax > 0 ? column.cchTextMax : lstrlenW(column.pszText);
			if (!TryBuildUiReplacementW(column.pszText, length, apiName, replacedW))
			{
				return false;
			}
			column.pszText = const_cast<LPWSTR>(replacedW.c_str());
			column.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryCopyUiReplacementA(char* buffer, int cchTextMax, const std::string& replacedA, bool returnCopyLen, LRESULT originalResult, LRESULT& outResult)
		{
			if (!buffer || cchTextMax <= 0)
			{
				return false;
			}
			size_t capacity = static_cast<size_t>(cchTextMax - 1);
			if (capacity == 0)
			{
				return false;
			}
			size_t copyLen = replacedA.size() < capacity ? replacedA.size() : capacity;
			memcpy(buffer, replacedA.data(), copyLen);
			buffer[copyLen] = '\0';
			outResult = returnCopyLen ? static_cast<LRESULT>(copyLen) : originalResult;
			return true;
		}

		static bool TryCopyUiReplacementW(wchar_t* buffer, int cchTextMax, const std::wstring& replacedW, bool returnCopyLen, LRESULT originalResult, LRESULT& outResult)
		{
			if (!buffer || cchTextMax <= 0)
			{
				return false;
			}
			size_t capacity = static_cast<size_t>(cchTextMax - 1);
			if (capacity == 0)
			{
				return false;
			}
			size_t copyLen = replacedW.size() < capacity ? replacedW.size() : capacity;
			wmemcpy(buffer, replacedW.data(), copyLen);
			buffer[copyLen] = L'\0';
			outResult = returnCopyLen ? static_cast<LRESULT>(copyLen) : originalResult;
			return true;
		}

		static bool TryReplaceQueriedTextA(char* buffer, int cchTextMax, const wchar_t* apiName, bool returnCopyLen, LRESULT originalResult, LRESULT& outResult)
		{
			if (!buffer || cchTextMax <= 0)
			{
				return false;
			}
			int length = lstrlenA(buffer);
			if (length <= 0)
			{
				return false;
			}
			std::string replacedA;
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(buffer, length, apiName, replacedA, replacedW))
			{
				return false;
			}
			return TryCopyUiReplacementA(buffer, cchTextMax, replacedA, returnCopyLen, originalResult, outResult);
		}

		static bool TryReplaceQueriedTextW(wchar_t* buffer, int cchTextMax, const wchar_t* apiName, bool returnCopyLen, LRESULT originalResult, LRESULT& outResult)
		{
			if (!buffer || cchTextMax <= 0)
			{
				return false;
			}
			int length = lstrlenW(buffer);
			if (length <= 0)
			{
				return false;
			}
			std::wstring replacedW;
			if (!TryBuildUiReplacementW(buffer, length, apiName, replacedW))
			{
				return false;
			}
			return TryCopyUiReplacementW(buffer, cchTextMax, replacedW, returnCopyLen, originalResult, outResult);
		}

		static bool TryReplaceComboBoxExItemTextA(COMBOBOXEXITEMA& item, const wchar_t* apiName, std::string& replacedA)
		{
			if ((item.mask & CBEIF_TEXT) == 0 || !item.pszText || IsAnsiCallbackText(item.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(item.pszText, lstrlenA(item.pszText), apiName, replacedA, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPSTR>(replacedA.c_str());
			item.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceComboBoxExItemTextW(COMBOBOXEXITEMW& item, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((item.mask & CBEIF_TEXT) == 0 || !item.pszText || IsWideCallbackText(item.pszText))
			{
				return false;
			}
			if (!TryBuildUiReplacementW(item.pszText, lstrlenW(item.pszText), apiName, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPWSTR>(replacedW.c_str());
			item.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryReplaceHeaderItemTextA(HDITEMA& item, const wchar_t* apiName, std::string& replacedA)
		{
			if ((item.mask & HDI_TEXT) == 0 || !item.pszText || IsAnsiCallbackText(item.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(item.pszText, lstrlenA(item.pszText), apiName, replacedA, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPSTR>(replacedA.c_str());
			item.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceHeaderItemTextW(HDITEMW& item, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((item.mask & HDI_TEXT) == 0 || !item.pszText || IsWideCallbackText(item.pszText))
			{
				return false;
			}
			if (!TryBuildUiReplacementW(item.pszText, lstrlenW(item.pszText), apiName, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPWSTR>(replacedW.c_str());
			item.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryReplaceTabItemTextA(TCITEMA& item, const wchar_t* apiName, std::string& replacedA)
		{
			if ((item.mask & TCIF_TEXT) == 0 || !item.pszText || IsAnsiCallbackText(item.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(item.pszText, lstrlenA(item.pszText), apiName, replacedA, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPSTR>(replacedA.c_str());
			item.cchTextMax = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceTabItemTextW(TCITEMW& item, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((item.mask & TCIF_TEXT) == 0 || !item.pszText || IsWideCallbackText(item.pszText))
			{
				return false;
			}
			if (!TryBuildUiReplacementW(item.pszText, lstrlenW(item.pszText), apiName, replacedW))
			{
				return false;
			}
			item.pszText = const_cast<LPWSTR>(replacedW.c_str());
			item.cchTextMax = static_cast<int>(replacedW.size());
			return true;
		}

		static bool TryReplaceToolbarButtonInfoTextA(TBBUTTONINFOA& buttonInfo, const wchar_t* apiName, std::string& replacedA)
		{
			if ((buttonInfo.dwMask & TBIF_TEXT) == 0 || !buttonInfo.pszText || IsAnsiCallbackText(buttonInfo.pszText))
			{
				return false;
			}
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(buttonInfo.pszText, lstrlenA(buttonInfo.pszText), apiName, replacedA, replacedW))
			{
				return false;
			}
			buttonInfo.pszText = const_cast<LPSTR>(replacedA.c_str());
			buttonInfo.cchText = static_cast<int>(replacedA.size());
			return true;
		}

		static bool TryReplaceToolbarButtonInfoTextW(TBBUTTONINFOW& buttonInfo, const wchar_t* apiName, std::wstring& replacedW)
		{
			if ((buttonInfo.dwMask & TBIF_TEXT) == 0 || !buttonInfo.pszText || IsWideCallbackText(buttonInfo.pszText))
			{
				return false;
			}
			if (!TryBuildUiReplacementW(buttonInfo.pszText, lstrlenW(buttonInfo.pszText), apiName, replacedW))
			{
				return false;
			}
			buttonInfo.pszText = const_cast<LPWSTR>(replacedW.c_str());
			buttonInfo.cchText = static_cast<int>(replacedW.size());
			return true;
		}

		static bool IsMenuStringFlags(UINT flags)
		{
			return (flags & (MF_BITMAP | MF_OWNERDRAW)) == 0;
		}

		static MENUITEMINFOW BuildMenuItemInfoW(const MENUITEMINFOA& source, std::wstring& replacedText)
		{
			MENUITEMINFOW target = {};
			target.cbSize = sizeof(target);
			target.fMask = source.fMask;
			target.fType = source.fType;
			target.fState = source.fState;
			target.wID = source.wID;
			target.hSubMenu = source.hSubMenu;
			target.hbmpChecked = source.hbmpChecked;
			target.hbmpUnchecked = source.hbmpUnchecked;
			target.dwItemData = source.dwItemData;
			target.dwTypeData = replacedText.empty() ? nullptr : const_cast<LPWSTR>(replacedText.c_str());
			target.cch = static_cast<UINT>(replacedText.size());
			target.hbmpItem = source.hbmpItem;
			return target;
		}

		static bool ResolveUxThemeApis()
		{
			HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
			if (!hUxTheme)
			{
				hUxTheme = LoadLibraryW(L"uxtheme.dll");
			}
			if (!hUxTheme)
			{
				return false;
			}

			if (!rawDrawThemeText_UiText)
			{
				rawDrawThemeText_UiText = reinterpret_cast<pDrawThemeText>(GetProcAddress(hUxTheme, "DrawThemeText"));
			}
			if (!rawDrawThemeTextEx_UiText)
			{
				rawDrawThemeTextEx_UiText = reinterpret_cast<pDrawThemeTextEx>(GetProcAddress(hUxTheme, "DrawThemeTextEx"));
			}
			return rawDrawThemeText_UiText != nullptr || rawDrawThemeTextEx_UiText != nullptr;
		}

		static void TryReplaceExistingWindowText(HWND hWnd, const wchar_t* apiName)
		{
			if (!hWnd || !IsWindow(hWnd))
			{
				return;
			}
			wchar_t buffer[1024] = {};
			int length = GetWindowTextW(hWnd, buffer, _countof(buffer));
			if (length <= 0)
			{
				return;
			}
			std::wstring replacedW;
			if (TryBuildUiReplacementW(buffer, length, apiName, replacedW))
			{
				SetWindowTextW(hWnd, replacedW.c_str());
			}
		}

		static BOOL CALLBACK TranslateDialogChildWindowProc(HWND hWnd, LPARAM lParam)
		{
			const wchar_t* apiName = reinterpret_cast<const wchar_t*>(lParam);
			TryReplaceExistingWindowText(hWnd, apiName ? apiName : L"DialogChild");
			return TRUE;
		}

		static void TranslateDialogWindowTree(HWND hWnd, const wchar_t* apiName)
		{
			TryReplaceExistingWindowText(hWnd, apiName);
			EnumChildWindows(hWnd, TranslateDialogChildWindowProc, reinterpret_cast<LPARAM>(apiName));
		}

		static INT_PTR CALLBACK UiDialogProcThunk(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
		{
			UiDialogProcContext* context = reinterpret_cast<UiDialogProcContext*>(GetWindowLongPtrW(hWnd, DWLP_USER));
			if (uMsg == WM_INITDIALOG)
			{
				context = reinterpret_cast<UiDialogProcContext*>(lParam);
				if (context)
				{
					SetWindowLongPtrW(hWnd, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
				}
			}

			INT_PTR result = FALSE;
			if (context && context->originalProc)
			{
				result = context->originalProc(hWnd, uMsg, wParam, uMsg == WM_INITDIALOG ? context->originalInitParam : lParam);
			}
			if (context && uMsg == WM_INITDIALOG && !context->translated)
			{
				context->translated = true;
				TranslateDialogWindowTree(hWnd, L"DialogInit");
			}
			if (uMsg == WM_NCDESTROY && context)
			{
				SetWindowLongPtrW(hWnd, DWLP_USER, 0);
				delete context;
			}
			return result;
		}

		INT WINAPI newMessageBoxA_UiText_SehImpl(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
		{
			std::string replacedTextA;
			std::string replacedCaptionA;
			std::wstring replacedTextW;
			std::wstring replacedCaptionW;
			const int textLen = lpText ? lstrlenA(lpText) : -1;
			const int captionLen = lpCaption ? lstrlenA(lpCaption) : -1;
			bool textChanged = TryBuildUiReplacementA(lpText, textLen, L"MessageBoxA.Text", replacedTextA, replacedTextW);
			bool captionChanged = TryBuildUiReplacementA(lpCaption, captionLen, L"MessageBoxA.Caption", replacedCaptionA, replacedCaptionW);
			if (!textChanged && !captionChanged)
			{
				return rawMessageBoxA_UiText(hWnd, lpText, lpCaption, uType);
			}

			LPCWSTR textPtr = textChanged ? replacedTextW.c_str() : nullptr;
			LPCWSTR captionPtr = captionChanged ? replacedCaptionW.c_str() : nullptr;
			if (!textChanged && lpText)
			{
				replacedTextW = MultiByteToWideWithCodePage(std::string(lpText, textLen), sg_textReadCodePage);
				textPtr = replacedTextW.c_str();
			}
			if (!captionChanged && lpCaption)
			{
				replacedCaptionW = MultiByteToWideWithCodePage(std::string(lpCaption, captionLen), sg_textReadCodePage);
				captionPtr = replacedCaptionW.c_str();
			}
			return MessageBoxW(hWnd, textPtr, captionPtr, uType);
		}

		INT WINAPI newMessageBoxA_UiText(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
		{
			__try { return newMessageBoxA_UiText_SehImpl(hWnd, lpText, lpCaption, uType); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawMessageBoxA_UiText(hWnd, lpText, lpCaption, uType); }
		}

		BOOL WINAPI newSetDlgItemTextA_UiText_SehImpl(HWND hDlg, int nIDDlgItem, LPCSTR lpString)
		{
			if (!lpString)
			{
				return rawSetDlgItemTextA_UiText(hDlg, nIDDlgItem, lpString);
			}

			std::string replacedA;
			std::wstring replacedW;
			const int length = lstrlenA(lpString);
			if (!TryBuildUiReplacementA(lpString, length, L"SetDlgItemTextA", replacedA, replacedW))
			{
				return rawSetDlgItemTextA_UiText(hDlg, nIDDlgItem, lpString);
			}
			return SetDlgItemTextW(hDlg, nIDDlgItem, replacedW.c_str());
		}

		BOOL WINAPI newSetDlgItemTextA_UiText(HWND hDlg, int nIDDlgItem, LPCSTR lpString)
		{
			__try { return newSetDlgItemTextA_UiText_SehImpl(hDlg, nIDDlgItem, lpString); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawSetDlgItemTextA_UiText(hDlg, nIDDlgItem, lpString); }
		}

		/* Validate that lParam is a plausible user-mode pointer before struct dereference.
		 * Windows guarantees the first 64KB (0x10000) of address space is never mapped.
		 * Game engines (e.g. Majiro) may send SendMessageA where Msg collides with
		 * common control messages (LVM_*, TVM_*, etc.) but lParam holds a char value
		 * like 0x64 instead of a struct pointer — this must not be dereferenced. */
		static bool IsPlausibleStructPtr(LPARAM lParam)
		{
			return (reinterpret_cast<ULONG_PTR>(reinterpret_cast<void*>(lParam)) >= 0x10000);
		}

		static bool TryHandleSpecializedSendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
			{
				std::string replacedA;
				switch (Msg)
				{
				case CBEM_INSERTITEMA:
				case CBEM_SETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					COMBOBOXEXITEMA item = *reinterpret_cast<COMBOBOXEXITEMA*>(lParam);
					if (!TryReplaceComboBoxExItemTextA(item, L"ComboBoxExItemA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case CBEM_GETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					COMBOBOXEXITEMA* item = reinterpret_cast<COMBOBOXEXITEMA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & CBEIF_TEXT) != 0)
					{
						TryReplaceQueriedTextA(item->pszText, item->cchTextMax, L"CBEM_GETITEMA", false, outResult, outResult);
					}
					return true;
				}
				case TVM_SETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVITEMA item = *reinterpret_cast<TVITEMA*>(lParam);
					if (!TryReplaceTreeViewItemTextA(item, L"TVM_SETITEMA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case TVM_INSERTITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVINSERTSTRUCTA insert = *reinterpret_cast<TVINSERTSTRUCTA*>(lParam);
					if (!TryReplaceTreeViewItemTextA(insert.item, L"TVM_INSERTITEMA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&insert));
					return true;
				}
				case TVM_GETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVITEMA* item = reinterpret_cast<TVITEMA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & TVIF_TEXT) != 0)
					{
						TryReplaceQueriedTextA(item->pszText, item->cchTextMax, L"TVM_GETITEMA", false, outResult, outResult);
					}
					return true;
				}
				case LVM_SETITEMA:
				case LVM_INSERTITEMA:
				case LVM_SETITEMTEXTA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVITEMA item = *reinterpret_cast<LVITEMA*>(lParam);
					if (!TryReplaceListViewItemTextA(item, L"ListViewItemA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case LVM_GETITEMA:
				case LVM_GETITEMTEXTA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVITEMA* item = reinterpret_cast<LVITEMA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (Msg == LVM_GETITEMTEXTA || (outResult && (item->mask & LVIF_TEXT) != 0))
					{
						TryReplaceQueriedTextA(item->pszText, item->cchTextMax, Msg == LVM_GETITEMTEXTA ? L"LVM_GETITEMTEXTA" : L"LVM_GETITEMA", Msg == LVM_GETITEMTEXTA, outResult, outResult);
					}
					return true;
				}
				case LVM_SETCOLUMNA:
				case LVM_INSERTCOLUMNA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVCOLUMNA column = *reinterpret_cast<LVCOLUMNA*>(lParam);
					if (!TryReplaceListViewColumnTextA(column, L"ListViewColumnA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&column));
					return true;
				}
				case HDM_SETITEMA:
				case HDM_INSERTITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					HDITEMA item = *reinterpret_cast<HDITEMA*>(lParam);
					if (!TryReplaceHeaderItemTextA(item, L"HeaderItemA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case HDM_GETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					HDITEMA* item = reinterpret_cast<HDITEMA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & HDI_TEXT) != 0)
					{
						TryReplaceQueriedTextA(item->pszText, item->cchTextMax, L"HDM_GETITEMA", false, outResult, outResult);
					}
					return true;
				}
				case TCM_SETITEMA:
				case TCM_INSERTITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TCITEMA item = *reinterpret_cast<TCITEMA*>(lParam);
					if (!TryReplaceTabItemTextA(item, L"TabItemA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case TCM_GETITEMA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TCITEMA* item = reinterpret_cast<TCITEMA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & TCIF_TEXT) != 0)
					{
						TryReplaceQueriedTextA(item->pszText, item->cchTextMax, L"TCM_GETITEMA", false, outResult, outResult);
					}
					return true;
				}
				case TB_ADDSTRINGA:
				{
					if (wParam || !IsPlausibleStructPtr(lParam)) return false;
					LPCSTR sourceText = reinterpret_cast<LPCSTR>(lParam);
					std::wstring replacedW;
					if (!TryBuildUiReplacementA(sourceText, lstrlenA(sourceText), L"TB_ADDSTRINGA", replacedA, replacedW)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedA.c_str()));
					return true;
				}
				case TB_SETBUTTONINFOA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TBBUTTONINFOA buttonInfo = *reinterpret_cast<TBBUTTONINFOA*>(lParam);
					if (!TryReplaceToolbarButtonInfoTextA(buttonInfo, L"TB_SETBUTTONINFOA", replacedA)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&buttonInfo));
					return true;
				}
				case TB_GETBUTTONINFOA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TBBUTTONINFOA* buttonInfo = reinterpret_cast<TBBUTTONINFOA*>(lParam);
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult >= 0 && (buttonInfo->dwMask & TBIF_TEXT) != 0)
					{
						TryReplaceQueriedTextA(buttonInfo->pszText, buttonInfo->cchText, L"TB_GETBUTTONINFOA", false, outResult, outResult);
					}
					return true;
				}
				case TB_GETBUTTONTEXTA:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					outResult = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
					if (outResult > 0)
					{
						TryReplaceQueriedTextA(reinterpret_cast<char*>(lParam), static_cast<int>(outResult) + 1, L"TB_GETBUTTONTEXTA", true, outResult, outResult);
					}
					return true;
				}
				default:
					return false;
				}
			}

			static bool TryHandleSpecializedSendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
			{
				std::wstring replacedW;
				switch (Msg)
				{
				case CBEM_INSERTITEMW:
				case CBEM_SETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					COMBOBOXEXITEMW item = *reinterpret_cast<COMBOBOXEXITEMW*>(lParam);
					if (!TryReplaceComboBoxExItemTextW(item, L"ComboBoxExItemW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case CBEM_GETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					COMBOBOXEXITEMW* item = reinterpret_cast<COMBOBOXEXITEMW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & CBEIF_TEXT) != 0)
					{
						TryReplaceQueriedTextW(item->pszText, item->cchTextMax, L"CBEM_GETITEMW", false, outResult, outResult);
					}
					return true;
				}
				case TVM_SETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVITEMW item = *reinterpret_cast<TVITEMW*>(lParam);
					if (!TryReplaceTreeViewItemTextW(item, L"TVM_SETITEMW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case TVM_INSERTITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVINSERTSTRUCTW insert = *reinterpret_cast<TVINSERTSTRUCTW*>(lParam);
					if (!TryReplaceTreeViewItemTextW(insert.item, L"TVM_INSERTITEMW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&insert));
					return true;
				}
				case TVM_GETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TVITEMW* item = reinterpret_cast<TVITEMW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & TVIF_TEXT) != 0)
					{
						TryReplaceQueriedTextW(item->pszText, item->cchTextMax, L"TVM_GETITEMW", false, outResult, outResult);
					}
					return true;
				}
				case LVM_SETITEMW:
				case LVM_INSERTITEMW:
				case LVM_SETITEMTEXTW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVITEMW item = *reinterpret_cast<LVITEMW*>(lParam);
					if (!TryReplaceListViewItemTextW(item, L"ListViewItemW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case LVM_GETITEMW:
				case LVM_GETITEMTEXTW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVITEMW* item = reinterpret_cast<LVITEMW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (Msg == LVM_GETITEMTEXTW || (outResult && (item->mask & LVIF_TEXT) != 0))
					{
						TryReplaceQueriedTextW(item->pszText, item->cchTextMax, Msg == LVM_GETITEMTEXTW ? L"LVM_GETITEMTEXTW" : L"LVM_GETITEMW", Msg == LVM_GETITEMTEXTW, outResult, outResult);
					}
					return true;
				}
				case LVM_SETCOLUMNW:
				case LVM_INSERTCOLUMNW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					LVCOLUMNW column = *reinterpret_cast<LVCOLUMNW*>(lParam);
					if (!TryReplaceListViewColumnTextW(column, L"ListViewColumnW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&column));
					return true;
				}
				case HDM_SETITEMW:
				case HDM_INSERTITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					HDITEMW item = *reinterpret_cast<HDITEMW*>(lParam);
					if (!TryReplaceHeaderItemTextW(item, L"HeaderItemW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case HDM_GETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					HDITEMW* item = reinterpret_cast<HDITEMW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & HDI_TEXT) != 0)
					{
						TryReplaceQueriedTextW(item->pszText, item->cchTextMax, L"HDM_GETITEMW", false, outResult, outResult);
					}
					return true;
				}
				case TCM_SETITEMW:
				case TCM_INSERTITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TCITEMW item = *reinterpret_cast<TCITEMW*>(lParam);
					if (!TryReplaceTabItemTextW(item, L"TabItemW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&item));
					return true;
				}
				case TCM_GETITEMW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TCITEMW* item = reinterpret_cast<TCITEMW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult && (item->mask & TCIF_TEXT) != 0)
					{
						TryReplaceQueriedTextW(item->pszText, item->cchTextMax, L"TCM_GETITEMW", false, outResult, outResult);
					}
					return true;
				}
				case TB_ADDSTRINGW:
				{
					if (wParam || !IsPlausibleStructPtr(lParam)) return false;
					LPCWSTR sourceText = reinterpret_cast<LPCWSTR>(lParam);
					if (!TryBuildUiReplacementW(sourceText, lstrlenW(sourceText), L"TB_ADDSTRINGW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedW.c_str()));
					return true;
				}
				case TB_SETBUTTONINFOW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TBBUTTONINFOW buttonInfo = *reinterpret_cast<TBBUTTONINFOW*>(lParam);
					if (!TryReplaceToolbarButtonInfoTextW(buttonInfo, L"TB_SETBUTTONINFOW", replacedW)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(&buttonInfo));
					return true;
				}
				case TB_GETBUTTONINFOW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					TBBUTTONINFOW* buttonInfo = reinterpret_cast<TBBUTTONINFOW*>(lParam);
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult >= 0 && (buttonInfo->dwMask & TBIF_TEXT) != 0)
					{
						TryReplaceQueriedTextW(buttonInfo->pszText, buttonInfo->cchText, L"TB_GETBUTTONINFOW", false, outResult, outResult);
					}
					return true;
				}
				case TB_GETBUTTONTEXTW:
				{
					if (!IsPlausibleStructPtr(lParam)) return false;
					outResult = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (outResult > 0)
					{
						TryReplaceQueriedTextW(reinterpret_cast<wchar_t*>(lParam), static_cast<int>(outResult) + 1, L"TB_GETBUTTONTEXTW", true, outResult, outResult);
					}
					return true;
				}
				default:
					return false;
				}
			}

			LRESULT WINAPI newSendMessageA_UiText_SehImpl(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			LRESULT specializedResult = 0;
				if (TryHandleSpecializedSendMessageA(hWnd, Msg, wParam, lParam, specializedResult))
				{
					return specializedResult;
				}
				if (IsUiSendMessageTextMessage(Msg) && lParam != 0)
			{
				LPCSTR sourceText = reinterpret_cast<LPCSTR>(lParam);
				const int length = lstrlenA(sourceText);
				std::string replacedA;
				std::wstring replacedW;
				if (!TryBuildUiReplacementA(sourceText, length, L"SendMessageA", replacedA, replacedW))
				{
					return rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
				}
				return SendMessageW(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedW.c_str()));
			}

			if (IsUiSendMessageTextQuery(Msg) && lParam != 0)
			{
				LRESULT result = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
				char* buffer = reinterpret_cast<char*>(lParam);
				int length = 0;
				if (Msg == WM_GETTEXT)
				{
					length = static_cast<int>(result);
				}
				else
				{
					length = lstrlenA(buffer);
				}
				if (length <= 0)
				{
					return result;
				}
				std::string replacedA;
				std::wstring replacedW;
				if (!TryBuildUiReplacementA(buffer, length, L"SendMessageA.Query", replacedA, replacedW))
				{
					return result;
				}
				size_t capacity = 0;
				if (Msg == WM_GETTEXT)
				{
					capacity = wParam > 0 ? static_cast<size_t>(wParam - 1) : 0;
				}
				else
				{
					capacity = replacedA.size();
				}
				if (capacity == 0)
				{
					return result;
				}
				size_t copyLen = replacedA.size() < capacity ? replacedA.size() : capacity;
				memcpy(buffer, replacedA.data(), copyLen);
				buffer[copyLen] = '\0';
				return static_cast<LRESULT>(copyLen);
			}

			if (IsUiSendMessageTextLengthQuery(Msg))
			{
				LRESULT result = rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
				if (result <= 0)
				{
					return result;
				}
				std::vector<char> buffer(static_cast<size_t>(result) + 4, '\0');
				UINT textMsg = Msg == WM_GETTEXTLENGTH ? WM_GETTEXT : (Msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT);
				LRESULT queryResult = rawSendMessageA_UiText(hWnd, textMsg, static_cast<WPARAM>(buffer.size()), reinterpret_cast<LPARAM>(buffer.data()));
				int length = queryResult > 0 ? static_cast<int>(queryResult) : lstrlenA(buffer.data());
				if (length <= 0)
				{
					return result;
				}
				std::string replacedA;
				std::wstring replacedW;
				if (!TryBuildUiReplacementA(buffer.data(), length, L"SendMessageA.LengthQuery", replacedA, replacedW))
				{
					return result;
				}
				return static_cast<LRESULT>(replacedA.size());
			}

			return rawSendMessageA_UiText(hWnd, Msg, wParam, lParam);
		}

		LRESULT WINAPI newSendMessageA_UiText(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			__try { return newSendMessageA_UiText_SehImpl(hWnd, Msg, wParam, lParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawSendMessageA_UiText(hWnd, Msg, wParam, lParam); }
		}

		LRESULT WINAPI newSendMessageW_UiText_SehImpl(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				LRESULT specializedResult = 0;
				if (TryHandleSpecializedSendMessageW(hWnd, Msg, wParam, lParam, specializedResult))
				{
					return specializedResult;
				}
				if (IsUiSendMessageTextMessage(Msg) && lParam != 0)
				{
					LPCWSTR sourceText = reinterpret_cast<LPCWSTR>(lParam);
					const int length = lstrlenW(sourceText);
					std::wstring replacedW;
					if (!TryBuildUiReplacementW(sourceText, length, L"SendMessageW", replacedW))
					{
						return rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					}
					return rawSendMessageW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedW.c_str()));
				}
				if (IsUiSendMessageTextQuery(Msg) && lParam != 0)
				{
					LRESULT result = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					wchar_t* buffer = reinterpret_cast<wchar_t*>(lParam);
					int length = Msg == WM_GETTEXT ? static_cast<int>(result) : lstrlenW(buffer);
					if (length <= 0) return result;
					std::wstring replacedW;
					if (!TryBuildUiReplacementW(buffer, length, L"SendMessageW.Query", replacedW)) return result;
					size_t capacity = Msg == WM_GETTEXT ? (wParam > 0 ? static_cast<size_t>(wParam - 1) : 0) : replacedW.size();
					if (capacity == 0) return result;
					size_t copyLen = replacedW.size() < capacity ? replacedW.size() : capacity;
					wmemcpy(buffer, replacedW.data(), copyLen);
					buffer[copyLen] = L'\0';
					return static_cast<LRESULT>(copyLen);
				}
				if (IsUiSendMessageTextLengthQuery(Msg))
				{
					LRESULT result = rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
					if (result <= 0) return result;
					std::vector<wchar_t> buffer(static_cast<size_t>(result) + 4, L'\0');
					UINT textMsg = Msg == WM_GETTEXTLENGTH ? WM_GETTEXT : (Msg == LB_GETTEXTLEN ? LB_GETTEXT : CB_GETLBTEXT);
					LRESULT queryResult = rawSendMessageW_UiText(hWnd, textMsg, static_cast<WPARAM>(buffer.size()), reinterpret_cast<LPARAM>(buffer.data()));
					int length = queryResult > 0 ? static_cast<int>(queryResult) : lstrlenW(buffer.data());
					if (length <= 0) return result;
					std::wstring replacedW;
					if (!TryBuildUiReplacementW(buffer.data(), length, L"SendMessageW.LengthQuery", replacedW)) return result;
					return static_cast<LRESULT>(replacedW.size());
				}
				return rawSendMessageW_UiText(hWnd, Msg, wParam, lParam);
			}

			LRESULT WINAPI newSendMessageW_UiText(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				__try { return newSendMessageW_UiText_SehImpl(hWnd, Msg, wParam, lParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawSendMessageW_UiText(hWnd, Msg, wParam, lParam); }
			}

			LRESULT WINAPI newSendDlgItemMessageA_UiText_SehImpl(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				HWND hCtrl = GetDlgItem(hDlg, nIDDlgItem);
				if (!hCtrl)
				{
					return rawSendDlgItemMessageA_UiText(hDlg, nIDDlgItem, Msg, wParam, lParam);
				}
				return newSendMessageA_UiText_SehImpl(hCtrl, Msg, wParam, lParam);
			}

			LRESULT WINAPI newSendDlgItemMessageA_UiText(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				__try { return newSendDlgItemMessageA_UiText_SehImpl(hDlg, nIDDlgItem, Msg, wParam, lParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawSendDlgItemMessageA_UiText(hDlg, nIDDlgItem, Msg, wParam, lParam); }
			}

			LRESULT WINAPI newSendDlgItemMessageW_UiText_SehImpl(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				HWND hCtrl = GetDlgItem(hDlg, nIDDlgItem);
				if (!hCtrl)
				{
					return rawSendDlgItemMessageW_UiText(hDlg, nIDDlgItem, Msg, wParam, lParam);
				}
				return newSendMessageW_UiText_SehImpl(hCtrl, Msg, wParam, lParam);
			}

			LRESULT WINAPI newSendDlgItemMessageW_UiText(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
			{
				__try { return newSendDlgItemMessageW_UiText_SehImpl(hDlg, nIDDlgItem, Msg, wParam, lParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawSendDlgItemMessageW_UiText(hDlg, nIDDlgItem, Msg, wParam, lParam); }
			}

			LRESULT WINAPI newDefWindowProcA_UiText_SehImpl(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			if (Msg == WM_SETTEXT && lParam != 0)
			{
				LPCSTR sourceText = reinterpret_cast<LPCSTR>(lParam);
				std::string replacedA;
				std::wstring replacedW;
				const int length = lstrlenA(sourceText);
				if (TryBuildUiReplacementA(sourceText, length, L"DefWindowProcA", replacedA, replacedW))
				{
					return rawDefWindowProcW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedW.c_str()));
				}
			}
			return rawDefWindowProcA_UiText(hWnd, Msg, wParam, lParam);
		}

		LRESULT WINAPI newDefWindowProcA_UiText(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			__try { return newDefWindowProcA_UiText_SehImpl(hWnd, Msg, wParam, lParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDefWindowProcA_UiText(hWnd, Msg, wParam, lParam); }
		}

		LRESULT WINAPI newDefWindowProcW_UiText_SehImpl(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			if (Msg == WM_SETTEXT && lParam != 0)
			{
				LPCWSTR sourceText = reinterpret_cast<LPCWSTR>(lParam);
				std::wstring replacedW;
				const int length = lstrlenW(sourceText);
				if (TryBuildUiReplacementW(sourceText, length, L"DefWindowProcW", replacedW))
				{
					return rawDefWindowProcW_UiText(hWnd, Msg, wParam, reinterpret_cast<LPARAM>(replacedW.c_str()));
				}
			}
			return rawDefWindowProcW_UiText(hWnd, Msg, wParam, lParam);
		}

		LRESULT WINAPI newDefWindowProcW_UiText(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
		{
			__try { return newDefWindowProcW_UiText_SehImpl(hWnd, Msg, wParam, lParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDefWindowProcW_UiText(hWnd, Msg, wParam, lParam); }
		}

		BOOL WINAPI newAppendMenuA_UiText_SehImpl(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			if (!IsMenuStringFlags(uFlags) || !lpNewItem)
			{
				return rawAppendMenuA_UiText(hMenu, uFlags, uIDNewItem, lpNewItem);
			}

			std::string replacedA;
			std::wstring replacedW;
			const int length = lstrlenA(lpNewItem);
			if (!TryBuildUiReplacementA(lpNewItem, length, L"AppendMenuA", replacedA, replacedW))
			{
				return rawAppendMenuA_UiText(hMenu, uFlags, uIDNewItem, lpNewItem);
			}
			return AppendMenuW(hMenu, uFlags, uIDNewItem, replacedW.c_str());
		}

		BOOL WINAPI newAppendMenuA_UiText(HMENU hMenu, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			__try { return newAppendMenuA_UiText_SehImpl(hMenu, uFlags, uIDNewItem, lpNewItem); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawAppendMenuA_UiText(hMenu, uFlags, uIDNewItem, lpNewItem); }
		}

		BOOL WINAPI newModifyMenuA_UiText_SehImpl(HMENU hMnu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			if (!IsMenuStringFlags(uFlags) || !lpNewItem)
			{
				return rawModifyMenuA_UiText(hMnu, uPosition, uFlags, uIDNewItem, lpNewItem);
			}

			std::string replacedA;
			std::wstring replacedW;
			const int length = lstrlenA(lpNewItem);
			if (!TryBuildUiReplacementA(lpNewItem, length, L"ModifyMenuA", replacedA, replacedW))
			{
				return rawModifyMenuA_UiText(hMnu, uPosition, uFlags, uIDNewItem, lpNewItem);
			}
			return ModifyMenuW(hMnu, uPosition, uFlags, uIDNewItem, replacedW.c_str());
		}

		BOOL WINAPI newModifyMenuA_UiText(HMENU hMnu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			__try { return newModifyMenuA_UiText_SehImpl(hMnu, uPosition, uFlags, uIDNewItem, lpNewItem); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawModifyMenuA_UiText(hMnu, uPosition, uFlags, uIDNewItem, lpNewItem); }
		}

		BOOL WINAPI newInsertMenuA_UiText_SehImpl(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			if (!IsMenuStringFlags(uFlags) || !lpNewItem)
			{
				return rawInsertMenuA_UiText(hMenu, uPosition, uFlags, uIDNewItem, lpNewItem);
			}

			std::string replacedA;
			std::wstring replacedW;
			const int length = lstrlenA(lpNewItem);
			if (!TryBuildUiReplacementA(lpNewItem, length, L"InsertMenuA", replacedA, replacedW))
			{
				return rawInsertMenuA_UiText(hMenu, uPosition, uFlags, uIDNewItem, lpNewItem);
			}
			return InsertMenuW(hMenu, uPosition, uFlags, uIDNewItem, replacedW.c_str());
		}

		BOOL WINAPI newInsertMenuA_UiText(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, LPCSTR lpNewItem)
		{
			__try { return newInsertMenuA_UiText_SehImpl(hMenu, uPosition, uFlags, uIDNewItem, lpNewItem); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawInsertMenuA_UiText(hMenu, uPosition, uFlags, uIDNewItem, lpNewItem); }
		}

		BOOL WINAPI newInsertMenuItemA_UiText_SehImpl(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFOA lpmi)
		{
			if (!lpmi || (lpmi->fMask & MIIM_STRING) == 0 || !lpmi->dwTypeData)
			{
				return rawInsertMenuItemA_UiText(hmenu, item, fByPosition, lpmi);
			}

			const int length = lpmi->cch > 0 ? static_cast<int>(lpmi->cch) : lstrlenA(lpmi->dwTypeData);
			std::string replacedA;
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(lpmi->dwTypeData, length, L"InsertMenuItemA", replacedA, replacedW))
			{
				return rawInsertMenuItemA_UiText(hmenu, item, fByPosition, lpmi);
			}

			MENUITEMINFOW localInfo = BuildMenuItemInfoW(*lpmi, replacedW);
			return InsertMenuItemW(hmenu, item, fByPosition, &localInfo);
		}

		BOOL WINAPI newInsertMenuItemA_UiText(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFOA lpmi)
		{
			__try { return newInsertMenuItemA_UiText_SehImpl(hmenu, item, fByPosition, lpmi); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawInsertMenuItemA_UiText(hmenu, item, fByPosition, lpmi); }
		}

		BOOL WINAPI newSetMenuItemInfoA_UiText_SehImpl(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFOA lpmii)
		{
			if (!lpmii || (lpmii->fMask & MIIM_STRING) == 0 || !lpmii->dwTypeData)
			{
				return rawSetMenuItemInfoA_UiText(hmenu, item, fByPosition, lpmii);
			}

			const int length = lpmii->cch > 0 ? static_cast<int>(lpmii->cch) : lstrlenA(lpmii->dwTypeData);
			std::string replacedA;
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(lpmii->dwTypeData, length, L"SetMenuItemInfoA", replacedA, replacedW))
			{
				return rawSetMenuItemInfoA_UiText(hmenu, item, fByPosition, lpmii);
			}

			MENUITEMINFOW localInfo = BuildMenuItemInfoW(*lpmii, replacedW);
			return SetMenuItemInfoW(hmenu, item, fByPosition, &localInfo);
		}

		BOOL WINAPI newSetMenuItemInfoA_UiText(HMENU hmenu, UINT item, BOOL fByPosition, LPCMENUITEMINFOA lpmii)
		{
			__try { return newSetMenuItemInfoA_UiText_SehImpl(hmenu, item, fByPosition, lpmii); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawSetMenuItemInfoA_UiText(hmenu, item, fByPosition, lpmii); }
		}

		INT_PTR WINAPI newDialogBoxParamA_UiText_SehImpl(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			if (!lpDialogFunc)
			{
				return rawDialogBoxParamA_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);
			}
			UiDialogProcContext* context = new UiDialogProcContext{};
			context->originalProc = lpDialogFunc;
			context->originalInitParam = dwInitParam;
			INT_PTR result = rawDialogBoxParamA_UiText(hInstance, lpTemplateName, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
			if (result == -1)
			{
				delete context;
			}
			return result;
		}

		INT_PTR WINAPI newDialogBoxParamA_UiText(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			__try { return newDialogBoxParamA_UiText_SehImpl(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDialogBoxParamA_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
		}

		HWND WINAPI newCreateDialogParamA_UiText_SehImpl(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			if (!lpDialogFunc)
			{
				return rawCreateDialogParamA_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);
			}
			UiDialogProcContext* context = new UiDialogProcContext{};
			context->originalProc = lpDialogFunc;
			context->originalInitParam = dwInitParam;
			HWND hWnd = rawCreateDialogParamA_UiText(hInstance, lpTemplateName, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
			if (!hWnd)
			{
				delete context;
			}
			return hWnd;
		}

		HWND WINAPI newCreateDialogParamA_UiText(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			__try { return newCreateDialogParamA_UiText_SehImpl(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawCreateDialogParamA_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
		}

		INT_PTR WINAPI newDialogBoxIndirectParamA_UiText_SehImpl(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			if (!lpDialogFunc)
			{
				return rawDialogBoxIndirectParamA_UiText(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);
			}
			UiDialogProcContext* context = new UiDialogProcContext{};
			context->originalProc = lpDialogFunc;
			context->originalInitParam = dwInitParam;
			INT_PTR result = rawDialogBoxIndirectParamA_UiText(hInstance, hDialogTemplate, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
			if (result == -1)
			{
				delete context;
			}
			return result;
		}

		INT_PTR WINAPI newDialogBoxIndirectParamA_UiText(HINSTANCE hInstance, LPCDLGTEMPLATEA hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			__try { return newDialogBoxIndirectParamA_UiText_SehImpl(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDialogBoxIndirectParamA_UiText(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam); }
		}

		HWND WINAPI newCreateDialogIndirectParamA_UiText_SehImpl(HINSTANCE hInstance, LPCDLGTEMPLATEA lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			if (!lpDialogFunc)
			{
				return rawCreateDialogIndirectParamA_UiText(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
			}
			UiDialogProcContext* context = new UiDialogProcContext{};
			context->originalProc = lpDialogFunc;
			context->originalInitParam = dwInitParam;
			HWND hWnd = rawCreateDialogIndirectParamA_UiText(hInstance, lpTemplate, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
			if (!hWnd)
			{
				delete context;
			}
			return hWnd;
		}

		HWND WINAPI newCreateDialogIndirectParamA_UiText(HINSTANCE hInstance, LPCDLGTEMPLATEA lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
		{
			__try { return newCreateDialogIndirectParamA_UiText_SehImpl(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawCreateDialogIndirectParamA_UiText(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam); }
		}

		INT_PTR WINAPI newDialogBoxParamW_UiText_SehImpl(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				if (!lpDialogFunc)
				{
					return rawDialogBoxParamW_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);
				}
				UiDialogProcContext* context = new UiDialogProcContext{};
				context->originalProc = lpDialogFunc;
				context->originalInitParam = dwInitParam;
				INT_PTR result = rawDialogBoxParamW_UiText(hInstance, lpTemplateName, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
				if (result == -1) delete context;
				return result;
			}

			INT_PTR WINAPI newDialogBoxParamW_UiText(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				__try { return newDialogBoxParamW_UiText_SehImpl(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawDialogBoxParamW_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
			}

			HWND WINAPI newCreateDialogParamW_UiText_SehImpl(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				if (!lpDialogFunc)
				{
					return rawCreateDialogParamW_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);
				}
				UiDialogProcContext* context = new UiDialogProcContext{};
				context->originalProc = lpDialogFunc;
				context->originalInitParam = dwInitParam;
				HWND hWnd = rawCreateDialogParamW_UiText(hInstance, lpTemplateName, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
				if (!hWnd) delete context;
				return hWnd;
			}

			HWND WINAPI newCreateDialogParamW_UiText(HINSTANCE hInstance, LPCWSTR lpTemplateName, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				__try { return newCreateDialogParamW_UiText_SehImpl(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawCreateDialogParamW_UiText(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam); }
			}

			INT_PTR WINAPI newDialogBoxIndirectParamW_UiText_SehImpl(HINSTANCE hInstance, LPCDLGTEMPLATEW hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				if (!lpDialogFunc)
				{
					return rawDialogBoxIndirectParamW_UiText(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);
				}
				UiDialogProcContext* context = new UiDialogProcContext{};
				context->originalProc = lpDialogFunc;
				context->originalInitParam = dwInitParam;
				INT_PTR result = rawDialogBoxIndirectParamW_UiText(hInstance, hDialogTemplate, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
				if (result == -1) delete context;
				return result;
			}

			INT_PTR WINAPI newDialogBoxIndirectParamW_UiText(HINSTANCE hInstance, LPCDLGTEMPLATEW hDialogTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				__try { return newDialogBoxIndirectParamW_UiText_SehImpl(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawDialogBoxIndirectParamW_UiText(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam); }
			}

			HWND WINAPI newCreateDialogIndirectParamW_UiText_SehImpl(HINSTANCE hInstance, LPCDLGTEMPLATEW lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				if (!lpDialogFunc)
				{
					return rawCreateDialogIndirectParamW_UiText(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam);
				}
				UiDialogProcContext* context = new UiDialogProcContext{};
				context->originalProc = lpDialogFunc;
				context->originalInitParam = dwInitParam;
				HWND hWnd = rawCreateDialogIndirectParamW_UiText(hInstance, lpTemplate, hWndParent, UiDialogProcThunk, reinterpret_cast<LPARAM>(context));
				if (!hWnd) delete context;
				return hWnd;
			}

			HWND WINAPI newCreateDialogIndirectParamW_UiText(HINSTANCE hInstance, LPCDLGTEMPLATEW lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM dwInitParam)
			{
				__try { return newCreateDialogIndirectParamW_UiText_SehImpl(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam); }
				__except (EXCEPTION_EXECUTE_HANDLER) { return rawCreateDialogIndirectParamW_UiText(hInstance, lpTemplate, hWndParent, lpDialogFunc, dwInitParam); }
			}

			INT WINAPI newMessageBoxIndirectA_UiText_SehImpl(const MSGBOXPARAMSA* lpmbp)
		{
			if (!lpmbp)
			{
				return rawMessageBoxIndirectA_UiText(lpmbp);
			}

			std::string replacedTextA;
			std::string replacedCaptionA;
			std::wstring replacedTextW;
			std::wstring replacedCaptionW;
			bool textChanged = false;
			bool captionChanged = false;
			if (lpmbp->lpszText)
			{
				const int textLen = lstrlenA(lpmbp->lpszText);
				textChanged = TryBuildUiReplacementA(lpmbp->lpszText, textLen, L"MessageBoxIndirectA.Text", replacedTextA, replacedTextW);
				if (!textChanged)
				{
					replacedTextW = MultiByteToWideWithCodePage(std::string(lpmbp->lpszText, textLen), sg_textReadCodePage);
				}
			}
			if (lpmbp->lpszCaption)
			{
				const int captionLen = lstrlenA(lpmbp->lpszCaption);
				captionChanged = TryBuildUiReplacementA(lpmbp->lpszCaption, captionLen, L"MessageBoxIndirectA.Caption", replacedCaptionA, replacedCaptionW);
				if (!captionChanged)
				{
					replacedCaptionW = MultiByteToWideWithCodePage(std::string(lpmbp->lpszCaption, captionLen), sg_textReadCodePage);
				}
			}
			if (!textChanged && !captionChanged)
			{
				return rawMessageBoxIndirectA_UiText(lpmbp);
			}

			MSGBOXPARAMSW localParams = {};
			localParams.cbSize = sizeof(localParams);
			localParams.hwndOwner = lpmbp->hwndOwner;
			localParams.hInstance = lpmbp->hInstance;
			localParams.lpszText = lpmbp->lpszText ? replacedTextW.c_str() : nullptr;
			localParams.lpszCaption = lpmbp->lpszCaption ? replacedCaptionW.c_str() : nullptr;
			localParams.dwStyle = lpmbp->dwStyle;
			localParams.lpszIcon = reinterpret_cast<LPCWSTR>(lpmbp->lpszIcon);
			localParams.dwContextHelpId = lpmbp->dwContextHelpId;
			localParams.lpfnMsgBoxCallback = lpmbp->lpfnMsgBoxCallback;
			localParams.dwLanguageId = lpmbp->dwLanguageId;
			return MessageBoxIndirectW(&localParams);
		}

		INT WINAPI newMessageBoxIndirectA_UiText(const MSGBOXPARAMSA* lpmbp)
		{
			__try { return newMessageBoxIndirectA_UiText_SehImpl(lpmbp); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawMessageBoxIndirectA_UiText(lpmbp); }
		}

		HRESULT WINAPI newDrawThemeText_UiText_SehImpl(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwTextFlags, DWORD dwTextFlags2, const RECT* pRect)
		{
			if (!rawDrawThemeText_UiText)
			{
				return E_NOTIMPL;
			}
			if (!pszText || iCharCount <= 0)
			{
				return rawDrawThemeText_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, dwTextFlags2, pRect);
			}

			std::wstring replacedW;
			if (!TryBuildUiReplacementW(pszText, iCharCount, L"DrawThemeText", replacedW))
			{
				return rawDrawThemeText_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, dwTextFlags2, pRect);
			}
			return rawDrawThemeText_UiText(hTheme, hdc, iPartId, iStateId, replacedW.c_str(), static_cast<int>(replacedW.size()), dwTextFlags, dwTextFlags2, pRect);
		}

		HRESULT WINAPI newDrawThemeText_UiText(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwTextFlags, DWORD dwTextFlags2, const RECT* pRect)
		{
			__try { return newDrawThemeText_UiText_SehImpl(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, dwTextFlags2, pRect); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDrawThemeText_UiText ? rawDrawThemeText_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, dwTextFlags2, pRect) : E_NOTIMPL; }
		}

		HRESULT WINAPI newDrawThemeTextEx_UiText_SehImpl(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwTextFlags, LPRECT pRect, const DTTOPTS* pOptions)
		{
			if (!rawDrawThemeTextEx_UiText)
			{
				return E_NOTIMPL;
			}
			if (!pszText || iCharCount <= 0)
			{
				return rawDrawThemeTextEx_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, pRect, pOptions);
			}

			std::wstring replacedW;
			if (!TryBuildUiReplacementW(pszText, iCharCount, L"DrawThemeTextEx", replacedW))
			{
				return rawDrawThemeTextEx_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, pRect, pOptions);
			}
			return rawDrawThemeTextEx_UiText(hTheme, hdc, iPartId, iStateId, replacedW.c_str(), static_cast<int>(replacedW.size()), dwTextFlags, pRect, pOptions);
		}

		HRESULT WINAPI newDrawThemeTextEx_UiText(HTHEME hTheme, HDC hdc, int iPartId, int iStateId, LPCWSTR pszText, int iCharCount, DWORD dwTextFlags, LPRECT pRect, const DTTOPTS* pOptions)
		{
			__try { return newDrawThemeTextEx_UiText_SehImpl(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, pRect, pOptions); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawDrawThemeTextEx_UiText ? rawDrawThemeTextEx_UiText(hTheme, hdc, iPartId, iStateId, pszText, iCharCount, dwTextFlags, pRect, pOptions) : E_NOTIMPL; }
		}

		INT_PTR WINAPI newPropertySheetA_UiText_SehImpl(LPCPROPSHEETHEADERA lppsh)
		{
			if (!lppsh || !lppsh->pszCaption)
			{
				return rawPropertySheetA_UiText(lppsh);
			}

			const int length = lstrlenA(lppsh->pszCaption);
			std::string replacedA;
			std::wstring replacedW;
			if (!TryBuildUiReplacementA(lppsh->pszCaption, length, L"PropertySheetA", replacedA, replacedW))
			{
				return rawPropertySheetA_UiText(lppsh);
			}

			PROPSHEETHEADERA localHeader = *lppsh;
			localHeader.pszCaption = replacedA.c_str();
			return rawPropertySheetA_UiText(&localHeader);
		}

		INT_PTR WINAPI newPropertySheetA_UiText(LPCPROPSHEETHEADERA lppsh)
		{
			__try { return newPropertySheetA_UiText_SehImpl(lppsh); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return rawPropertySheetA_UiText(lppsh); }
		}

		VOID WINAPI newExitProcessGuard_UiText_SehImpl(UINT uExitCode)
		{
			if (InterlockedExchange(&sg_exitProcessGuardEntered, 1) == 0)
			{
				SetHookRuntimeShuttingDown(true);
				ReleaseStartupWindowGate();
			}
			rawExitProcessGuard_UiText(uExitCode);
		}

		VOID WINAPI newExitProcessGuard_UiText(UINT uExitCode)
		{
			__try { newExitProcessGuard_UiText_SehImpl(uExitCode); }
			__except (EXCEPTION_EXECUTE_HANDLER) { rawExitProcessGuard_UiText(uExitCode); }
		}

		bool HookMessageBoxA()
		{
			return !TryDetourAttach(&rawMessageBoxA_UiText, newMessageBoxA_UiText);
		}

		bool HookSetDlgItemTextA()
		{
			return !TryDetourAttach(&rawSetDlgItemTextA_UiText, newSetDlgItemTextA_UiText);
		}

		bool HookSendDlgItemMessageA()
			{
				return !TryDetourAttach(&rawSendDlgItemMessageA_UiText, newSendDlgItemMessageA_UiText);
			}

			bool HookSendDlgItemMessageW()
			{
				return !TryDetourAttach(&rawSendDlgItemMessageW_UiText, newSendDlgItemMessageW_UiText);
			}

			bool HookSendMessageA()
		{
			return !TryDetourAttach(&rawSendMessageA_UiText, newSendMessageA_UiText);
		}

		bool HookSendMessageW()
			{
				return !TryDetourAttach(&rawSendMessageW_UiText, newSendMessageW_UiText);
			}

			bool HookDefWindowProcA()
		{
			return !TryDetourAttach(&rawDefWindowProcA_UiText, newDefWindowProcA_UiText);
		}

		bool HookDefWindowProcW()
		{
			return !TryDetourAttach(&rawDefWindowProcW_UiText, newDefWindowProcW_UiText);
		}

		bool HookAppendMenuA()
		{
			return !TryDetourAttach(&rawAppendMenuA_UiText, newAppendMenuA_UiText);
		}

		bool HookModifyMenuA()
		{
			return !TryDetourAttach(&rawModifyMenuA_UiText, newModifyMenuA_UiText);
		}

		bool HookInsertMenuA()
		{
			return !TryDetourAttach(&rawInsertMenuA_UiText, newInsertMenuA_UiText);
		}

		bool HookInsertMenuItemA()
		{
			return !TryDetourAttach(&rawInsertMenuItemA_UiText, newInsertMenuItemA_UiText);
		}

		bool HookSetMenuItemInfoA()
		{
			return !TryDetourAttach(&rawSetMenuItemInfoA_UiText, newSetMenuItemInfoA_UiText);
		}

		bool HookMessageBoxIndirectA()
		{
			return !TryDetourAttach(&rawMessageBoxIndirectA_UiText, newMessageBoxIndirectA_UiText);
		}

		bool HookDialogBoxParamA()
		{
			return !TryDetourAttach(&rawDialogBoxParamA_UiText, newDialogBoxParamA_UiText);
		}

		bool HookDialogBoxParamW()
			{
				return !TryDetourAttach(&rawDialogBoxParamW_UiText, newDialogBoxParamW_UiText);
			}

			bool HookCreateDialogParamA()
		{
			return !TryDetourAttach(&rawCreateDialogParamA_UiText, newCreateDialogParamA_UiText);
		}

		bool HookCreateDialogParamW()
			{
				return !TryDetourAttach(&rawCreateDialogParamW_UiText, newCreateDialogParamW_UiText);
			}

			bool HookDialogBoxIndirectParamA()
		{
			return !TryDetourAttach(&rawDialogBoxIndirectParamA_UiText, newDialogBoxIndirectParamA_UiText);
		}

		bool HookDialogBoxIndirectParamW()
			{
				return !TryDetourAttach(&rawDialogBoxIndirectParamW_UiText, newDialogBoxIndirectParamW_UiText);
			}

			bool HookCreateDialogIndirectParamA()
		{
			return !TryDetourAttach(&rawCreateDialogIndirectParamA_UiText, newCreateDialogIndirectParamA_UiText);
		}

		bool HookCreateDialogIndirectParamW()
			{
				return !TryDetourAttach(&rawCreateDialogIndirectParamW_UiText, newCreateDialogIndirectParamW_UiText);
			}

			bool HookDrawThemeText()
		{
			if (!ResolveUxThemeApis() || !rawDrawThemeText_UiText)
			{
				return true;
			}
			return !TryDetourAttach(&rawDrawThemeText_UiText, newDrawThemeText_UiText);
		}

		bool HookDrawThemeTextEx()
		{
			if (!ResolveUxThemeApis() || !rawDrawThemeTextEx_UiText)
			{
				return true;
			}
			return !TryDetourAttach(&rawDrawThemeTextEx_UiText, newDrawThemeTextEx_UiText);
		}

		bool HookPropertySheetA()
		{
			if (!rawPropertySheetA_UiText)
			{
				HMODULE hComctl32 = GetModuleHandleW(L"comctl32.dll");
				if (!hComctl32)
				{
					hComctl32 = LoadLibraryW(L"comctl32.dll");
				}
				if (!hComctl32)
				{
					return true;
				}
				rawPropertySheetA_UiText = reinterpret_cast<pPropertySheetA>(GetProcAddress(hComctl32, "PropertySheetA"));
				if (!rawPropertySheetA_UiText)
				{
					return true;
				}
			}
			return !TryDetourAttach(&rawPropertySheetA_UiText, newPropertySheetA_UiText);
		}

		bool HookExitProcessGuard()
		{
			return !TryDetourAttach(&rawExitProcessGuard_UiText, newExitProcessGuard_UiText);
		}
		//*********END UI Text Replace*********
