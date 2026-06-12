		//*********START Window Title Replace*********
		// 窗口标题替换规则
		struct WindowTitleRule
		{
			std::wstring original;
			std::wstring replacement;
		};
		static std::vector<WindowTitleRule> sg_vecWindowTitleRules;
		static UINT sg_windowTitleReadCodePage = CP_ACP;
		static UINT sg_windowTitleWriteCodePage = CP_ACP;
		static bool sg_enableWindowTitleVerboseLog = false;
		static bool sg_skipWindowTitleReplace = false;
		static std::map<std::wstring, bool> sg_mapWindowTitleVerboseLog;
		static volatile LONG sg_enableStartupWindowGate = 0;
		static DWORD sg_startupWindowGateBypassThreadId = 0;
		static HANDLE sg_startupWindowGateEvent = nullptr;
		static bool sg_windowLifecycleApiHooksAttached = false;
		static bool sg_windowTitleTextApiHooksAttached = false;
		static pShowWindow rawShowWindow = ShowWindow;
		static pShowWindowAsync rawShowWindowAsync = ShowWindowAsync;
		static pSetWindowPos rawSetWindowPos = SetWindowPos;
		static pSetWindowTextW rawSetWindowTextW = SetWindowTextW;
		static CRITICAL_SECTION sg_startupWindowGateLock;
		static INIT_ONCE sg_startupWindowGateLockInitOnce = INIT_ONCE_STATIC_INIT;
		static std::vector<HWND> sg_startupDeferredWindows;

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

		typedef BOOL(WINAPI* pSetWindowDisplayAffinityDynamic)(HWND hWnd, DWORD dwAffinity);
		static volatile LONG sg_enableScreenCaptureProtection = 0;
		static DWORD sg_screenCaptureProtectionAffinity = WDA_EXCLUDEFROMCAPTURE;
		static bool sg_screenCaptureProtectionFallbackToMonitor = true;
		static bool sg_screenCaptureProtectionProtectToolWindows = false;
		static bool sg_screenCaptureProtectionProtectOwnedWindows = false;
		static bool sg_screenCaptureProtectionVerboseLog = false;
		static bool sg_screenCaptureProtectionApiUnavailableLogged = false;
		static pSetWindowDisplayAffinityDynamic sg_pSetWindowDisplayAffinity = nullptr;
		static INIT_ONCE sg_setWindowDisplayAffinityInitOnce = INIT_ONCE_STATIC_INIT;
		static HANDLE sg_screenCaptureProtectionEventThread = nullptr;
		static HWINEVENTHOOK sg_screenCaptureProtectionCreateHook = nullptr;
		static HWINEVENTHOOK sg_screenCaptureProtectionShowHook = nullptr;

		// 添加窗口标题替换规则
		void AddWindowTitleRule(const wchar_t* originalTitle, const wchar_t* newTitle)
		{
			if (originalTitle && newTitle)
			{
				WindowTitleRule rule;
				rule.original = originalTitle;
				rule.replacement = newTitle;
				sg_vecWindowTitleRules.push_back(rule);
			}
		}
		void SetWindowTitleEncoding(uint32_t codePage)
		{
			SetWindowTitleEncodings(codePage, codePage);
		}

		void SetWindowTitleEncodings(uint32_t readCodePage, uint32_t writeCodePage)
		{
			sg_windowTitleReadCodePage = readCodePage == 0 ? CP_ACP : (UINT)readCodePage;
			sg_windowTitleWriteCodePage = writeCodePage == 0 ? sg_windowTitleReadCodePage : (UINT)writeCodePage;
			LogMessage(LogLevel::Info, L"SetWindowTitleEncodings: read=%u write=%u", sg_windowTitleReadCodePage, sg_windowTitleWriteCodePage);
		}

		static std::wstring EscapeWindowTitleLogText(const std::wstring& text)
		{
			std::wstring escaped;
			escaped.reserve(text.size());
			for (wchar_t ch : text)
			{
				switch (ch)
				{
				case L'\r':
					escaped += L"\\r";
					break;
				case L'\n':
					escaped += L"\\n";
					break;
				case L'\t':
					escaped += L"\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}
			return escaped;
		}

		static std::wstring BuildWindowTitleAnsiByteHexPreview(const std::string& text)
		{
			if (text.empty())
			{
				return L"(empty)";
			}

			static const wchar_t* kHex = L"0123456789ABCDEF";
			const size_t previewLimit = 24;
			size_t count = text.size() > previewLimit ? previewLimit : text.size();
			std::wstring hex;
			hex.reserve(count * 3 + 8);
			for (size_t i = 0; i < count; ++i)
			{
				if (i > 0)
				{
					hex += L' ';
				}
				unsigned char value = static_cast<unsigned char>(text[i]);
				hex += kHex[(value >> 4) & 0x0F];
				hex += kHex[value & 0x0F];
			}
			if (count < text.size())
			{
				hex += L" ...";
			}
			return hex;
		}

		static bool ShouldLogWindowTitleVerboseKey(const std::wstring& key)
		{
			if (!sg_enableWindowTitleVerboseLog)
			{
				return false;
			}
			if (sg_mapWindowTitleVerboseLog.find(key) != sg_mapWindowTitleVerboseLog.end())
			{
				return false;
			}
			sg_mapWindowTitleVerboseLog[key] = true;
			return true;
		}

		static void LogWindowTitleVerboseObserve(const std::wstring& title)
		{
			if (!ShouldLogWindowTitleVerboseKey(std::wstring(L"OBS|") + title))
			{
				return;
			}
			std::wstring escaped = EscapeWindowTitleLogText(title);
			LogMessage(LogLevel::Info, L"WindowTitle verbose: observe len=%d title=\"%s\"",
				(int)title.size(), escaped.c_str());
		}

		static void LogWindowTitleVerboseHit(const std::wstring& source, const std::wstring& replaced)
		{
			std::wstring key = std::wstring(L"HIT|") + source + L"\x1f" + replaced;
			if (!ShouldLogWindowTitleVerboseKey(key))
			{
				return;
			}
			std::wstring escapedSource = EscapeWindowTitleLogText(source);
			std::wstring escapedReplaced = EscapeWindowTitleLogText(replaced);
			LogMessage(LogLevel::Info, L"WindowTitle verbose: hit from=\"%s\" to=\"%s\"",
				escapedSource.c_str(), escapedReplaced.c_str());
		}

		static void LogWindowTitleVerboseDecodeFailureA(const char* title)
		{
			if (!title)
			{
				return;
			}
			std::wstring bytesPreview = BuildWindowTitleAnsiByteHexPreview(std::string(title));
			std::wstring key = std::wstring(L"A_DECODE_FAIL|") + bytesPreview;
			if (!ShouldLogWindowTitleVerboseKey(key))
			{
				return;
			}
			LogMessage(LogLevel::Info, L"WindowTitle verbose: observe kind=A readCp=%u decode=failed bytes=%s",
				sg_windowTitleReadCodePage, bytesPreview.c_str());
		}

		void EnableWindowTitleVerboseLog(bool enable)
		{
			sg_enableWindowTitleVerboseLog = enable;
			if (!enable)
			{
				sg_mapWindowTitleVerboseLog.clear();
			}
			LogMessage(LogLevel::Info, L"EnableWindowTitleVerboseLog: %s", enable ? L"true" : L"false");
		}

		void SetWindowTitleReplaceBypass(bool bypass)
		{
			sg_skipWindowTitleReplace = bypass;
			LogMessage(LogLevel::Debug, L"SetWindowTitleReplaceBypass: %s", bypass ? L"true" : L"false");
		}

		static BOOL CALLBACK InitStartupWindowGateLockOnce(PINIT_ONCE initOnce, PVOID parameter, PVOID* context)
		{
			UNREFERENCED_PARAMETER(initOnce);
			UNREFERENCED_PARAMETER(parameter);
			UNREFERENCED_PARAMETER(context);
			InitializeCriticalSection(&sg_startupWindowGateLock);
			return TRUE;
		}

		static void EnsureStartupWindowGateLock()
		{
			InitOnceExecuteOnce(&sg_startupWindowGateLockInitOnce, InitStartupWindowGateLockOnce, nullptr, nullptr);
		}

		static bool IsStartupWindowGateActive()
		{
			return InterlockedCompareExchange(&sg_enableStartupWindowGate, 0, 0) != 0;
		}

		static bool IsStartupWindowGateBypassThread()
		{
			return GetCurrentThreadId() == sg_startupWindowGateBypassThreadId;
		}

		static bool IsStartupDeferredWindowCandidate(HWND hWnd)
		{
			if (!hWnd || !IsWindow(hWnd))
			{
				return false;
			}
			DWORD processId = 0;
			GetWindowThreadProcessId(hWnd, &processId);
			if (processId != GetCurrentProcessId())
			{
				return false;
			}
			if (GetWindow(hWnd, GW_OWNER) != nullptr)
			{
				return false;
			}
			LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
			return (style & WS_CHILD) == 0;
		}

		static void RememberStartupDeferredWindow(HWND hWnd)
		{
			if (!IsStartupDeferredWindowCandidate(hWnd))
			{
				return;
			}
			EnsureStartupWindowGateLock();
			EnterCriticalSection(&sg_startupWindowGateLock);
			for (HWND existing : sg_startupDeferredWindows)
			{
				if (existing == hWnd)
				{
					LeaveCriticalSection(&sg_startupWindowGateLock);
					return;
				}
			}
			sg_startupDeferredWindows.push_back(hWnd);
			LeaveCriticalSection(&sg_startupWindowGateLock);
			LogMessage(LogLevel::Info, L"StartupWindowGate: defer hwnd=%p", hWnd);
		}

		struct StartupVisibleWindowCaptureContext
		{
			DWORD processId = 0;
			std::vector<HWND> windows;
		};

		static BOOL CALLBACK CollectVisibleStartupWindowsProc(HWND hWnd, LPARAM lParam)
		{
			StartupVisibleWindowCaptureContext* context = reinterpret_cast<StartupVisibleWindowCaptureContext*>(lParam);
			if (!context || !IsStartupDeferredWindowCandidate(hWnd) || !IsWindowVisible(hWnd))
			{
				return TRUE;
			}

			DWORD processId = 0;
			GetWindowThreadProcessId(hWnd, &processId);
			if (processId != context->processId)
			{
				return TRUE;
			}

			context->windows.push_back(hWnd);
			return TRUE;
		}

		static void CaptureExistingStartupWindows()
		{
			StartupVisibleWindowCaptureContext context = {};
			context.processId = GetCurrentProcessId();
			if (!EnumWindows(CollectVisibleStartupWindowsProc, reinterpret_cast<LPARAM>(&context)))
			{
				LogMessage(LogLevel::Warn, L"StartupWindowGate: EnumWindows failed, gle=%lu", GetLastError());
				return;
			}

			for (HWND hWnd : context.windows)
			{
				RememberStartupDeferredWindow(hWnd);
				rawSetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
				LogMessage(LogLevel::Info, L"StartupWindowGate: captured pre-existing hwnd=%p", hWnd);
			}
		}

		static bool ShouldDeferStartupWindow(HWND hWnd)
		{
			return IsStartupWindowGateActive()
				&& !IsStartupWindowGateBypassThread()
				&& IsStartupDeferredWindowCandidate(hWnd);
		}

		static bool IsStartupShowCommand(int nCmdShow)
		{
			switch (nCmdShow)
			{
			case SW_HIDE:
			case SW_MINIMIZE:
			case SW_SHOWMINIMIZED:
			case SW_FORCEMINIMIZE:
				return false;
			default:
				return true;
			}
		}

		static bool IsScreenCaptureProtectionEnabled()
		{
			return InterlockedCompareExchange(&sg_enableScreenCaptureProtection, 0, 0) != 0;
		}

		static BOOL CALLBACK InitSetWindowDisplayAffinityOnce(PINIT_ONCE initOnce, PVOID parameter, PVOID* context)
		{
			UNREFERENCED_PARAMETER(initOnce);
			UNREFERENCED_PARAMETER(parameter);
			UNREFERENCED_PARAMETER(context);

			HMODULE user32 = GetModuleHandleW(L"user32.dll");
			if (!user32)
			{
				user32 = LoadLibraryW(L"user32.dll");
			}
			if (user32)
			{
				sg_pSetWindowDisplayAffinity = reinterpret_cast<pSetWindowDisplayAffinityDynamic>(GetProcAddress(user32, "SetWindowDisplayAffinity"));
			}
			return TRUE;
		}

		static pSetWindowDisplayAffinityDynamic GetSetWindowDisplayAffinityProc()
		{
			InitOnceExecuteOnce(&sg_setWindowDisplayAffinityInitOnce, InitSetWindowDisplayAffinityOnce, nullptr, nullptr);
			if (!sg_pSetWindowDisplayAffinity && !sg_screenCaptureProtectionApiUnavailableLogged)
			{
				sg_screenCaptureProtectionApiUnavailableLogged = true;
				LogMessage(LogLevel::Warn, L"ScreenCaptureProtection: SetWindowDisplayAffinity unavailable");
			}
			return sg_pSetWindowDisplayAffinity;
		}

		static bool IsScreenCaptureProtectionCandidate(HWND hWnd)
		{
			if (!hWnd || !IsWindow(hWnd))
			{
				return false;
			}

			DWORD processId = 0;
			GetWindowThreadProcessId(hWnd, &processId);
			if (processId != GetCurrentProcessId())
			{
				return false;
			}

			LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
			if ((style & WS_CHILD) != 0)
			{
				return false;
			}

			if (!sg_screenCaptureProtectionProtectOwnedWindows && GetWindow(hWnd, GW_OWNER) != nullptr)
			{
				return false;
			}

			LONG_PTR exStyle = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
			if (!sg_screenCaptureProtectionProtectToolWindows && (exStyle & WS_EX_TOOLWINDOW) != 0)
			{
				return false;
			}

			return true;
		}

		static bool TrySetWindowDisplayAffinityWithFallback(HWND hWnd, DWORD affinity, DWORD& appliedAffinity, DWORD& lastError)
		{
			appliedAffinity = affinity;
			lastError = ERROR_SUCCESS;
			pSetWindowDisplayAffinityDynamic setWindowDisplayAffinity = GetSetWindowDisplayAffinityProc();
			if (!setWindowDisplayAffinity)
			{
				lastError = ERROR_PROC_NOT_FOUND;
				return false;
			}

			SetLastError(ERROR_SUCCESS);
			if (setWindowDisplayAffinity(hWnd, affinity))
			{
				return true;
			}
			lastError = GetLastError();

			if (affinity == WDA_EXCLUDEFROMCAPTURE && sg_screenCaptureProtectionFallbackToMonitor)
			{
				appliedAffinity = WDA_MONITOR;
				SetLastError(ERROR_SUCCESS);
				if (setWindowDisplayAffinity(hWnd, WDA_MONITOR))
				{
					lastError = ERROR_SUCCESS;
					return true;
				}
				lastError = GetLastError();
			}
			return false;
		}

		static bool TryApplyScreenCaptureProtection(HWND hWnd, const wchar_t* reason)
		{
			if (!IsScreenCaptureProtectionEnabled() || !IsScreenCaptureProtectionCandidate(hWnd))
			{
				return false;
			}

			DWORD appliedAffinity = sg_screenCaptureProtectionAffinity;
			DWORD lastError = ERROR_SUCCESS;
			bool ok = TrySetWindowDisplayAffinityWithFallback(hWnd, sg_screenCaptureProtectionAffinity, appliedAffinity, lastError);
			if (ok)
			{
				if (sg_screenCaptureProtectionVerboseLog)
				{
					LogMessage(LogLevel::Info, L"ScreenCaptureProtection: applied hwnd=%p affinity=0x%08X reason=%s",
						hWnd, appliedAffinity, reason ? reason : L"(unknown)");
				}
				return true;
			}

			LogMessage(sg_screenCaptureProtectionVerboseLog ? LogLevel::Warn : LogLevel::Debug,
				L"ScreenCaptureProtection: failed hwnd=%p affinity=0x%08X fallback=%d gle=%lu reason=%s",
				hWnd, sg_screenCaptureProtectionAffinity, sg_screenCaptureProtectionFallbackToMonitor ? 1 : 0,
				(unsigned long)lastError, reason ? reason : L"(unknown)");
			return false;
		}

		void SetScreenCaptureProtectionConfig(bool enable, uint32_t affinity, bool fallbackToMonitor, bool protectToolWindows, bool protectOwnedWindows, bool verboseLog)
		{
			sg_screenCaptureProtectionAffinity = affinity == WDA_MONITOR ? WDA_MONITOR : WDA_EXCLUDEFROMCAPTURE;
			sg_screenCaptureProtectionFallbackToMonitor = fallbackToMonitor;
			sg_screenCaptureProtectionProtectToolWindows = protectToolWindows;
			sg_screenCaptureProtectionProtectOwnedWindows = protectOwnedWindows;
			sg_screenCaptureProtectionVerboseLog = verboseLog;
			InterlockedExchange(&sg_enableScreenCaptureProtection, enable ? 1 : 0);
			LogMessage(LogLevel::Info, L"ScreenCaptureProtection: enable=%d affinity=0x%08X fallback=%d tool=%d owned=%d verbose=%d",
				enable ? 1 : 0, sg_screenCaptureProtectionAffinity, fallbackToMonitor ? 1 : 0,
				protectToolWindows ? 1 : 0, protectOwnedWindows ? 1 : 0, verboseLog ? 1 : 0);
		}

		struct ScreenCaptureProtectionEnumContext
		{
			uint32_t observed = 0;
			uint32_t candidates = 0;
			uint32_t applied = 0;
		};

		static BOOL CALLBACK EnumScreenCaptureProtectionProc(HWND hWnd, LPARAM lParam)
		{
			ScreenCaptureProtectionEnumContext* context = reinterpret_cast<ScreenCaptureProtectionEnumContext*>(lParam);
			if (!context)
			{
				return TRUE;
			}
			context->observed++;
			if (!IsScreenCaptureProtectionCandidate(hWnd))
			{
				return TRUE;
			}
			context->candidates++;
			if (TryApplyScreenCaptureProtection(hWnd, L"EnumWindows"))
			{
				context->applied++;
			}
			return TRUE;
		}

		void ApplyScreenCaptureProtectionToExistingWindows()
		{
			if (!IsScreenCaptureProtectionEnabled())
			{
				return;
			}
			ScreenCaptureProtectionEnumContext context = {};
			if (!EnumWindows(EnumScreenCaptureProtectionProc, reinterpret_cast<LPARAM>(&context)))
			{
				LogMessage(LogLevel::Warn, L"ScreenCaptureProtection: EnumWindows failed, gle=%lu", GetLastError());
				return;
			}
			LogMessage(LogLevel::Info, L"ScreenCaptureProtection: existing windows observed=%u candidates=%u applied=%u",
				context.observed, context.candidates, context.applied);
		}

		void EnableStartupWindowGate(bool enable, uint32_t bypassThreadId)
		{
			EnsureStartupWindowGateLock();
			if (!sg_startupWindowGateEvent)
			{
				sg_startupWindowGateEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
			}
			sg_startupWindowGateBypassThreadId = enable ? static_cast<DWORD>(bypassThreadId) : 0;
			InterlockedExchange(&sg_enableStartupWindowGate, enable ? 1 : 0);
			if (sg_startupWindowGateEvent)
			{
				if (enable)
				{
					ResetEvent(sg_startupWindowGateEvent);
				}
				else
				{
					SetEvent(sg_startupWindowGateEvent);
				}
			}
			LogMessage(LogLevel::Info, L"EnableStartupWindowGate: enable=%d bypassTid=%lu",
				enable ? 1 : 0, (unsigned long)sg_startupWindowGateBypassThreadId);
			if (enable)
			{
				CaptureExistingStartupWindows();
			}
		}

		void ReleaseStartupWindowGate()
		{
			std::vector<HWND> windowsToShow;
			EnsureStartupWindowGateLock();
			EnterCriticalSection(&sg_startupWindowGateLock);
			windowsToShow.swap(sg_startupDeferredWindows);
			LeaveCriticalSection(&sg_startupWindowGateLock);
			InterlockedExchange(&sg_enableStartupWindowGate, 0);
			sg_startupWindowGateBypassThreadId = 0;
			if (sg_startupWindowGateEvent)
			{
				SetEvent(sg_startupWindowGateEvent);
			}
			for (HWND hWnd : windowsToShow)
			{
				if (!IsWindow(hWnd))
				{
					continue;
				}
				if (IsIconic(hWnd))
				{
					rawShowWindowAsync(hWnd, SW_RESTORE);
				}
				else
				{
					rawShowWindowAsync(hWnd, SW_SHOW);
				}
				rawSetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				rawSetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			}
			LogMessage(LogLevel::Info, L"ReleaseStartupWindowGate");
		}

		static void WaitForStartupWindowGate(const wchar_t* apiName)
		{
			if (InterlockedCompareExchange(&sg_enableStartupWindowGate, 0, 0) == 0)
			{
				return;
			}
			if (GetCurrentThreadId() == sg_startupWindowGateBypassThreadId)
			{
				return;
			}
			if (!sg_startupWindowGateEvent)
			{
				return;
			}

			LogMessage(LogLevel::Info, L"StartupWindowGate: wait api=%s tid=%lu",
				apiName ? apiName : L"(unknown)", (unsigned long)GetCurrentThreadId());
			WaitForSingleObject(sg_startupWindowGateEvent, INFINITE);
			LogMessage(LogLevel::Info, L"StartupWindowGate: resume api=%s tid=%lu",
				apiName ? apiName : L"(unknown)", (unsigned long)GetCurrentThreadId());
		}

		// 匹配并替换窗口标题
		static std::wstring ProcessWindowTitleW(const wchar_t* title)
		{
			if (!title) return L"";
			
			std::wstring wsTitle = title;
			if (sg_skipWindowTitleReplace)
			{
				return wsTitle;
			}
			LogWindowTitleVerboseObserve(wsTitle);
			
			for (const auto& rule : sg_vecWindowTitleRules)
			{
				if (WildcardMatchW(rule.original.c_str(), wsTitle.c_str()))
				{
					LogWindowTitleVerboseHit(wsTitle, rule.replacement);
					return rule.replacement;
				}
			}
			
			return wsTitle;
		}

		static bool TryBuildWindowTitleReplacementFromA(const char* title, std::wstring& replaced)
		{
			replaced.clear();
			if (!title) return false;
			if (sg_skipWindowTitleReplace) return false;
			
			int len = MultiByteToWideChar(sg_windowTitleReadCodePage, 0, title, -1, NULL, 0);
			if (len <= 0)
			{
				LogWindowTitleVerboseDecodeFailureA(title);
				return false;
			}
			
			std::wstring wTitle(len - 1, L'\0');
			MultiByteToWideChar(sg_windowTitleReadCodePage, 0, title, -1, &wTitle[0], len);
			
			replaced = ProcessWindowTitleW(wTitle.c_str());
			return !replaced.empty() && replaced != wTitle;
		}

		static BOOL ApplyWindowTitleUnicode(HWND hWnd, const std::wstring& title)
		{
			if (!hWnd || title.empty())
			{
				return FALSE;
			}

			BOOL setRet = FALSE;
			if (IsWindowUnicode(hWnd))
			{
				setRet = rawSetWindowTextW(hWnd, title.c_str());
			}
			else
			{
				setRet = static_cast<BOOL>(DefWindowProcW(hWnd, WM_SETTEXT, 0, (LPARAM)title.c_str()));
			}
			rawSetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
			return setRet;
		}

		static int QueryWindowTitleUnicode(HWND hWnd, wchar_t* title, int titleCapacity)
		{
			if (!hWnd || !title || titleCapacity <= 0)
			{
				return 0;
			}

			title[0] = L'\0';
			if (IsWindowUnicode(hWnd))
			{
				return GetWindowTextW(hWnd, title, titleCapacity);
			}

			return static_cast<int>(DefWindowProcW(hWnd, WM_GETTEXT, (WPARAM)titleCapacity, (LPARAM)title));
		}
		static bool TryApplyWindowTitle(HWND hWnd);

		//*********Hook CreateWindowExA*********
		static pCreateWindowExA rawCreateWindowExA = CreateWindowExA;

		HWND WINAPI newCreateWindowExA_SehImpl(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
			DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent,
			HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
			HWND hWnd = nullptr;
			bool deferWindow = IsStartupWindowGateActive() && !IsStartupWindowGateBypassThread()
				&& hWndParent == nullptr && (dwStyle & WS_CHILD) == 0 && (dwStyle & WS_VISIBLE) != 0;
			DWORD createStyle = deferWindow ? (dwStyle & ~WS_VISIBLE) : dwStyle;
			std::wstring newTitleW;
			bool hasUnicodeReplacement = false;
			if (lpWindowName && !sg_vecWindowTitleRules.empty())
			{
				hasUnicodeReplacement = TryBuildWindowTitleReplacementFromA(lpWindowName, newTitleW);
			}
			hWnd = rawCreateWindowExA(dwExStyle, lpClassName, lpWindowName, createStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
			if (deferWindow)
			{
				RememberStartupDeferredWindow(hWnd);
			}
			TryApplyScreenCaptureProtection(hWnd, L"CreateWindowExA");
			if (hasUnicodeReplacement)
			{
				ApplyWindowTitleUnicode(hWnd, newTitleW);
				return hWnd;
			}
			TryApplyWindowTitle(hWnd);
			return hWnd;
		}
		HWND WINAPI newCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName,
			DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent,
			HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			__try { return newCreateWindowExA_SehImpl(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam); }
		}


		bool HookCreateWindowExA()
		{
			bool failed = !TryDetourAttach(&rawCreateWindowExA, newCreateWindowExA);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookCreateWindowExA: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook CreateWindowExA*********

		//*********Hook CreateWindowExW*********
		static pCreateWindowExW rawCreateWindowExW = CreateWindowExW;

		HWND WINAPI newCreateWindowExW_SehImpl(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
			DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent,
			HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
			HWND hWnd = nullptr;
			bool deferWindow = IsStartupWindowGateActive() && !IsStartupWindowGateBypassThread()
				&& hWndParent == nullptr && (dwStyle & WS_CHILD) == 0 && (dwStyle & WS_VISIBLE) != 0;
			DWORD createStyle = deferWindow ? (dwStyle & ~WS_VISIBLE) : dwStyle;
			if (lpWindowName && !sg_vecWindowTitleRules.empty())
			{
				std::wstring newTitle = ProcessWindowTitleW(lpWindowName);
				if (newTitle != lpWindowName)
				{
					hWnd = rawCreateWindowExW(dwExStyle, lpClassName, newTitle.c_str(), createStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
					if (deferWindow)
					{
						RememberStartupDeferredWindow(hWnd);
					}
					TryApplyScreenCaptureProtection(hWnd, L"CreateWindowExW");
					TryApplyWindowTitle(hWnd);
					return hWnd;
				}
			}
			hWnd = rawCreateWindowExW(dwExStyle, lpClassName, lpWindowName, createStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
			if (deferWindow)
			{
				RememberStartupDeferredWindow(hWnd);
			}
			TryApplyScreenCaptureProtection(hWnd, L"CreateWindowExW");
			TryApplyWindowTitle(hWnd);
			return hWnd;
		}
		HWND WINAPI newCreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName,
			DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent,
			HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			__try { return newCreateWindowExW_SehImpl(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam); }
		}


		bool HookCreateWindowExW()
		{
			bool failed = !TryDetourAttach(&rawCreateWindowExW, newCreateWindowExW);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookCreateWindowExW: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook CreateWindowExW*********

		//*********Hook ShowWindow*********
		BOOL WINAPI newShowWindow_SehImpl(HWND hWnd, int nCmdShow)
{
			if (ShouldDeferStartupWindow(hWnd) && IsStartupShowCommand(nCmdShow))
			{
				RememberStartupDeferredWindow(hWnd);
				return TRUE;
			}
			if (IsStartupShowCommand(nCmdShow))
			{
				TryApplyScreenCaptureProtection(hWnd, L"ShowWindow");
			}
			return rawShowWindow(hWnd, nCmdShow);
		}
		BOOL WINAPI newShowWindow(HWND hWnd, int nCmdShow)
		{
			__try { return newShowWindow_SehImpl(hWnd, nCmdShow); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawShowWindow(hWnd, nCmdShow); }
		}


		bool HookShowWindow()
		{
			bool failed = !TryDetourAttach(&rawShowWindow, newShowWindow);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookShowWindow: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook ShowWindow*********

		//*********Hook ShowWindowAsync*********
		BOOL WINAPI newShowWindowAsync_SehImpl(HWND hWnd, int nCmdShow)
{
			if (ShouldDeferStartupWindow(hWnd) && IsStartupShowCommand(nCmdShow))
			{
				RememberStartupDeferredWindow(hWnd);
				return TRUE;
			}
			if (IsStartupShowCommand(nCmdShow))
			{
				TryApplyScreenCaptureProtection(hWnd, L"ShowWindowAsync");
			}
			return rawShowWindowAsync(hWnd, nCmdShow);
		}
		BOOL WINAPI newShowWindowAsync(HWND hWnd, int nCmdShow)
		{
			__try { return newShowWindowAsync_SehImpl(hWnd, nCmdShow); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawShowWindowAsync(hWnd, nCmdShow); }
		}


		bool HookShowWindowAsync()
		{
			bool failed = !TryDetourAttach(&rawShowWindowAsync, newShowWindowAsync);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookShowWindowAsync: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook ShowWindowAsync*********

		//*********Hook SetWindowPos*********
		BOOL WINAPI newSetWindowPos_SehImpl(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
{
			if (ShouldDeferStartupWindow(hWnd) && (uFlags & SWP_SHOWWINDOW) != 0)
			{
				RememberStartupDeferredWindow(hWnd);
				uFlags &= ~SWP_SHOWWINDOW;
				uFlags |= SWP_HIDEWINDOW;
			}
			else if ((uFlags & SWP_SHOWWINDOW) != 0)
			{
				TryApplyScreenCaptureProtection(hWnd, L"SetWindowPos");
			}
			return rawSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
		}
		BOOL WINAPI newSetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags)
		{
			__try { return newSetWindowPos_SehImpl(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags); }
		}


		bool HookSetWindowPos()
		{
			bool failed = !TryDetourAttach(&rawSetWindowPos, newSetWindowPos);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookSetWindowPos: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook SetWindowPos*********

		//*********Hook SetWindowTextA*********
		static pSetWindowTextA rawSetWindowTextA = SetWindowTextA;

		BOOL WINAPI newSetWindowTextA_SehImpl(HWND hWnd, LPCSTR lpString)
{
			if (lpString && !sg_vecWindowTitleRules.empty())
			{
				std::wstring newTitleW;
				if (TryBuildWindowTitleReplacementFromA(lpString, newTitleW))
				{
					return ApplyWindowTitleUnicode(hWnd, newTitleW);
				}
			}
			return rawSetWindowTextA(hWnd, lpString);
		}
		BOOL WINAPI newSetWindowTextA(HWND hWnd, LPCSTR lpString)
		{
			__try { return newSetWindowTextA_SehImpl(hWnd, lpString); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawSetWindowTextA(hWnd, lpString); }
		}


		bool HookSetWindowTextA()
		{
			bool failed = !TryDetourAttach(&rawSetWindowTextA, newSetWindowTextA);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookSetWindowTextA: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook SetWindowTextA*********

		//*********Hook SetWindowTextW*********
		static int sg_windowTitleMode = 2;
		static HANDLE sg_windowTitleEventThread = nullptr;
		static HWINEVENTHOOK sg_windowTitleCreateHook = nullptr;
		static HWINEVENTHOOK sg_windowTitleShowHook = nullptr;
		static HWINEVENTHOOK sg_windowTitleNameHook = nullptr;
		static const wchar_t* GetWindowTitleModeName(int mode)
		{
			switch (mode)
			{
			case 0:
				return L"api";
			case 1:
				return L"event";
			default:
				return L"hybrid";
			}
		}

		static bool TryApplyWindowTitle(HWND hWnd)
		{
			if (!hWnd || sg_vecWindowTitleRules.empty())
			{
				return false;
			}
			if (!IsWindow(hWnd))
			{
				return false;
			}
			LONG_PTR style = GetWindowLongPtrW(hWnd, GWL_STYLE);
			if ((style & WS_CHILD) != 0 || (style & WS_CAPTION) != WS_CAPTION)
			{
				return false;
			}

			DWORD processId = 0;
			GetWindowThreadProcessId(hWnd, &processId);
			if (processId != GetCurrentProcessId())
			{
				return false;
			}

			wchar_t title[1024] = {};
			int titleLen = QueryWindowTitleUnicode(hWnd, title, (int)(sizeof(title) / sizeof(title[0])));
			if (titleLen <= 0)
			{
				return false;
			}

			std::wstring replaced = ProcessWindowTitleW(title);
			if (replaced.empty() || replaced == title)
			{
				return false;
			}

			return ApplyWindowTitleUnicode(hWnd, replaced) != FALSE;
		}

		static BOOL CALLBACK EnumWindowTitleProc(HWND hWnd, LPARAM lParam)
		{
			UNREFERENCED_PARAMETER(lParam);
			TryApplyWindowTitle(hWnd);
			return TRUE;
		}

		static void CALLBACK WindowTitleWinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hWnd,
			LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
		{
			UNREFERENCED_PARAMETER(hWinEventHook);
			UNREFERENCED_PARAMETER(event);
			UNREFERENCED_PARAMETER(dwEventThread);
			UNREFERENCED_PARAMETER(dwmsEventTime);

			if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
			{
				return;
			}
			TryApplyScreenCaptureProtection(hWnd, L"WinEvent");
			TryApplyWindowTitle(hWnd);
		}

		static DWORD WINAPI WindowTitleEventThreadProc(LPVOID lpParameter)
		{
			UNREFERENCED_PARAMETER(lpParameter);

			sg_windowTitleCreateHook = nullptr;
			sg_windowTitleShowHook = nullptr;
			sg_windowTitleCreateHook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, WindowTitleWinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
			sg_windowTitleShowHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, WindowTitleWinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
			sg_windowTitleNameHook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, WindowTitleWinEventProc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
			LogMessage(LogLevel::Info, L"TitleEvent(Hook): create=%p show=%p name=%p", sg_windowTitleCreateHook, sg_windowTitleShowHook, sg_windowTitleNameHook);

			if (!sg_windowTitleCreateHook && !sg_windowTitleShowHook && !sg_windowTitleNameHook)
			{
				LogMessage(LogLevel::Error, L"TitleEvent(Hook): all hooks failed");
				return 1;
			}

			EnumWindows(EnumWindowTitleProc, 0);

			MSG msg = {};
			while (GetMessageW(&msg, nullptr, 0, 0) > 0)
			{
				DispatchMessageW(&msg);
			}

			if (sg_windowTitleCreateHook)
			{
				UnhookWinEvent(sg_windowTitleCreateHook);
				sg_windowTitleCreateHook = nullptr;
			}
			if (sg_windowTitleShowHook)
			{
				UnhookWinEvent(sg_windowTitleShowHook);
				sg_windowTitleShowHook = nullptr;
			}
			if (sg_windowTitleNameHook)
			{
				UnhookWinEvent(sg_windowTitleNameHook);
				sg_windowTitleNameHook = nullptr;
			}
			LogMessage(LogLevel::Info, L"TitleEvent(Thread): exited");
			return 0;
		}

		static bool StartWindowTitleEventMode()
		{
			if (sg_windowTitleEventThread)
			{
				LogMessage(LogLevel::Info, L"TitleEvent(Start): already started");
				return true;
			}

			sg_windowTitleEventThread = CreateThread(nullptr, 0, WindowTitleEventThreadProc, nullptr, 0, nullptr);
			bool ok = sg_windowTitleEventThread != nullptr;
			LogMessage(ok ? LogLevel::Info : LogLevel::Error, L"TitleEvent(Start): %s, thread=%p", ok ? L"ok" : L"failed", sg_windowTitleEventThread);
			return ok;
		}

		BOOL WINAPI newSetWindowTextW_SehImpl(HWND hWnd, LPCWSTR lpString)
{
			if (lpString && !sg_vecWindowTitleRules.empty())
			{
				std::wstring newTitle = ProcessWindowTitleW(lpString);
				if (newTitle != lpString)
				{
					return ApplyWindowTitleUnicode(hWnd, newTitle);
				}
			}
			return rawSetWindowTextW(hWnd, lpString);
		}
		BOOL WINAPI newSetWindowTextW(HWND hWnd, LPCWSTR lpString)
		{
			__try { return newSetWindowTextW_SehImpl(hWnd, lpString); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawSetWindowTextW(hWnd, lpString); }
		}


		bool HookSetWindowTextW()
		{
			bool failed = !TryDetourAttach(&rawSetWindowTextW, newSetWindowTextW);
			LogMessage(failed ? LogLevel::Error : LogLevel::Info, L"HookSetWindowTextW: %s", failed ? L"failed" : L"ok");
			return failed;
		}
		//*********END Hook SetWindowTextW*********

		static bool EnsureWindowLifecycleApiHooksAttached(const wchar_t* owner)
		{
			if (sg_windowLifecycleApiHooksAttached)
			{
				LogMessage(LogLevel::Info, L"%s: lifecycle api hooks already attached", owner ? owner : L"WindowLifecycle");
				return true;
			}
			bool hasFailed = false;
			hasFailed |= HookCreateWindowExA();
			hasFailed |= HookCreateWindowExW();
			hasFailed |= HookShowWindow();
			hasFailed |= HookShowWindowAsync();
			hasFailed |= HookSetWindowPos();
			if (!hasFailed)
			{
				sg_windowLifecycleApiHooksAttached = true;
			}
			return !hasFailed;
		}

		static bool EnsureWindowTitleTextApiHooksAttached()
		{
			if (sg_windowTitleTextApiHooksAttached)
			{
				LogMessage(LogLevel::Info, L"HookWindowTitleAPIs: text api hooks already attached");
				return true;
			}
			bool hasFailed = false;
			hasFailed |= HookSetWindowTextA();
			hasFailed |= HookSetWindowTextW();
			if (!hasFailed)
			{
				sg_windowTitleTextApiHooksAttached = true;
			}
			return !hasFailed;
		}

		bool HookScreenCaptureProtectionAPIs(int mode)
		{
			if (!IsScreenCaptureProtectionEnabled())
			{
				LogMessage(LogLevel::Info, L"HookScreenCaptureProtectionAPIs: disabled");
				return true;
			}
			if (mode < 0 || mode > 2)
			{
				mode = 2;
			}

			bool useApiHook = (mode == 0 || mode == 2);
			bool useEventHook = (mode == 1 || mode == 2);
			bool hasFailed = false;
			if (useApiHook)
			{
				hasFailed |= !EnsureWindowLifecycleApiHooksAttached(L"HookScreenCaptureProtectionAPIs");
			}
			if (useEventHook)
			{
				hasFailed |= !StartWindowTitleEventMode();
			}
			bool ok = !hasFailed;
			LogMessage(ok ? LogLevel::Info : LogLevel::Error, L"HookScreenCaptureProtectionAPIs: %s mode=%d api=%d event=%d",
				ok ? L"ok" : L"failed", mode, useApiHook ? 1 : 0, useEventHook ? 1 : 0);
			return ok;
		}

		// 启用窗口标题Hook
		bool HookWindowTitleAPIs(int mode)
		{
			if (sg_vecWindowTitleRules.empty() && InterlockedCompareExchange(&sg_enableStartupWindowGate, 0, 0) == 0)
			{
				LogMessage(LogLevel::Warn, L"HookWindowTitleAPIs: no rules and startup gate disabled");
				return false;
			}

			if (mode < 0 || mode > 2)
			{
				mode = 2;
			}
			sg_windowTitleMode = mode;

			bool useApiHook = (sg_windowTitleMode == 0 || sg_windowTitleMode == 2);
			bool useEventHook = (sg_windowTitleMode == 1 || sg_windowTitleMode == 2);
			LogMessage(LogLevel::Info, L"HookWindowTitleAPIs: mode=%d(%s), rules=%u, api=%d, event=%d",
				sg_windowTitleMode, GetWindowTitleModeName(sg_windowTitleMode), (uint32_t)sg_vecWindowTitleRules.size(), useApiHook ? 1 : 0, useEventHook ? 1 : 0);

			bool hasFailed = false;
			if (useApiHook)
			{
				hasFailed |= !EnsureWindowLifecycleApiHooksAttached(L"HookWindowTitleAPIs");
				hasFailed |= !EnsureWindowTitleTextApiHooksAttached();
			}
			if (useEventHook)
			{
				hasFailed |= !StartWindowTitleEventMode();
			}
			if (!sg_vecWindowTitleRules.empty())
			{
				EnumWindows(EnumWindowTitleProc, 0);
			}
			bool ok = !hasFailed;
			LogMessage(ok ? LogLevel::Info : LogLevel::Error, L"HookWindowTitleAPIs: %s", ok ? L"ok" : L"failed");
			return ok;
		}
		//*********END Window Title Replace*********




