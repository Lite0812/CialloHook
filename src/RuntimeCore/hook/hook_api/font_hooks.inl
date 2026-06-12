		//*********Font File Loading*********

		bool LoadFontFromFile(const wchar_t* wpFontFilePath, bool showError)
		{
			if (!wpFontFilePath || wcslen(wpFontFilePath) == 0)
			{
				return false;
			}

			// Check font file exists
			DWORD dwAttrib = GetFileAttributesW(wpFontFilePath);
			if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
			{
				if (showError)
				{
					std::wstring errMsg = std::wstring(L"Font file not found: ") + wpFontFilePath;
					MessageBoxW(NULL, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
				}
				return false;
			}

			// Load font file as private font resource
			int nFontsLoaded = AddFontResourceExW(wpFontFilePath, FR_PRIVATE, NULL);
			if (nFontsLoaded == 0)
			{
				if (showError)
				{
					std::wstring errMsg = std::wstring(L"Failed to load font file: ") + wpFontFilePath;
					MessageBoxW(NULL, errMsg.c_str(), L"HookFont", MB_OK | MB_ICONERROR);
				}
				return false;
			}

			return true;
		}
		//*********END Font File Loading*********

		//*********Start Hook CreateFontA*******
		static DWORD sg_dwCharSet = 0;
		static bool sg_enableCharsetSpoof = false;
		static DWORD sg_dwSpoofFromCharSet = SHIFTJIS_CHARSET;
		static DWORD sg_dwSpoofToCharSet = DEFAULT_CHARSET;
		static LPCSTR sg_lpFontName = nullptr;
		static int sg_iFontHeight = 0;
		static int sg_iFontWidth = 0;
		static int sg_iFontWeight = 0;
		static float sg_fFontScale = 1.0f;  // Font scaling multiplier
		static float sg_fFontSpacingScale = 1.0f;
		static float sg_fGlyphAspectRatio = 1.0f;
		static int sg_iGlyphOffsetX = 0;
		static int sg_iGlyphOffsetY = 0;
		static int sg_iMetricsOffsetLeft = 0;
		static int sg_iMetricsOffsetRight = 0;
		static int sg_iMetricsOffsetTop = 0;
		static int sg_iMetricsOffsetBottom = 0;
		static thread_local int sg_fontCreateNesting = 0;
		static thread_local int sg_hdcFontReplacementNesting = 0;
		static thread_local int sg_dwriteHookNesting = 0;
		struct FontCreateNestingScope
		{
			FontCreateNestingScope()
			{
				++sg_fontCreateNesting;
			}
			~FontCreateNestingScope()
			{
				--sg_fontCreateNesting;
			}
		};
		struct HdcFontReplacementScope
		{
			bool active = true;
			HdcFontReplacementScope()
			{
				++sg_hdcFontReplacementNesting;
			}
			void release()
			{
				active = false;
			}
			~HdcFontReplacementScope()
			{
				if (active && sg_hdcFontReplacementNesting > 0)
				{
					--sg_hdcFontReplacementNesting;
				}
			}
		};
		struct DWriteHookNestingScope
		{
			DWriteHookNestingScope()
			{
				++sg_dwriteHookNesting;
			}
			~DWriteHookNestingScope()
			{
				if (sg_dwriteHookNesting > 0)
				{
					--sg_dwriteHookNesting;
				}
			}
		};
		static SRWLOCK sg_scaledFontHandlesLock = SRWLOCK_INIT;
		static HFONT sg_scaledFontHandles[4096] = {};
		static thread_local bool sg_runtimeFloatScaleActive = false;
		struct RuntimeFloatScaleScope
		{
			bool owner = false;
			explicit RuntimeFloatScaleScope(bool activate)
			{
				if (activate && !sg_runtimeFloatScaleActive)
				{
					sg_runtimeFloatScaleActive = true;
					owner = true;
				}
			}
			~RuntimeFloatScaleScope()
			{
				if (owner)
				{
					sg_runtimeFloatScaleActive = false;
				}
			}
		};
		static LPCWSTR sg_lpFontNameW = nullptr;
		struct FontNameRedirectRule
		{
			std::wstring sourceKey;
			std::wstring targetFontName;
		};
		static std::vector<std::wstring> sg_skipFontRuleKeys;
		static std::vector<FontNameRedirectRule> sg_fontRedirectRules;
		static bool sg_enableFontHookVerboseLog = false;
		static void LogFontHookHit(const wchar_t* apiName)
		{
			if (sg_enableFontHookVerboseLog)
			{
				LogMessage(LogLevel::Info, L"[FontHook] hit api=%s", apiName);
			}
		}
		static bool IsReadableMemoryProtect(DWORD protect)
		{
			if ((protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
			{
				return false;
			}
			protect &= 0xff;
			return protect == PAGE_READONLY
				|| protect == PAGE_READWRITE
				|| protect == PAGE_WRITECOPY
				|| protect == PAGE_EXECUTE_READ
				|| protect == PAGE_EXECUTE_READWRITE
				|| protect == PAGE_EXECUTE_WRITECOPY;
		}
		static bool IsReadableMemoryRange(const void* ptr, size_t bytes)
		{
			if (!ptr || bytes == 0)
			{
				return false;
			}
			const BYTE* current = static_cast<const BYTE*>(ptr);
			const BYTE* end = current + bytes;
			while (current < end)
			{
				MEMORY_BASIC_INFORMATION mbi = {};
				if (VirtualQuery(current, &mbi, sizeof(mbi)) != sizeof(mbi)
					|| mbi.State != MEM_COMMIT
					|| !IsReadableMemoryProtect(mbi.Protect))
				{
					return false;
				}
				const BYTE* regionEnd = static_cast<const BYTE*>(mbi.BaseAddress) + mbi.RegionSize;
				if (regionEnd <= current)
				{
					return false;
				}
				current = regionEnd;
			}
			return true;
		}
		static bool TryCopyWideFaceName(const wchar_t* source, wchar_t (&buffer)[LF_FACESIZE])
		{
			buffer[0] = L'\0';
			if (!IsReadableMemoryRange(source, sizeof(wchar_t) * LF_FACESIZE))
			{
				return false;
			}
			__try
			{
				for (size_t i = 0; i < LF_FACESIZE - 1; ++i)
				{
					wchar_t ch = source[i];
					buffer[i] = ch;
					if (ch == L'\0')
					{
						return i != 0;
					}
				}
				buffer[LF_FACESIZE - 1] = L'\0';
				return buffer[0] != L'\0';
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				buffer[0] = L'\0';
				return false;
			}
		}
		static bool TryCopyAnsiFaceName(const char* source, char (&buffer)[LF_FACESIZE])
		{
			buffer[0] = '\0';
			if (!IsReadableMemoryRange(source, sizeof(char) * LF_FACESIZE))
			{
				return false;
			}
			__try
			{
				for (size_t i = 0; i < LF_FACESIZE - 1; ++i)
				{
					char ch = source[i];
					buffer[i] = ch;
					if (ch == '\0')
					{
						return i != 0;
					}
				}
				buffer[LF_FACESIZE - 1] = '\0';
				return buffer[0] != '\0';
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				buffer[0] = '\0';
				return false;
			}
		}
		static bool TryCopyLogFontA(const LOGFONTA* source, LOGFONTA& local)
		{
			if (!IsReadableMemoryRange(source, sizeof(LOGFONTA)))
			{
				return false;
			}
			__try
			{
				local = *source;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
		static bool TryCopyLogFontW(const LOGFONTW* source, LOGFONTW& local)
		{
			if (!IsReadableMemoryRange(source, sizeof(LOGFONTW)))
			{
				return false;
			}
			__try
			{
				local = *source;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
		static bool IsSimpleWideSpace(wchar_t ch)
		{
			return ch == L' ' || ch == L'\r' || ch == L'\n' || ch == L'\t';
		}

		static wchar_t ToSimpleFontRuleUpper(wchar_t ch)
		{
			if (ch >= L'a' && ch <= L'z')
			{
				return ch - (L'a' - L'A');
			}
			return ch;
		}

		static std::wstring TrimWideCopy(std::wstring value)
		{
			size_t begin = 0;
			while (begin < value.size() && IsSimpleWideSpace(value[begin]))
			{
				++begin;
			}
			size_t end = value.size();
			while (end > begin && IsSimpleWideSpace(value[end - 1]))
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}
		static std::wstring NormalizeFontRuleKey(std::wstring value)
		{
			value = TrimWideCopy(std::move(value));
			for (wchar_t& ch : value)
			{
				ch = ToSimpleFontRuleUpper(ch);
			}
			return value;
		}
		static std::wstring NormalizeFontRuleKey(const wchar_t* value)
		{
			wchar_t faceName[LF_FACESIZE] = {};
			if (!TryCopyWideFaceName(value, faceName))
			{
				return std::wstring();
			}
			return NormalizeFontRuleKey(std::wstring(faceName));
		}
		static bool IsSkipFontRuleMatch(const std::wstring& requestedKey)
		{
			if (requestedKey.empty())
			{
				return false;
			}
			for (const std::wstring& key : sg_skipFontRuleKeys)
			{
				if (key == requestedKey)
				{
					return true;
				}
			}
			return false;
		}
		static bool TryResolveRedirectFontNameW(const wchar_t* requestedFaceName, wchar_t (&buffer)[LF_FACESIZE], bool& skipOverride)
		{
			buffer[0] = L'\0';
			skipOverride = false;
			if (sg_skipFontRuleKeys.empty() && sg_fontRedirectRules.empty())
			{
				return false;
			}
			std::wstring requestedKey = NormalizeFontRuleKey(requestedFaceName);
			if (requestedKey.empty())
			{
				return false;
			}
			if (IsSkipFontRuleMatch(requestedKey))
			{
				skipOverride = true;
				return false;
			}
			for (const FontNameRedirectRule& rule : sg_fontRedirectRules)
			{
				if (rule.sourceKey == requestedKey)
				{
					return TryCopyWideFaceName(rule.targetFontName.c_str(), buffer);
				}
			}
			return false;
		}
		static bool TryGetForcedFontNameW(wchar_t (&buffer)[LF_FACESIZE])
		{
			return TryCopyWideFaceName(sg_lpFontNameW, buffer);
		}
		static bool HasFontOverrideRules()
		{
			return !sg_skipFontRuleKeys.empty() || !sg_fontRedirectRules.empty();
		}
		static bool TryGetForcedFontNameWForRequest(const wchar_t* requestedFaceName, wchar_t (&buffer)[LF_FACESIZE], bool& skipOverride)
		{
			skipOverride = false;
			if (HasFontOverrideRules())
			{
				if (TryResolveRedirectFontNameW(requestedFaceName, buffer, skipOverride))
				{
					return true;
				}
				if (skipOverride)
				{
					return false;
				}
			}
			return TryGetForcedFontNameW(buffer);
		}
		static std::string WideFaceNameToAnsi(const wchar_t* wideFaceName)
		{
			if (!wideFaceName || wideFaceName[0] == L'\0')
			{
				return "";
			}
			int len = WideCharToMultiByte(CP_ACP, 0, wideFaceName, -1, nullptr, 0, nullptr, nullptr);
			if (len <= 0)
			{
				return "";
			}
			std::string result(len, '\0');
			WideCharToMultiByte(CP_ACP, 0, wideFaceName, -1, &result[0], len, nullptr, nullptr);
			if (!result.empty() && result.back() == '\0')
			{
				result.pop_back();
			}
			return result;
		}
		static bool ApplyOverrideToLogFontW(LOGFONTW& lf, bool applySizeScale = true);
		static bool ApplyOverrideToLogFontA(LOGFONTA& lf, bool applySizeScale = true);
		static std::string GetForcedFontNameA();
		static bool ShouldApplyFloatSizeScale()
		{
			return sg_iFontHeight == 0 && sg_fFontScale != 1.0f;
		}
		static bool IsAnyFontSizeOverrideEnabled()
		{
			return sg_iFontHeight != 0 || sg_iFontWidth != 0 || sg_fFontScale != 1.0f || sg_fGlyphAspectRatio != 1.0f;
		}
		static bool IsFontHandleMarkedScaled(HFONT hFont)
		{
			if (!hFont)
			{
				return false;
			}
			AcquireSRWLockShared(&sg_scaledFontHandlesLock);
			for (size_t i = 0; i < ARRAYSIZE(sg_scaledFontHandles); ++i)
			{
				if (sg_scaledFontHandles[i] == hFont)
				{
					ReleaseSRWLockShared(&sg_scaledFontHandlesLock);
					return true;
				}
			}
			ReleaseSRWLockShared(&sg_scaledFontHandlesLock);
			return false;
		}
		static void MarkFontHandleScaled(HFONT hFont)
		{
			if (!hFont)
			{
				return;
			}
			AcquireSRWLockExclusive(&sg_scaledFontHandlesLock);
			for (size_t i = 0; i < ARRAYSIZE(sg_scaledFontHandles); ++i)
			{
				if (sg_scaledFontHandles[i] == hFont)
				{
					ReleaseSRWLockExclusive(&sg_scaledFontHandlesLock);
					return;
				}
			}
			static size_t nextIndex = 0;
			sg_scaledFontHandles[nextIndex] = hFont;
			nextIndex = (nextIndex + 1) % ARRAYSIZE(sg_scaledFontHandles);
			ReleaseSRWLockExclusive(&sg_scaledFontHandlesLock);
		}
		static LONG ApplyGlyphAspectRatioToWidth(LONG width, LONG referenceHeight)
		{
			if (sg_fGlyphAspectRatio <= 0.0f || sg_fGlyphAspectRatio == 1.0f)
			{
				return width;
			}
			if (width != 0)
			{
				return (LONG)(width * sg_fGlyphAspectRatio);
			}
			LONG baseHeight = referenceHeight >= 0 ? referenceHeight : -referenceHeight;
			if (baseHeight <= 0)
			{
				return 0;
			}
			LONG computedWidth = (LONG)(baseHeight * sg_fGlyphAspectRatio);
			return computedWidth > 0 ? computedWidth : 1;
		}
		static LONG ResolveScaledFontWidth(LONG width, LONG referenceHeight)
		{
			LONG resolvedWidth = width;
			if (sg_iFontWidth != 0)
			{
				resolvedWidth = sg_iFontWidth;
			}
			return ApplyGlyphAspectRatioToWidth(resolvedWidth, referenceHeight);
		}
		static int ClampToNonNegative(int value)
		{
			return value < 0 ? 0 : value;
		}
		static BYTE ResolveOverrideCharSet(BYTE currentCharSet)
		{
			if (sg_dwCharSet != 0)
			{
				return static_cast<BYTE>(sg_dwCharSet);
			}
			if (sg_enableCharsetSpoof && currentCharSet == static_cast<BYTE>(sg_dwSpoofFromCharSet))
			{
				return static_cast<BYTE>(sg_dwSpoofToCharSet);
			}
			return currentCharSet;
		}
		static DWORD ResolveOverrideCharSet(DWORD currentCharSet)
		{
			return static_cast<DWORD>(ResolveOverrideCharSet(static_cast<BYTE>(currentCharSet)));
		}
		static bool IsFontObjectHandle(HANDLE h)
		{
			return h && GetObjectType(h) == OBJ_FONT;
		}
		static bool TryGetObjectType(HANDLE h, DWORD& type)
		{
			type = 0;
			if (!h)
			{
				return false;
			}
			__try
			{
				type = GetObjectType(h);
				return type != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				type = 0;
				return false;
			}
		}
		static bool IsHdcObjectHandle(HDC hdc)
		{
			DWORD type = 0;
			if (!TryGetObjectType(hdc, type))
			{
				return false;
			}
			return type == OBJ_DC
				|| type == OBJ_MEMDC
				|| type == OBJ_METADC
				|| type == OBJ_ENHMETADC;
		}
		static void ApplyMetricsOffsetToTextMetricsA(LPTEXTMETRICA lptm)
		{
			if (!lptm || (sg_iMetricsOffsetTop == 0 && sg_iMetricsOffsetBottom == 0))
			{
				return;
			}
			int ascent = ClampToNonNegative((int)lptm->tmAscent + sg_iMetricsOffsetTop);
			int descent = ClampToNonNegative((int)lptm->tmDescent + sg_iMetricsOffsetBottom);
			lptm->tmAscent = (LONG)ascent;
			lptm->tmDescent = (LONG)descent;
			int height = ascent + descent;
			lptm->tmHeight = (LONG)(height > 0 ? height : 1);
			lptm->tmInternalLeading = (LONG)ClampToNonNegative((int)lptm->tmInternalLeading + sg_iMetricsOffsetTop);
			lptm->tmExternalLeading = (LONG)ClampToNonNegative((int)lptm->tmExternalLeading + sg_iMetricsOffsetBottom);
		}
		static void ApplyMetricsOffsetToTextMetricsW(LPTEXTMETRICW lptm)
		{
			if (!lptm || (sg_iMetricsOffsetTop == 0 && sg_iMetricsOffsetBottom == 0))
			{
				return;
			}
			int ascent = ClampToNonNegative((int)lptm->tmAscent + sg_iMetricsOffsetTop);
			int descent = ClampToNonNegative((int)lptm->tmDescent + sg_iMetricsOffsetBottom);
			lptm->tmAscent = (LONG)ascent;
			lptm->tmDescent = (LONG)descent;
			int height = ascent + descent;
			lptm->tmHeight = (LONG)(height > 0 ? height : 1);
			lptm->tmInternalLeading = (LONG)ClampToNonNegative((int)lptm->tmInternalLeading + sg_iMetricsOffsetTop);
			lptm->tmExternalLeading = (LONG)ClampToNonNegative((int)lptm->tmExternalLeading + sg_iMetricsOffsetBottom);
		}
		static void ApplyGlyphOffsetToTextMetricsA(LPTEXTMETRICA lptm)
		{
			if (!lptm || sg_iGlyphOffsetY == 0)
			{
				return;
			}
			int ascent = (int)lptm->tmAscent + sg_iGlyphOffsetY;
			int descent = (int)lptm->tmDescent - sg_iGlyphOffsetY;
			if (ascent < 0)
			{
				descent += ascent;
				ascent = 0;
			}
			if (descent < 0)
			{
				ascent += descent;
				descent = 0;
			}
			if (ascent < 0)
			{
				ascent = 0;
			}
			if (descent < 0)
			{
				descent = 0;
			}
			lptm->tmAscent = (LONG)ascent;
			lptm->tmDescent = (LONG)descent;
		}
		static void ApplyGlyphOffsetToTextMetricsW(LPTEXTMETRICW lptm)
		{
			if (!lptm || sg_iGlyphOffsetY == 0)
			{
				return;
			}
			int ascent = (int)lptm->tmAscent + sg_iGlyphOffsetY;
			int descent = (int)lptm->tmDescent - sg_iGlyphOffsetY;
			if (ascent < 0)
			{
				descent += ascent;
				ascent = 0;
			}
			if (descent < 0)
			{
				ascent += descent;
				descent = 0;
			}
			if (ascent < 0)
			{
				ascent = 0;
			}
			if (descent < 0)
			{
				descent = 0;
			}
			lptm->tmAscent = (LONG)ascent;
			lptm->tmDescent = (LONG)descent;
		}
		static void ApplyGlyphOffsetToABCArr(ABC* abc)
		{
			if (!abc)
			{
				return;
			}
			abc->abcA += sg_iMetricsOffsetLeft + sg_iGlyphOffsetX;
			abc->abcC += sg_iMetricsOffsetRight - sg_iGlyphOffsetX;
		}
		static void ApplyGlyphOffsetToABCFLOATArr(ABCFLOAT* abc)
		{
			if (!abc)
			{
				return;
			}
			abc->abcfA += (FLOAT)(sg_iMetricsOffsetLeft + sg_iGlyphOffsetX);
			abc->abcfC += (FLOAT)(sg_iMetricsOffsetRight - sg_iGlyphOffsetX);
		}
		static void ApplyMetricsOffsetToCharWidth(INT* widthValue)
		{
			if (!widthValue)
			{
				return;
			}
			*widthValue += (sg_iMetricsOffsetLeft + sg_iMetricsOffsetRight);
			if (*widthValue < 0)
			{
				*widthValue = 0;
			}
		}
		static void ApplyMetricsOffsetToCharWidthFloat(FLOAT* widthValue)
		{
			if (!widthValue)
			{
				return;
			}
			*widthValue += (FLOAT)(sg_iMetricsOffsetLeft + sg_iMetricsOffsetRight);
			if (*widthValue < 0.0f)
			{
				*widthValue = 0.0f;
			}
		}
		static void SetFontAdjustments(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_dwCharSet = uiCharSet;
			sg_enableCharsetSpoof = enableCharsetSpoof;
			sg_dwSpoofFromCharSet = spoofFromCharSet;
			sg_dwSpoofToCharSet = spoofToCharSet;
			sg_iFontHeight = iHeight;
			sg_iFontWidth = iWidth;
			sg_iFontWeight = iWeight;
			sg_fFontScale = fScale;
			sg_fFontSpacingScale = fSpacingScale;
			sg_fGlyphAspectRatio = fGlyphAspectRatio;
			sg_iGlyphOffsetX = iGlyphOffsetX;
			sg_iGlyphOffsetY = iGlyphOffsetY;
			sg_iMetricsOffsetLeft = iMetricsOffsetLeft;
			sg_iMetricsOffsetRight = iMetricsOffsetRight;
			sg_iMetricsOffsetTop = iMetricsOffsetTop;
			sg_iMetricsOffsetBottom = iMetricsOffsetBottom;
		}
		void SetFontHookRules(
			const wchar_t* const* skipFontNames,
			size_t skipCount,
			const wchar_t* const* redirectFromFontNames,
			const wchar_t* const* redirectToFontNames,
			size_t redirectCount)
		{
			sg_skipFontRuleKeys.clear();
			sg_fontRedirectRules.clear();

			for (size_t i = 0; i < skipCount; ++i)
			{
				if (!skipFontNames || !skipFontNames[i])
				{
					continue;
				}
				std::wstring key = NormalizeFontRuleKey(skipFontNames[i]);
				if (key.empty())
				{
					continue;
				}
				if (!IsSkipFontRuleMatch(key))
				{
					sg_skipFontRuleKeys.push_back(std::move(key));
				}
			}

			for (size_t i = 0; i < redirectCount; ++i)
			{
				if (!redirectFromFontNames || !redirectToFontNames || !redirectFromFontNames[i] || !redirectToFontNames[i])
				{
					continue;
				}
				FontNameRedirectRule rule = {};
				rule.sourceKey = NormalizeFontRuleKey(redirectFromFontNames[i]);
				rule.targetFontName = TrimWideCopy(redirectToFontNames[i]);
				if (rule.sourceKey.empty() || rule.targetFontName.empty())
				{
					continue;
				}
				sg_fontRedirectRules.push_back(std::move(rule));
			}
		}
		void EnableFontHookVerboseLog(bool enable)
		{
			sg_enableFontHookVerboseLog = enable;
			LogMessage(LogLevel::Info, L"EnableFontHookVerboseLog: %s", enable ? L"true" : L"false");
		}
		static pCreateFontA rawCreateFontA = CreateFontA;
		
		HFONT WINAPI newCreateFontA_SehImpl(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			FontCreateNestingScope scope;
			bool skipOverride = false;
			std::string redirectFaceName;
			char requestedFaceName[LF_FACESIZE] = {};
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (HasFontOverrideRules()
				&& TryCopyAnsiFaceName(pszFaceName, requestedFaceName)
				&& TryResolveRedirectFontNameW(AnsiToWide(requestedFaceName).c_str(), redirectedFaceNameW, skipOverride))
			{
				redirectFaceName = WideFaceNameToAnsi(redirectedFaceNameW);
			}
			if (skipOverride)
			{
				return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			// Font size scaling: fixed height first, otherwise apply scale
			if (sg_iFontHeight) {
				cHeight = sg_iFontHeight;
			} else if (sg_fFontScale != 1.0f) {
				cHeight = (INT)(cHeight * sg_fFontScale);
			}
			
			cWidth = (INT)ResolveScaledFontWidth(cWidth, cHeight);
			
			if (sg_iFontWeight) { cWeight = sg_iFontWeight; }
			iCharSet = ResolveOverrideCharSet(iCharSet);
			if (!redirectFaceName.empty())
			{
				pszFaceName = redirectFaceName.c_str();
			}
			else
			{
				pszFaceName = sg_lpFontName;
			}
			HFONT hFont = rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontA(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
		{
			LogFontHookHit(L"CreateFontA");
			__try { return newCreateFontA_SehImpl(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName); }
		}


		bool HookCreateFontA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontName = cpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return !TryDetourAttach(&rawCreateFontA, newCreateFontA);
		}
		//*********END Hook CreateFontA*********


		//*********Start Hook CreateFontIndirectA*******
		static pCreateFontIndirectA rawCreateFontIndirectA = CreateFontIndirectA;
		HFONT WINAPI newCreateFontIndirectA_SehImpl(LOGFONTA* lplf)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectA(lplf);
			}
			FontCreateNestingScope scope;
			LOGFONTA local = {};
			if (!TryCopyLogFontA(lplf, local))
			{
				return rawCreateFontIndirectA(lplf);
			}
			bool skipOverride = false;
			std::string forcedFaceName;
			char requestedFaceName[LF_FACESIZE] = {};
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (HasFontOverrideRules()
				&& TryCopyAnsiFaceName(local.lfFaceName, requestedFaceName)
				&& TryResolveRedirectFontNameW(AnsiToWide(requestedFaceName).c_str(), redirectedFaceNameW, skipOverride))
			{
				forcedFaceName = WideFaceNameToAnsi(redirectedFaceNameW);
			}
			if (skipOverride)
			{
				return rawCreateFontIndirectA(lplf);
			}
			// Font size scaling: fixed value first, otherwise apply scale
			if (sg_iFontHeight) {
				local.lfHeight = sg_iFontHeight;
			} else if (sg_fFontScale != 1.0f) {
				local.lfHeight = (LONG)(local.lfHeight * sg_fFontScale);
			}
			
			local.lfWidth = ResolveScaledFontWidth(local.lfWidth, local.lfHeight);
			
			if (sg_iFontWeight) { local.lfWeight = sg_iFontWeight; }
			local.lfCharSet = ResolveOverrideCharSet(local.lfCharSet);
			if (!forcedFaceName.empty())
			{
				strcpy_s(local.lfFaceName, forcedFaceName.c_str());
			}
			else
			{
				strcpy_s(local.lfFaceName, sg_lpFontName);
			}
			HFONT hFont = rawCreateFontIndirectA(&local);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontIndirectA(LOGFONTA* lplf)
		{
			LogFontHookHit(L"CreateFontIndirectA");
			__try { return newCreateFontIndirectA_SehImpl(lplf); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontIndirectA(lplf); }
		}


		bool HookCreateFontIndirectA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontName = cpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return !TryDetourAttach(&rawCreateFontIndirectA, newCreateFontIndirectA);
		}
		//*********END Hook CreateFontIndirectA*********

		//*********Start Hook CreateFontW*******
		static pCreateFontW rawCreateFontW = CreateFontW;
		HFONT WINAPI newCreateFontW_SehImpl(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			FontCreateNestingScope scope;
			bool skipOverride = false;
			wchar_t requestedFaceName[LF_FACESIZE] = {};
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			if (!TryCopyWideFaceName(pszFaceName, requestedFaceName))
			{
				return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			bool hasForcedFaceName = TryGetForcedFontNameWForRequest(requestedFaceName, forcedFaceName, skipOverride);
			if (skipOverride)
			{
				return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			// Font size scaling: fixed height first, otherwise apply scale
			if (sg_iFontHeight) {
				cHeight = sg_iFontHeight;
			} else if (sg_fFontScale != 1.0f) {
				cHeight = (INT)(cHeight * sg_fFontScale);
			}
			
			cWidth = (INT)ResolveScaledFontWidth(cWidth, cHeight);
			
			if (sg_iFontWeight) { cWeight = sg_iFontWeight; }
			iCharSet = ResolveOverrideCharSet(iCharSet);
			if (hasForcedFaceName)
			{
				pszFaceName = forcedFaceName;
			}
			HFONT hFont = rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontW(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
		{
			LogFontHookHit(L"CreateFontW");
			__try { return newCreateFontW_SehImpl(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName); }
		}


		bool HookCreateFontW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontNameW = wpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return !TryDetourAttach(&rawCreateFontW, newCreateFontW);
		}
		//*********END Hook CreateFontW*******


		//*********Start Hook CreateFontIndirectW*******
		static pCreateFontIndirectW rawCreateFontIndirectW = CreateFontIndirectW;
		HFONT WINAPI newCreateFontIndirectW_SehImpl(LOGFONTW* lplf)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectW(lplf);
			}
			FontCreateNestingScope scope;
			LOGFONTW local = {};
			if (!TryCopyLogFontW(lplf, local))
			{
				return rawCreateFontIndirectW(lplf);
			}
			if (local.lfFaceName[0] == L'\0')
			{
				return rawCreateFontIndirectW(lplf);
			}
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			bool hasForcedFaceName = TryGetForcedFontNameWForRequest(local.lfFaceName, forcedFaceName, skipOverride);
			if (skipOverride)
			{
				return rawCreateFontIndirectW(lplf);
			}
			// Font size scaling: fixed value first, otherwise apply scale
			if (sg_iFontHeight) {
				local.lfHeight = sg_iFontHeight;
			} else if (sg_fFontScale != 1.0f) {
				local.lfHeight = (LONG)(local.lfHeight * sg_fFontScale);
			}
			
			local.lfWidth = ResolveScaledFontWidth(local.lfWidth, local.lfHeight);
			
			if (sg_iFontWeight) { local.lfWeight = sg_iFontWeight; }
			local.lfCharSet = ResolveOverrideCharSet(local.lfCharSet);
			if (hasForcedFaceName)
			{
				wcsncpy_s(local.lfFaceName, forcedFaceName, _TRUNCATE);
			}
			HFONT hFont = rawCreateFontIndirectW(&local);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontIndirectW(LOGFONTW* lplf)
		{
			LogFontHookHit(L"CreateFontIndirectW");
			__try { return newCreateFontIndirectW_SehImpl(lplf); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontIndirectW(lplf); }
		}


		bool HookCreateFontIndirectW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontNameW = wpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return !TryDetourAttach(&rawCreateFontIndirectW, newCreateFontIndirectW);
		}
		//*********END Hook CreateFontIndirectW*********

		static bool sg_unlockFontSelection = false;
		static pEnumFontFamiliesExA rawEnumFontFamiliesExA = EnumFontFamiliesExA;
		static pEnumFontFamiliesExW rawEnumFontFamiliesExW = EnumFontFamiliesExW;
		static bool HasPositiveTextMetrics(const TEXTMETRICA* lpntme)
		{
			if (!lpntme)
			{
				return true;
			}
			return lpntme->tmHeight > 0
				&& (lpntme->tmAscent + lpntme->tmDescent) > 0
				&& lpntme->tmAveCharWidth > 0
				&& lpntme->tmMaxCharWidth > 0;
		}
		static bool HasPositiveTextMetrics(const TEXTMETRICW* lpntme)
		{
			if (!lpntme)
			{
				return true;
			}
			return lpntme->tmHeight > 0
				&& (lpntme->tmAscent + lpntme->tmDescent) > 0
				&& lpntme->tmAveCharWidth > 0
				&& lpntme->tmMaxCharWidth > 0;
		}
		int WINAPI newEnumFontFamiliesExA_SehImpl(HDC hdc, LPLOGFONTA lpLogfont, pFONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
{
			LOGFONTA target = {};
			if (lpLogfont)
			{
				target = *lpLogfont;
			}
			if (sg_unlockFontSelection)
			{
				target.lfCharSet = DEFAULT_CHARSET;
				target.lfFaceName[0] = '\0';
			}
			return rawEnumFontFamiliesExA(hdc, &target, lpProc, lParam, dwFlags);
		}
		int WINAPI newEnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, pFONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
		{
			LogFontHookHit(L"EnumFontFamiliesExA");
			__try { return newEnumFontFamiliesExA_SehImpl(hdc, lpLogfont, lpProc, lParam, dwFlags); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontFamiliesExA(hdc, lpLogfont, lpProc, lParam, dwFlags); }
		}


		int WINAPI newEnumFontFamiliesExW_SehImpl(HDC hdc, LPLOGFONTW lpLogfont, pFONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
{
			LOGFONTW target = {};
			if (lpLogfont)
			{
				target = *lpLogfont;
			}
			if (sg_unlockFontSelection)
			{
				target.lfCharSet = DEFAULT_CHARSET;
				target.lfFaceName[0] = L'\0';
			}
			return rawEnumFontFamiliesExW(hdc, &target, lpProc, lParam, dwFlags);
		}
		int WINAPI newEnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, pFONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
		{
			LogFontHookHit(L"EnumFontFamiliesExW");
			__try { return newEnumFontFamiliesExW_SehImpl(hdc, lpLogfont, lpProc, lParam, dwFlags); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontFamiliesExW(hdc, lpLogfont, lpProc, lParam, dwFlags); }
		}


		bool HookEnumFontFamiliesExA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontFamiliesExA, newEnumFontFamiliesExA);
		}

		bool HookEnumFontFamiliesExW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontFamiliesExW, newEnumFontFamiliesExW);
		}

		static pSelectObject rawSelectObject = SelectObject;

		static bool ApplyOverrideToLogFontW(LOGFONTW& lf, bool applySizeScale)
		{
			bool changed = false;
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			if (TryGetForcedFontNameWForRequest(lf.lfFaceName, forcedFaceName, skipOverride))
			{
				if (wcsncmp(lf.lfFaceName, forcedFaceName, LF_FACESIZE) != 0)
				{
					wcsncpy_s(lf.lfFaceName, forcedFaceName, _TRUNCATE);
					changed = true;
				}
			}
			else if (skipOverride)
			{
				return false;
			}
			BYTE resolvedCharSet = ResolveOverrideCharSet(lf.lfCharSet);
			if (lf.lfCharSet != resolvedCharSet)
			{
				lf.lfCharSet = resolvedCharSet;
				changed = true;
			}
			if (applySizeScale)
			{
				LONG newHeight = lf.lfHeight;
				LONG newWidth = lf.lfWidth;
				if (sg_iFontHeight != 0)
				{
					newHeight = sg_iFontHeight;
				}
				else if (sg_fFontScale != 1.0f)
				{
					newHeight = (LONG)(newHeight * sg_fFontScale);
				}
				newWidth = ResolveScaledFontWidth(newWidth, newHeight);
				if (lf.lfHeight != newHeight)
				{
					lf.lfHeight = newHeight;
					changed = true;
				}
				if (lf.lfWidth != newWidth)
				{
					lf.lfWidth = newWidth;
					changed = true;
				}
			}
			if (sg_iFontWeight != 0 && lf.lfWeight != sg_iFontWeight)
			{
				lf.lfWeight = sg_iFontWeight;
				changed = true;
			}
			return changed;
		}
		static bool ApplyOverrideToLogFontA(LOGFONTA& lf, bool applySizeScale)
		{
			bool changed = false;
			bool skipOverride = false;
			std::string forcedFaceName;
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (HasFontOverrideRules())
			{
				if (TryResolveRedirectFontNameW(AnsiToWide(lf.lfFaceName).c_str(), redirectedFaceNameW, skipOverride))
				{
					forcedFaceName = WideFaceNameToAnsi(redirectedFaceNameW);
				}
				else if (skipOverride)
				{
					return false;
				}
			}
			if (forcedFaceName.empty())
			{
				forcedFaceName = GetForcedFontNameA();
			}
			if (!forcedFaceName.empty() && strncmp(lf.lfFaceName, forcedFaceName.c_str(), LF_FACESIZE) != 0)
			{
				strcpy_s(lf.lfFaceName, forcedFaceName.c_str());
				changed = true;
			}
			BYTE resolvedCharSet = ResolveOverrideCharSet(lf.lfCharSet);
			if (lf.lfCharSet != resolvedCharSet)
			{
				lf.lfCharSet = resolvedCharSet;
				changed = true;
			}
			if (applySizeScale)
			{
				LONG newHeight = lf.lfHeight;
				LONG newWidth = lf.lfWidth;
				if (sg_iFontHeight != 0)
				{
					newHeight = sg_iFontHeight;
				}
				else if (sg_fFontScale != 1.0f)
				{
					newHeight = (LONG)(newHeight * sg_fFontScale);
				}
				newWidth = ResolveScaledFontWidth(newWidth, newHeight);
				if (lf.lfHeight != newHeight)
				{
					lf.lfHeight = newHeight;
					changed = true;
				}
				if (lf.lfWidth != newWidth)
				{
					lf.lfWidth = newWidth;
					changed = true;
				}
			}
			if (sg_iFontWeight != 0 && lf.lfWeight != sg_iFontWeight)
			{
				lf.lfWeight = sg_iFontWeight;
				changed = true;
			}
			return changed;
		}

		static std::string GetForcedFontNameA()
		{
			if (sg_lpFontName && sg_lpFontName[0] != '\0')
			{
				return std::string(sg_lpFontName);
			}
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			if (!TryGetForcedFontNameW(forcedFaceName))
			{
				return "";
			}
			return WideFaceNameToAnsi(forcedFaceName);
		}
		static std::string GetForcedFontNameAForRequest(const char* requestedFaceName, bool& skipOverride)
		{
			skipOverride = false;
			if (HasFontOverrideRules())
			{
				wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
				if (TryResolveRedirectFontNameW(AnsiToWide(requestedFaceName).c_str(), redirectedFaceNameW, skipOverride))
				{
					return WideFaceNameToAnsi(redirectedFaceNameW);
				}
				if (skipOverride)
				{
					return "";
				}
			}
			return GetForcedFontNameA();
		}

		static pGetObjectA rawGetObjectA = GetObjectA;
		static pGetObjectW rawGetObjectW = GetObjectW;

		static HFONT ReplaceHdcFont(HDC hdc, HFONT* pOldFont)
		{
			if (pOldFont)
			{
				*pOldFont = nullptr;
			}
			if (!hdc || sg_hdcFontReplacementNesting > 0 || !IsHdcObjectHandle(hdc))
			{
				return nullptr;
			}
			HdcFontReplacementScope replacementScope;
			HFONT hCurFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
			if (!hCurFont)
			{
				return nullptr;
			}
			LOGFONTW lf = {};
			if (rawGetObjectW(hCurFont, sizeof(lf), &lf) == 0)
			{
				return nullptr;
			}
			bool applySizeScale = !IsFontHandleMarkedScaled(hCurFont);
			if (!ApplyOverrideToLogFontW(lf, applySizeScale))
			{
				return nullptr;
			}
			HFONT hNewFont = rawCreateFontIndirectW(&lf);
			if (!hNewFont)
			{
				return nullptr;
			}
			HGDIOBJ oldObject = rawSelectObject(hdc, hNewFont);
			if (!oldObject || oldObject == HGDI_ERROR)
			{
				DeleteObject(hNewFont);
				return nullptr;
			}
			if (applySizeScale && IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hNewFont);
			}
			if (pOldFont)
			{
				*pOldFont = (HFONT)oldObject;
			}
			replacementScope.release();
			return hNewFont;
		}

		static void RestoreHdcFont(HDC hdc, HFONT hOldFont, HFONT hNewFont)
		{
			if (!hNewFont)
			{
				return;
			}
			if (hOldFont && (HGDIOBJ)hOldFont != HGDI_ERROR)
			{
				rawSelectObject(hdc, hOldFont);
			}
			DeleteObject(hNewFont);
			if (sg_hdcFontReplacementNesting > 0)
			{
				--sg_hdcFontReplacementNesting;
			}
		}

		static bool TryGetCurrentHdcLogFontW(HDC hdc, LOGFONTW& lf)
		{
			if (!hdc)
			{
				return false;
			}
			HFONT hCurFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
			if (!hCurFont)
			{
				return false;
			}
			return rawGetObjectW(hCurFont, sizeof(lf), &lf) != 0;
		}
		static pGetTextFaceA rawGetTextFaceA = GetTextFaceA;
		static pGetTextFaceW rawGetTextFaceW = GetTextFaceW;
		static pGetTextMetricsA rawGetTextMetricsA = GetTextMetricsA;
		static pGetTextMetricsW rawGetTextMetricsW = GetTextMetricsW;
		static bool IsSafeChooseFontResultA(const LOGFONTA* logFont)
		{
			if (!logFont)
			{
				return true;
			}
			HDC hdc = CreateCompatibleDC(nullptr);
			if (!hdc)
			{
				return true;
			}
			HFONT hFont = rawCreateFontIndirectA(logFont);
			if (!hFont)
			{
				DeleteDC(hdc);
				return false;
			}
			HGDIOBJ oldObject = rawSelectObject(hdc, hFont);
			if (!oldObject || oldObject == HGDI_ERROR)
			{
				DeleteObject(hFont);
				DeleteDC(hdc);
				return false;
			}
			TEXTMETRICA metrics = {};
			BOOL ok = rawGetTextMetricsA(hdc, &metrics);
			rawSelectObject(hdc, oldObject);
			DeleteObject(hFont);
			DeleteDC(hdc);
			return ok && HasPositiveTextMetrics(&metrics);
		}
		static bool IsSafeChooseFontResultW(const LOGFONTW* logFont)
		{
			if (!logFont)
			{
				return true;
			}
			HDC hdc = CreateCompatibleDC(nullptr);
			if (!hdc)
			{
				return true;
			}
			HFONT hFont = rawCreateFontIndirectW(logFont);
			if (!hFont)
			{
				DeleteDC(hdc);
				return false;
			}
			HGDIOBJ oldObject = rawSelectObject(hdc, hFont);
			if (!oldObject || oldObject == HGDI_ERROR)
			{
				DeleteObject(hFont);
				DeleteDC(hdc);
				return false;
			}
			TEXTMETRICW metrics = {};
			BOOL ok = rawGetTextMetricsW(hdc, &metrics);
			rawSelectObject(hdc, oldObject);
			DeleteObject(hFont);
			DeleteDC(hdc);
			return ok && HasPositiveTextMetrics(&metrics);
		}
		static pCreateFontIndirectExA rawCreateFontIndirectExA = CreateFontIndirectExA;
		static pCreateFontIndirectExW rawCreateFontIndirectExW = CreateFontIndirectExW;
		static pGetCharABCWidthsA rawGetCharABCWidthsA = GetCharABCWidthsA;
		static pGetCharABCWidthsW rawGetCharABCWidthsW = GetCharABCWidthsW;
		static pGetCharABCWidthsFloatA rawGetCharABCWidthsFloatA = GetCharABCWidthsFloatA;
		static pGetCharABCWidthsFloatW rawGetCharABCWidthsFloatW = GetCharABCWidthsFloatW;
		static pGetCharWidthA rawGetCharWidthA = GetCharWidthA;
		static pGetCharWidthW rawGetCharWidthW = GetCharWidthW;
		static pGetCharWidth32A rawGetCharWidth32A = GetCharWidth32A;
		static pGetCharWidth32W rawGetCharWidth32W = GetCharWidth32W;
		static pGetKerningPairsA rawGetKerningPairsA = GetKerningPairsA;
		static pGetKerningPairsW rawGetKerningPairsW = GetKerningPairsW;
		static pGetOutlineTextMetricsA rawGetOutlineTextMetricsA = GetOutlineTextMetricsA;
		static pGetOutlineTextMetricsW rawGetOutlineTextMetricsW = GetOutlineTextMetricsW;
		static pAddFontResourceA rawAddFontResourceA = AddFontResourceA;
		static pAddFontResourceW rawAddFontResourceW = AddFontResourceW;
		static pAddFontResourceExA rawAddFontResourceExA = AddFontResourceExA;
		static pAddFontMemResourceEx rawAddFontMemResourceEx = AddFontMemResourceEx;
		static pRemoveFontResourceA rawRemoveFontResourceA = RemoveFontResourceA;
		static pRemoveFontResourceW rawRemoveFontResourceW = RemoveFontResourceW;
		static pRemoveFontResourceExA rawRemoveFontResourceExA = RemoveFontResourceExA;
		static pRemoveFontMemResourceEx rawRemoveFontMemResourceEx = RemoveFontMemResourceEx;
		static pEnumFontsA rawEnumFontsA = EnumFontsA;
		static pEnumFontsW rawEnumFontsW = EnumFontsW;
		static pEnumFontFamiliesA rawEnumFontFamiliesA = EnumFontFamiliesA;
		static pEnumFontFamiliesW rawEnumFontFamiliesW = EnumFontFamiliesW;
		static pChooseFontA rawChooseFontA = nullptr;
		static pChooseFontW rawChooseFontW = nullptr;
		static DWORD UnlockChooseFontFlags(DWORD flags)
		{
			flags &= ~(CF_SELECTSCRIPT
				| CF_SCRIPTSONLY
				| CF_FIXEDPITCHONLY
				| CF_TTONLY
				| CF_SCALABLEONLY
				| CF_NOVECTORFONTS
				| CF_NOSIMULATIONS
				| CF_NOVERTFONTS
				| CF_WYSIWYG
				| CF_LIMITSIZE
				| CF_PRINTERFONTS);
			flags |= CF_SCREENFONTS;
			return flags;
		}
		static pGetCharWidthFloatA rawGetCharWidthFloatA = GetCharWidthFloatA;
		static pGetCharWidthFloatW rawGetCharWidthFloatW = GetCharWidthFloatW;
		static pGetCharWidthI rawGetCharWidthI = GetCharWidthI;
		static pGetCharABCWidthsI rawGetCharABCWidthsI = GetCharABCWidthsI;
		static pGetTextExtentPointI rawGetTextExtentPointI = GetTextExtentPointI;
		static pGetTextExtentExPointI rawGetTextExtentExPointI = GetTextExtentExPointI;
		static pGetFontData rawGetFontData = GetFontData;
		static pGetFontLanguageInfo rawGetFontLanguageInfo = GetFontLanguageInfo;
		static pGetFontUnicodeRanges rawGetFontUnicodeRanges = GetFontUnicodeRanges;
		static pLoadLibraryW rawLoadLibraryW = LoadLibraryW;
		static pLoadLibraryExW rawLoadLibraryExW = LoadLibraryExW;
		static pDWriteCreateFactory rawDWriteCreateFactory = nullptr;
			static pD2D1CreateFactory rawD2D1CreateFactory = nullptr;
				static pD2D1CreateDevice rawD2D1CreateDevice = nullptr;
				static pD2D1CreateDeviceContext rawD2D1CreateDeviceContext = nullptr;
		static pDWriteFactoryCreateTextFormat rawDWriteFactoryCreateTextFormat = nullptr;
			static pDWriteFactoryCreateTextLayout rawDWriteFactoryCreateTextLayout = nullptr;
			static pDWriteFactoryCreateGdiCompatibleTextLayout rawDWriteFactoryCreateGdiCompatibleTextLayout = nullptr;
				static pDWriteTextLayoutSetFontFamilyName rawDWriteTextLayoutSetFontFamilyName = nullptr;
				static pDWriteTextLayoutSetFontSize rawDWriteTextLayoutSetFontSize = nullptr;
		static pD2D1FactoryCreateHwndRenderTarget rawD2D1FactoryCreateHwndRenderTarget = nullptr;
			static pD2D1FactoryCreateDxgiSurfaceRenderTarget rawD2D1FactoryCreateDxgiSurfaceRenderTarget = nullptr;
			static pD2D1FactoryCreateDCRenderTarget rawD2D1FactoryCreateDCRenderTarget = nullptr;
			static pD2D1FactoryCreateWicBitmapRenderTarget rawD2D1FactoryCreateWicBitmapRenderTarget = nullptr;
				static pD2D1Factory1CreateDevice rawD2D1Factory1CreateDevice = nullptr;
				static pD2D1DeviceCreateDeviceContext rawD2D1DeviceCreateDeviceContext = nullptr;
			static pD2D1RenderTargetDrawText rawD2D1RenderTargetDrawText = nullptr;
			static pD2D1RenderTargetDrawTextLayout rawD2D1RenderTargetDrawTextLayout = nullptr;
			static pGdipCreateFontFamilyFromName rawGdipCreateFontFamilyFromName = nullptr;
		static pGdipCreateFontFromLogfontW rawGdipCreateFontFromLogfontW = nullptr;
		static pGdipCreateFontFromLogfontA rawGdipCreateFontFromLogfontA = nullptr;
		static pGdipCreateFontFromHFONT rawGdipCreateFontFromHFONT = nullptr;
		static pGdipCreateFontFromDC rawGdipCreateFontFromDC = nullptr;
		static pGdipCreateFont rawGdipCreateFont = nullptr;
		static pGdipDrawString rawGdipDrawString = nullptr;
		static pGdipDrawDriverString rawGdipDrawDriverString = nullptr;
		static pGdipMeasureString rawGdipMeasureString = nullptr;
		static pGdipMeasureCharacterRanges rawGdipMeasureCharacterRanges = nullptr;
		static pGdipMeasureDriverString rawGdipMeasureDriverString = nullptr;
		static bool sg_hookedDWriteCreateFactory = false;
		static bool sg_hookedGdipCreateFontFamily = false;
		static bool sg_hookedGdipCreateFontFromLogfontW = false;
		static bool sg_hookedGdipCreateFontFromLogfontA = false;
		static bool sg_hookedGdipCreateFontFromHFONT = false;
		static bool sg_hookedGdipCreateFontFromDC = false;
		static bool sg_hookedGdipCreateFont = false;
		static bool sg_hookedGdipDrawString = false;
		static bool sg_hookedGdipDrawDriverString = false;
		static bool sg_hookedGdipMeasureString = false;
		static bool sg_hookedGdipMeasureCharacterRanges = false;
		static bool sg_hookedGdipMeasureDriverString = false;
		static bool sg_hookedLoadLibraryW = false;
		static bool sg_hookedLoadLibraryExW = false;
		static bool sg_hookedDWriteCreateTextFormat = false;
		static bool sg_hookedDWriteCreateTextLayout = false;
			static bool sg_hookedDWriteTextLayoutSetFontFamilyName = false;
			static bool sg_hookedDWriteTextLayoutSetFontSize = false;
		static bool sg_hookedDWriteCreateGdiCompatibleTextLayout = false;
		static bool sg_hookedD2D1CreateFactory = false;
			static bool sg_hookedD2D1CreateDevice = false;
			static bool sg_hookedD2D1CreateDeviceContext = false;
		static bool sg_hookedD2D1FactoryCreateHwndRenderTarget = false;
		static bool sg_hookedD2D1FactoryCreateDxgiSurfaceRenderTarget = false;
		static bool sg_hookedD2D1FactoryCreateDCRenderTarget = false;
		static bool sg_hookedD2D1FactoryCreateWicBitmapRenderTarget = false;
			static bool sg_hookedD2D1Factory1CreateDevice = false;
			static bool sg_hookedD2D1DeviceCreateDeviceContext = false;
		static bool sg_hookedD2D1RenderTargetDrawText = false;
		static bool sg_hookedD2D1RenderTargetDrawTextLayout = false;
		static bool sg_hookedChooseFontA = false;
		static bool sg_hookedChooseFontW = false;

		int WINAPI newGetObjectA_SehImpl(HANDLE h, int c, LPVOID pv)
{
			int ret = rawGetObjectA(h, c, pv);
			if (ret >= (int)sizeof(LOGFONTA) && pv && c >= (int)sizeof(LOGFONTA) && IsFontObjectHandle(h))
			{
				LOGFONTA* lf = (LOGFONTA*)pv;
				ApplyOverrideToLogFontA(*lf, false);
			}
			return ret;
		}
		int WINAPI newGetObjectA(HANDLE h, int c, LPVOID pv)
		{
			LogFontHookHit(L"GetObjectA");
			__try { return newGetObjectA_SehImpl(h, c, pv); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetObjectA(h, c, pv); }
		}


		int WINAPI newGetObjectW_SehImpl(HANDLE h, int c, LPVOID pv)
{
			int ret = rawGetObjectW(h, c, pv);
			if (ret >= (int)sizeof(LOGFONTW) && pv && c >= (int)sizeof(LOGFONTW) && IsFontObjectHandle(h))
			{
				LOGFONTW* lf = (LOGFONTW*)pv;
				ApplyOverrideToLogFontW(*lf, false);
			}
			return ret;
		}
		int WINAPI newGetObjectW(HANDLE h, int c, LPVOID pv)
		{
			LogFontHookHit(L"GetObjectW");
			__try { return newGetObjectW_SehImpl(h, c, pv); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetObjectW(h, c, pv); }
		}


		int WINAPI newGetTextFaceA_SehImpl(HDC hdc, int c, LPSTR lpName)
{
			LOGFONTW currentLf = {};
			bool skipOverride = false;
			std::string forced;
			if (TryGetCurrentHdcLogFontW(hdc, currentLf))
			{
				wchar_t forcedFaceNameW[LF_FACESIZE] = {};
				if (TryGetForcedFontNameWForRequest(currentLf.lfFaceName, forcedFaceNameW, skipOverride))
				{
					forced = WideFaceNameToAnsi(forcedFaceNameW);
				}
			}
			else
			{
				forced = GetForcedFontNameA();
			}
			if (forced.empty())
			{
				return rawGetTextFaceA(hdc, c, lpName);
			}
			int needed = (int)forced.length() + 1;
			if (!lpName || c <= 0)
			{
				return needed;
			}
			strncpy_s(lpName, c, forced.c_str(), _TRUNCATE);
			return (int)strlen(lpName);
		}
		int WINAPI newGetTextFaceA(HDC hdc, int c, LPSTR lpName)
		{
			LogFontHookHit(L"GetTextFaceA");
			__try { return newGetTextFaceA_SehImpl(hdc, c, lpName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextFaceA(hdc, c, lpName); }
		}


		int WINAPI newGetTextFaceW_SehImpl(HDC hdc, int c, LPWSTR lpName)
{
			LOGFONTW currentLf = {};
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			bool hasForcedFaceName = TryGetCurrentHdcLogFontW(hdc, currentLf)
				? TryGetForcedFontNameWForRequest(currentLf.lfFaceName, forcedFaceName, skipOverride)
				: TryGetForcedFontNameW(forcedFaceName);
			if (!hasForcedFaceName)
			{
				return rawGetTextFaceW(hdc, c, lpName);
			}
			int needed = (int)wcslen(forcedFaceName) + 1;
			if (!lpName || c <= 0)
			{
				return needed;
			}
			wcsncpy_s(lpName, c, forcedFaceName, _TRUNCATE);
			return (int)wcslen(lpName);
		}
		int WINAPI newGetTextFaceW(HDC hdc, int c, LPWSTR lpName)
		{
			LogFontHookHit(L"GetTextFaceW");
			__try { return newGetTextFaceW_SehImpl(hdc, c, lpName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextFaceW(hdc, c, lpName); }
		}


		BOOL WINAPI newGetTextMetricsA_SehImpl(HDC hdc, LPTEXTMETRICA lptm)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextMetricsA(hdc, lptm);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret)
			{
				ApplyMetricsOffsetToTextMetricsA(lptm);
				ApplyGlyphOffsetToTextMetricsA(lptm);
			}
			return ret;
		}
		BOOL WINAPI newGetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm)
		{
			LogFontHookHit(L"GetTextMetricsA");
			__try { return newGetTextMetricsA_SehImpl(hdc, lptm); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextMetricsA(hdc, lptm); }
		}


		BOOL WINAPI newGetTextMetricsW_SehImpl(HDC hdc, LPTEXTMETRICW lptm)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextMetricsW(hdc, lptm);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret)
			{
				ApplyMetricsOffsetToTextMetricsW(lptm);
				ApplyGlyphOffsetToTextMetricsW(lptm);
			}
			return ret;
		}
		BOOL WINAPI newGetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm)
		{
			LogFontHookHit(L"GetTextMetricsW");
			__try { return newGetTextMetricsW_SehImpl(hdc, lptm); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextMetricsW(hdc, lptm); }
		}


		HFONT WINAPI newCreateFontIndirectExA_SehImpl(const ENUMLOGFONTEXDVA* penumlfex)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectExA(penumlfex);
			}
			FontCreateNestingScope scope;
			if (!penumlfex)
			{
				return rawCreateFontIndirectExA(penumlfex);
			}
			ENUMLOGFONTEXDVA local = *penumlfex;
			ApplyOverrideToLogFontA(local.elfEnumLogfontEx.elfLogFont);
			HFONT hFont = rawCreateFontIndirectExA(&local);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontIndirectExA(const ENUMLOGFONTEXDVA* penumlfex)
		{
			LogFontHookHit(L"CreateFontIndirectExA");
			__try { return newCreateFontIndirectExA_SehImpl(penumlfex); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontIndirectExA(penumlfex); }
		}


		HFONT WINAPI newCreateFontIndirectExW_SehImpl(const ENUMLOGFONTEXDVW* penumlfex)
{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectExW(penumlfex);
			}
			FontCreateNestingScope scope;
			if (!penumlfex)
			{
				return rawCreateFontIndirectExW(penumlfex);
			}
			ENUMLOGFONTEXDVW local = *penumlfex;
			ApplyOverrideToLogFontW(local.elfEnumLogfontEx.elfLogFont);
			HFONT hFont = rawCreateFontIndirectExW(&local);
			if (IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hFont);
			}
			return hFont;
		}
		HFONT WINAPI newCreateFontIndirectExW(const ENUMLOGFONTEXDVW* penumlfex)
		{
			LogFontHookHit(L"CreateFontIndirectExW");
			__try { return newCreateFontIndirectExW_SehImpl(penumlfex); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawCreateFontIndirectExW(penumlfex); }
		}


		BOOL WINAPI newGetCharABCWidthsA_SehImpl(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsA(hdc, wFirst, wLast, lpABC);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpABC != nullptr && wLast >= wFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iGlyphOffsetX != 0 || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = wLast - wFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpABC[i].abcA = (int)(lpABC[i].abcA * sg_fFontSpacingScale);
						lpABC[i].abcB = (UINT)(lpABC[i].abcB * sg_fFontSpacingScale);
						lpABC[i].abcC = (int)(lpABC[i].abcC * sg_fFontSpacingScale);
					}
					ApplyGlyphOffsetToABCArr(&lpABC[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharABCWidthsA(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
		{
			LogFontHookHit(L"GetCharABCWidthsA");
			__try { return newGetCharABCWidthsA_SehImpl(hdc, wFirst, wLast, lpABC); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharABCWidthsA(hdc, wFirst, wLast, lpABC); }
		}


		BOOL WINAPI newGetCharABCWidthsW_SehImpl(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsW(hdc, wFirst, wLast, lpABC);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpABC != nullptr && wLast >= wFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iGlyphOffsetX != 0 || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = wLast - wFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpABC[i].abcA = (int)(lpABC[i].abcA * sg_fFontSpacingScale);
						lpABC[i].abcB = (UINT)(lpABC[i].abcB * sg_fFontSpacingScale);
						lpABC[i].abcC = (int)(lpABC[i].abcC * sg_fFontSpacingScale);
					}
					ApplyGlyphOffsetToABCArr(&lpABC[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharABCWidthsW(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
		{
			LogFontHookHit(L"GetCharABCWidthsW");
			__try { return newGetCharABCWidthsW_SehImpl(hdc, wFirst, wLast, lpABC); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharABCWidthsW(hdc, wFirst, wLast, lpABC); }
		}


		BOOL WINAPI newGetCharABCWidthsFloatA_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsFloatA(hdc, iFirst, iLast, lpABC);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpABC != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iGlyphOffsetX != 0 || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpABC[i].abcfA *= sg_fFontSpacingScale;
						lpABC[i].abcfB *= sg_fFontSpacingScale;
						lpABC[i].abcfC *= sg_fFontSpacingScale;
					}
					ApplyGlyphOffsetToABCFLOATArr(&lpABC[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharABCWidthsFloatA(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
		{
			LogFontHookHit(L"GetCharABCWidthsFloatA");
			__try { return newGetCharABCWidthsFloatA_SehImpl(hdc, iFirst, iLast, lpABC); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharABCWidthsFloatA(hdc, iFirst, iLast, lpABC); }
		}


		BOOL WINAPI newGetCharABCWidthsFloatW_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsFloatW(hdc, iFirst, iLast, lpABC);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpABC != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iGlyphOffsetX != 0 || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpABC[i].abcfA *= sg_fFontSpacingScale;
						lpABC[i].abcfB *= sg_fFontSpacingScale;
						lpABC[i].abcfC *= sg_fFontSpacingScale;
					}
					ApplyGlyphOffsetToABCFLOATArr(&lpABC[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharABCWidthsFloatW(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
		{
			LogFontHookHit(L"GetCharABCWidthsFloatW");
			__try { return newGetCharABCWidthsFloatW_SehImpl(hdc, iFirst, iLast, lpABC); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharABCWidthsFloatW(hdc, iFirst, iLast, lpABC); }
		}


		BOOL WINAPI newGetCharWidthA_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidthA(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] = (INT)(lpBuffer[i] * sg_fFontSpacingScale);
					}
					ApplyMetricsOffsetToCharWidth(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidthA(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidthA");
			__try { return newGetCharWidthA_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidthA(hdc, iFirst, iLast, lpBuffer); }
		}


		BOOL WINAPI newGetCharWidthW_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidthW(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] = (INT)(lpBuffer[i] * sg_fFontSpacingScale);
					}
					ApplyMetricsOffsetToCharWidth(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidthW(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidthW");
			__try { return newGetCharWidthW_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidthW(hdc, iFirst, iLast, lpBuffer); }
		}


		BOOL WINAPI newGetCharWidth32A_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidth32A(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] = (INT)(lpBuffer[i] * sg_fFontSpacingScale);
					}
					ApplyMetricsOffsetToCharWidth(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidth32A(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidth32A");
			__try { return newGetCharWidth32A_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidth32A(hdc, iFirst, iLast, lpBuffer); }
		}


		BOOL WINAPI newGetCharWidth32W_SehImpl(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidth32W(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] = (INT)(lpBuffer[i] * sg_fFontSpacingScale);
					}
					ApplyMetricsOffsetToCharWidth(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidth32W(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidth32W");
			__try { return newGetCharWidth32W_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidth32W(hdc, iFirst, iLast, lpBuffer); }
		}


		DWORD WINAPI newGetKerningPairsA_SehImpl(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetKerningPairsA(hdc, nPairs, lpKerningPairs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		DWORD WINAPI newGetKerningPairsA(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
		{
			LogFontHookHit(L"GetKerningPairsA");
			__try { return newGetKerningPairsA_SehImpl(hdc, nPairs, lpKerningPairs); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetKerningPairsA(hdc, nPairs, lpKerningPairs); }
		}


		DWORD WINAPI newGetKerningPairsW_SehImpl(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetKerningPairsW(hdc, nPairs, lpKerningPairs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		DWORD WINAPI newGetKerningPairsW(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
		{
			LogFontHookHit(L"GetKerningPairsW");
			__try { return newGetKerningPairsW_SehImpl(hdc, nPairs, lpKerningPairs); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetKerningPairsW(hdc, nPairs, lpKerningPairs); }
		}


		UINT WINAPI newGetOutlineTextMetricsA_SehImpl(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA lpotm)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			UINT ret = rawGetOutlineTextMetricsA(hdc, cjCopy, lpotm);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret != 0 && lpotm)
			{
				ApplyMetricsOffsetToTextMetricsA(&lpotm->otmTextMetrics);
				ApplyGlyphOffsetToTextMetricsA(&lpotm->otmTextMetrics);
			}
			return ret;
		}
		UINT WINAPI newGetOutlineTextMetricsA(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA lpotm)
		{
			LogFontHookHit(L"GetOutlineTextMetricsA");
			__try { return newGetOutlineTextMetricsA_SehImpl(hdc, cjCopy, lpotm); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetOutlineTextMetricsA(hdc, cjCopy, lpotm); }
		}


		UINT WINAPI newGetOutlineTextMetricsW_SehImpl(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW lpotm)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			UINT ret = rawGetOutlineTextMetricsW(hdc, cjCopy, lpotm);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret != 0 && lpotm)
			{
				ApplyMetricsOffsetToTextMetricsW(&lpotm->otmTextMetrics);
				ApplyGlyphOffsetToTextMetricsW(&lpotm->otmTextMetrics);
			}
			return ret;
		}
		UINT WINAPI newGetOutlineTextMetricsW(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW lpotm)
		{
			LogFontHookHit(L"GetOutlineTextMetricsW");
			__try { return newGetOutlineTextMetricsW_SehImpl(hdc, cjCopy, lpotm); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetOutlineTextMetricsW(hdc, cjCopy, lpotm); }
		}


		int WINAPI newAddFontResourceA_SehImpl(LPCSTR lpFileName)
{
			return rawAddFontResourceA(lpFileName);
		}
		int WINAPI newAddFontResourceA(LPCSTR lpFileName)
		{
			LogFontHookHit(L"AddFontResourceA");
			__try { return newAddFontResourceA_SehImpl(lpFileName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawAddFontResourceA(lpFileName); }
		}


		int WINAPI newAddFontResourceW_SehImpl(LPCWSTR lpFileName)
{
			return rawAddFontResourceW(lpFileName);
		}
		int WINAPI newAddFontResourceW(LPCWSTR lpFileName)
		{
			LogFontHookHit(L"AddFontResourceW");
			__try { return newAddFontResourceW_SehImpl(lpFileName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawAddFontResourceW(lpFileName); }
		}


		int WINAPI newAddFontResourceExA_SehImpl(LPCSTR name, DWORD fl, PVOID pdv)
{
			return rawAddFontResourceExA(name, fl, pdv);
		}
		int WINAPI newAddFontResourceExA(LPCSTR name, DWORD fl, PVOID pdv)
		{
			LogFontHookHit(L"AddFontResourceExA");
			__try { return newAddFontResourceExA_SehImpl(name, fl, pdv); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawAddFontResourceExA(name, fl, pdv); }
		}


		HANDLE WINAPI newAddFontMemResourceEx_SehImpl(PVOID pbFont, DWORD cbFont, PVOID pdv, DWORD* pcFonts)
{
			return rawAddFontMemResourceEx(pbFont, cbFont, pdv, pcFonts);
		}
		HANDLE WINAPI newAddFontMemResourceEx(PVOID pbFont, DWORD cbFont, PVOID pdv, DWORD* pcFonts)
		{
			LogFontHookHit(L"AddFontMemResourceEx");
			__try { return newAddFontMemResourceEx_SehImpl(pbFont, cbFont, pdv, pcFonts); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawAddFontMemResourceEx(pbFont, cbFont, pdv, pcFonts); }
		}


		BOOL WINAPI newRemoveFontResourceA_SehImpl(LPCSTR lpFileName)
{
			return rawRemoveFontResourceA(lpFileName);
		}
		BOOL WINAPI newRemoveFontResourceA(LPCSTR lpFileName)
		{
			LogFontHookHit(L"RemoveFontResourceA");
			__try { return newRemoveFontResourceA_SehImpl(lpFileName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawRemoveFontResourceA(lpFileName); }
		}


		BOOL WINAPI newRemoveFontResourceW_SehImpl(LPCWSTR lpFileName)
{
			return rawRemoveFontResourceW(lpFileName);
		}
		BOOL WINAPI newRemoveFontResourceW(LPCWSTR lpFileName)
		{
			LogFontHookHit(L"RemoveFontResourceW");
			__try { return newRemoveFontResourceW_SehImpl(lpFileName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawRemoveFontResourceW(lpFileName); }
		}


		BOOL WINAPI newRemoveFontResourceExA_SehImpl(LPCSTR name, DWORD fl, PVOID pdv)
{
			return rawRemoveFontResourceExA(name, fl, pdv);
		}
		BOOL WINAPI newRemoveFontResourceExA(LPCSTR name, DWORD fl, PVOID pdv)
		{
			LogFontHookHit(L"RemoveFontResourceExA");
			__try { return newRemoveFontResourceExA_SehImpl(name, fl, pdv); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawRemoveFontResourceExA(name, fl, pdv); }
		}


		BOOL WINAPI newRemoveFontMemResourceEx_SehImpl(HANDLE h)
{
			return rawRemoveFontMemResourceEx(h);
		}
		BOOL WINAPI newRemoveFontMemResourceEx(HANDLE h)
		{
			LogFontHookHit(L"RemoveFontMemResourceEx");
			__try { return newRemoveFontMemResourceEx_SehImpl(h); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawRemoveFontMemResourceEx(h); }
		}


		int WINAPI newEnumFontsA_SehImpl(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
{
			LPCSTR useFaceName = sg_unlockFontSelection ? nullptr : lpFaceName;
			return rawEnumFontsA(hdc, useFaceName, lpProc, lParam);
		}
		int WINAPI newEnumFontsA(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
		{
			LogFontHookHit(L"EnumFontsA");
			__try { return newEnumFontsA_SehImpl(hdc, lpFaceName, lpProc, lParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontsA(hdc, lpFaceName, lpProc, lParam); }
		}


		int WINAPI newEnumFontsW_SehImpl(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
{
			LPCWSTR useFaceName = sg_unlockFontSelection ? nullptr : lpFaceName;
			return rawEnumFontsW(hdc, useFaceName, lpProc, lParam);
		}
		int WINAPI newEnumFontsW(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
		{
			LogFontHookHit(L"EnumFontsW");
			__try { return newEnumFontsW_SehImpl(hdc, lpFaceName, lpProc, lParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontsW(hdc, lpFaceName, lpProc, lParam); }
		}


		int WINAPI newEnumFontFamiliesA_SehImpl(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
{
			LPCSTR useFaceName = sg_unlockFontSelection ? nullptr : lpFaceName;
			return rawEnumFontFamiliesA(hdc, useFaceName, lpProc, lParam);
		}
		int WINAPI newEnumFontFamiliesA(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
		{
			LogFontHookHit(L"EnumFontFamiliesA");
			__try { return newEnumFontFamiliesA_SehImpl(hdc, lpFaceName, lpProc, lParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontFamiliesA(hdc, lpFaceName, lpProc, lParam); }
		}


		int WINAPI newEnumFontFamiliesW_SehImpl(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
{
			LPCWSTR useFaceName = sg_unlockFontSelection ? nullptr : lpFaceName;
			return rawEnumFontFamiliesW(hdc, useFaceName, lpProc, lParam);
		}
		int WINAPI newEnumFontFamiliesW(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
		{
			LogFontHookHit(L"EnumFontFamiliesW");
			__try { return newEnumFontFamiliesW_SehImpl(hdc, lpFaceName, lpProc, lParam); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawEnumFontFamiliesW(hdc, lpFaceName, lpProc, lParam); }
		}


		BOOL WINAPI newChooseFontA_SehImpl(LPCHOOSEFONTA lpcf)
{
			if (!rawChooseFontA || !lpcf || !sg_unlockFontSelection)
			{
				return rawChooseFontA ? rawChooseFontA(lpcf) : FALSE;
			}

			CHOOSEFONTA local = *lpcf;
			LOGFONTA localLogFont = {};
			LOGFONTA fallbackLogFont = {};
			LPLOGFONTA originalLogFont = local.lpLogFont;
			if (originalLogFont)
			{
				fallbackLogFont = *originalLogFont;
				localLogFont = *originalLogFont;
				localLogFont.lfCharSet = DEFAULT_CHARSET;
				localLogFont.lfFaceName[0] = '\0';
				local.lpLogFont = &localLogFont;
			}
			local.Flags = UnlockChooseFontFlags(local.Flags);

			BOOL ret = rawChooseFontA(&local);
			if (ret && originalLogFont && !IsSafeChooseFontResultA(&localLogFont))
			{
				localLogFont = fallbackLogFont;
				LogMessage(LogLevel::Warn, L"UnlockFontSelection: rejected unsafe ChooseFontA result");
			}
			if (originalLogFont)
			{
				*originalLogFont = localLogFont;
			}
			local.lpLogFont = originalLogFont;
			*lpcf = local;
			return ret;
		}
		BOOL WINAPI newChooseFontA(LPCHOOSEFONTA lpcf)
		{
			LogFontHookHit(L"ChooseFontA");
			__try { return newChooseFontA_SehImpl(lpcf); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawChooseFontA(lpcf); }
		}


		BOOL WINAPI newChooseFontW_SehImpl(LPCHOOSEFONTW lpcf)
{
			if (!rawChooseFontW || !lpcf || !sg_unlockFontSelection)
			{
				return rawChooseFontW ? rawChooseFontW(lpcf) : FALSE;
			}

			CHOOSEFONTW local = *lpcf;
			LOGFONTW localLogFont = {};
			LOGFONTW fallbackLogFont = {};
			LPLOGFONTW originalLogFont = local.lpLogFont;
			if (originalLogFont)
			{
				fallbackLogFont = *originalLogFont;
				localLogFont = *originalLogFont;
				localLogFont.lfCharSet = DEFAULT_CHARSET;
				localLogFont.lfFaceName[0] = L'\0';
				local.lpLogFont = &localLogFont;
			}
			local.Flags = UnlockChooseFontFlags(local.Flags);

			BOOL ret = rawChooseFontW(&local);
			if (ret && originalLogFont && !IsSafeChooseFontResultW(&localLogFont))
			{
				localLogFont = fallbackLogFont;
				LogMessage(LogLevel::Warn, L"UnlockFontSelection: rejected unsafe ChooseFontW result");
			}
			if (originalLogFont)
			{
				*originalLogFont = localLogFont;
			}
			local.lpLogFont = originalLogFont;
			*lpcf = local;
			return ret;
		}
		BOOL WINAPI newChooseFontW(LPCHOOSEFONTW lpcf)
		{
			LogFontHookHit(L"ChooseFontW");
			__try { return newChooseFontW_SehImpl(lpcf); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawChooseFontW(lpcf); }
		}


		BOOL WINAPI newGetCharWidthFloatA_SehImpl(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidthFloatA(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] *= sg_fFontSpacingScale;
					}
					ApplyMetricsOffsetToCharWidthFloat(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidthFloatA(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidthFloatA");
			__try { return newGetCharWidthFloatA_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidthFloatA(hdc, iFirst, iLast, lpBuffer); }
		}


		BOOL WINAPI newGetCharWidthFloatW_SehImpl(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidthFloatW(hdc, iFirst, iLast, lpBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && lpBuffer != nullptr && iLast >= iFirst
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				UINT count = iLast - iFirst + 1;
				for (UINT i = 0; i < count; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						lpBuffer[i] *= sg_fFontSpacingScale;
					}
					ApplyMetricsOffsetToCharWidthFloat(&lpBuffer[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidthFloatW(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
		{
			LogFontHookHit(L"GetCharWidthFloatW");
			__try { return newGetCharWidthFloatW_SehImpl(hdc, iFirst, iLast, lpBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidthFloatW(hdc, iFirst, iLast, lpBuffer); }
		}


		BOOL WINAPI newGetCharWidthI_SehImpl(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT piWidths)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharWidthI(hdc, giFirst, cgi, pgi, piWidths);
			RestoreHdcFont(hdc, hOld, hNew);
			if (ret && piWidths != nullptr && cgi > 0
				&& (sg_fFontSpacingScale != 1.0f || sg_iMetricsOffsetLeft != 0 || sg_iMetricsOffsetRight != 0))
			{
				for (UINT i = 0; i < cgi; ++i)
				{
					if (sg_fFontSpacingScale != 1.0f)
					{
						piWidths[i] = (INT)(piWidths[i] * sg_fFontSpacingScale);
					}
					ApplyMetricsOffsetToCharWidth(&piWidths[i]);
				}
			}
			return ret;
		}
		BOOL WINAPI newGetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT piWidths)
		{
			LogFontHookHit(L"GetCharWidthI");
			__try { return newGetCharWidthI_SehImpl(hdc, giFirst, cgi, pgi, piWidths); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharWidthI(hdc, giFirst, cgi, pgi, piWidths); }
		}


		BOOL WINAPI newGetCharABCWidthsI_SehImpl(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPABC lpabc)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsI(hdc, giFirst, cgi, pgi, lpabc);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		BOOL WINAPI newGetCharABCWidthsI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPABC lpabc)
		{
			LogFontHookHit(L"GetCharABCWidthsI");
			__try { return newGetCharABCWidthsI_SehImpl(hdc, giFirst, cgi, pgi, lpabc); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetCharABCWidthsI(hdc, giFirst, cgi, pgi, lpabc); }
		}


		BOOL WINAPI newGetTextExtentPointI_SehImpl(HDC hdc, LPWORD pgiIn, int cgi, LPSIZE pSize)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextExtentPointI(hdc, pgiIn, cgi, pSize);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		BOOL WINAPI newGetTextExtentPointI(HDC hdc, LPWORD pgiIn, int cgi, LPSIZE pSize)
		{
			LogFontHookHit(L"GetTextExtentPointI");
			__try { return newGetTextExtentPointI_SehImpl(hdc, pgiIn, cgi, pSize); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextExtentPointI(hdc, pgiIn, cgi, pSize); }
		}


		BOOL WINAPI newGetTextExtentExPointI_SehImpl(HDC hdc, LPWORD lpwszString, int cwchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextExtentExPointI(hdc, lpwszString, cwchString, nMaxExtent, lpnFit, lpnDx, lpSize);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		BOOL WINAPI newGetTextExtentExPointI(HDC hdc, LPWORD lpwszString, int cwchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
		{
			LogFontHookHit(L"GetTextExtentExPointI");
			__try { return newGetTextExtentExPointI_SehImpl(hdc, lpwszString, cwchString, nMaxExtent, lpnFit, lpnDx, lpSize); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetTextExtentExPointI(hdc, lpwszString, cwchString, nMaxExtent, lpnFit, lpnDx, lpSize); }
		}


		DWORD WINAPI newGetFontData_SehImpl(HDC hdc, DWORD dwTable, DWORD dwOffset, PVOID pvBuffer, DWORD cjBuffer)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontData(hdc, dwTable, dwOffset, pvBuffer, cjBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		DWORD WINAPI newGetFontData(HDC hdc, DWORD dwTable, DWORD dwOffset, PVOID pvBuffer, DWORD cjBuffer)
		{
			LogFontHookHit(L"GetFontData");
			__try { return newGetFontData_SehImpl(hdc, dwTable, dwOffset, pvBuffer, cjBuffer); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetFontData(hdc, dwTable, dwOffset, pvBuffer, cjBuffer); }
		}


		DWORD WINAPI newGetFontLanguageInfo_SehImpl(HDC hdc)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontLanguageInfo(hdc);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		DWORD WINAPI newGetFontLanguageInfo(HDC hdc)
		{
			LogFontHookHit(L"GetFontLanguageInfo");
			__try { return newGetFontLanguageInfo_SehImpl(hdc); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetFontLanguageInfo(hdc); }
		}


		DWORD WINAPI newGetFontUnicodeRanges_SehImpl(HDC hdc, LPGLYPHSET lpgs)
{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontUnicodeRanges(hdc, lpgs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}
		DWORD WINAPI newGetFontUnicodeRanges(HDC hdc, LPGLYPHSET lpgs)
		{
			LogFontHookHit(L"GetFontUnicodeRanges");
			__try { return newGetFontUnicodeRanges_SehImpl(hdc, lpgs); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGetFontUnicodeRanges(hdc, lpgs); }
		}


		static void TryHookDWriteTextLayoutMethods(void* textLayout);
		static void TryHookD2DRenderTargetMethods(void* renderTarget);
		static void TryHookD2DDeviceMethods(void* device);

		HRESULT __stdcall newDWriteFactoryCreateTextFormat_SehImpl(void* factory,
			LPCWSTR fontFamilyName,
			void* fontCollection,
			int fontWeight,
			int fontStyle,
			int fontStretch,
			float fontSize,
			LPCWSTR localeName,
			void** textFormat)
{
			if (sg_dwriteHookNesting > 0)
			{
				return rawDWriteFactoryCreateTextFormat(factory, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat);
			}
			DWriteHookNestingScope dwriteScope;
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			LPCWSTR useName = TryGetForcedFontNameWForRequest(fontFamilyName, forcedFaceName, skipOverride) ? forcedFaceName : fontFamilyName;
			bool applyScale = ShouldApplyFloatSizeScale() && !sg_runtimeFloatScaleActive;
			RuntimeFloatScaleScope scaleScope(applyScale);
			float useFontSize = applyScale ? (fontSize * sg_fFontScale) : fontSize;
			return rawDWriteFactoryCreateTextFormat(factory, useName, fontCollection, fontWeight, fontStyle, fontStretch, useFontSize, localeName, textFormat);
		}
		HRESULT __stdcall newDWriteFactoryCreateTextFormat(void* factory,
			LPCWSTR fontFamilyName,
			void* fontCollection,
			int fontWeight,
			int fontStyle,
			int fontStretch,
			float fontSize,
			LPCWSTR localeName,
			void** textFormat)
		{
			if (sg_dwriteHookNesting > 0)
			{
				return rawDWriteFactoryCreateTextFormat(factory, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat);
			}
			__try { return newDWriteFactoryCreateTextFormat_SehImpl(factory, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteFactoryCreateTextFormat(factory, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat); }
		}


		HRESULT __stdcall newDWriteFactoryCreateTextLayout_SehImpl(void* factory,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				float maxWidth,
				float maxHeight,
				void** textLayout)
			{
				return rawDWriteFactoryCreateTextLayout(factory, string, stringLength, textFormat, maxWidth, maxHeight, textLayout);
			}
			HRESULT __stdcall newDWriteFactoryCreateTextLayout(void* factory,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				float maxWidth,
				float maxHeight,
				void** textLayout)
			{
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteFactoryCreateTextLayout(factory, string, stringLength, textFormat, maxWidth, maxHeight, textLayout);
				}
				__try { return newDWriteFactoryCreateTextLayout_SehImpl(factory, string, stringLength, textFormat, maxWidth, maxHeight, textLayout); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteFactoryCreateTextLayout(factory, string, stringLength, textFormat, maxWidth, maxHeight, textLayout); }
			}

			HRESULT __stdcall newDWriteFactoryCreateGdiCompatibleTextLayout_SehImpl(void* factory,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				float layoutWidth,
				float layoutHeight,
				float pixelsPerDip,
				const void* transform,
				BOOL useGdiNatural,
				void** textLayout)
			{
				return rawDWriteFactoryCreateGdiCompatibleTextLayout(factory, string, stringLength, textFormat, layoutWidth, layoutHeight, pixelsPerDip, transform, useGdiNatural, textLayout);
			}
			HRESULT __stdcall newDWriteFactoryCreateGdiCompatibleTextLayout(void* factory,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				float layoutWidth,
				float layoutHeight,
				float pixelsPerDip,
				const void* transform,
				BOOL useGdiNatural,
				void** textLayout)
			{
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteFactoryCreateGdiCompatibleTextLayout(factory, string, stringLength, textFormat, layoutWidth, layoutHeight, pixelsPerDip, transform, useGdiNatural, textLayout);
				}
				__try { return newDWriteFactoryCreateGdiCompatibleTextLayout_SehImpl(factory, string, stringLength, textFormat, layoutWidth, layoutHeight, pixelsPerDip, transform, useGdiNatural, textLayout); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteFactoryCreateGdiCompatibleTextLayout(factory, string, stringLength, textFormat, layoutWidth, layoutHeight, pixelsPerDip, transform, useGdiNatural, textLayout); }
			}

			HRESULT __stdcall newDWriteTextLayoutSetFontFamilyName_SehImpl(void* textLayout, LPCWSTR fontFamilyName, DWRITE_TEXT_RANGE textRange)
			{
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteTextLayoutSetFontFamilyName(textLayout, fontFamilyName, textRange);
				}
				DWriteHookNestingScope dwriteScope;
				bool skipOverride = false;
				wchar_t forcedFaceName[LF_FACESIZE] = {};
				LPCWSTR useName = TryGetForcedFontNameWForRequest(fontFamilyName, forcedFaceName, skipOverride) ? forcedFaceName : fontFamilyName;
				return rawDWriteTextLayoutSetFontFamilyName(textLayout, useName, textRange);
			}
			HRESULT __stdcall newDWriteTextLayoutSetFontFamilyName(void* textLayout, LPCWSTR fontFamilyName, DWRITE_TEXT_RANGE textRange)
			{
				LogFontHookHit(L"DWriteTextLayoutSetFontFamilyName");
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteTextLayoutSetFontFamilyName(textLayout, fontFamilyName, textRange);
				}
				__try { return newDWriteTextLayoutSetFontFamilyName_SehImpl(textLayout, fontFamilyName, textRange); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteTextLayoutSetFontFamilyName(textLayout, fontFamilyName, textRange); }
			}

			HRESULT __stdcall newDWriteTextLayoutSetFontSize_SehImpl(void* textLayout, FLOAT fontSize, DWRITE_TEXT_RANGE textRange)
			{
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteTextLayoutSetFontSize(textLayout, fontSize, textRange);
				}
				DWriteHookNestingScope dwriteScope;
				bool applyScale = ShouldApplyFloatSizeScale() && !sg_runtimeFloatScaleActive;
				RuntimeFloatScaleScope scaleScope(applyScale);
				FLOAT useFontSize = applyScale ? (fontSize * sg_fFontScale) : fontSize;
				return rawDWriteTextLayoutSetFontSize(textLayout, useFontSize, textRange);
			}
			HRESULT __stdcall newDWriteTextLayoutSetFontSize(void* textLayout, FLOAT fontSize, DWRITE_TEXT_RANGE textRange)
			{
				LogFontHookHit(L"DWriteTextLayoutSetFontSize");
				if (sg_dwriteHookNesting > 0)
				{
					return rawDWriteTextLayoutSetFontSize(textLayout, fontSize, textRange);
				}
				__try { return newDWriteTextLayoutSetFontSize_SehImpl(textLayout, fontSize, textRange); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteTextLayoutSetFontSize(textLayout, fontSize, textRange); }
			}

			static void TryHookDWriteTextLayoutMethods(void* textLayout)
			{
				if (!textLayout)
				{
					return;
				}
				ScopedDetourErrorDialogSuppression suppressDetourErrorDialog;
				void** vtable = *(void***)textLayout;
				if (!vtable)
				{
					return;
				}
				if (!sg_hookedDWriteTextLayoutSetFontFamilyName)
				{
					rawDWriteTextLayoutSetFontFamilyName = (pDWriteTextLayoutSetFontFamilyName)vtable[31];
					if (rawDWriteTextLayoutSetFontFamilyName)
					{
						bool failed = !TryDetourAttach(&rawDWriteTextLayoutSetFontFamilyName, newDWriteTextLayoutSetFontFamilyName);
						sg_hookedDWriteTextLayoutSetFontFamilyName = !failed;
					}
				}
				if (!sg_hookedDWriteTextLayoutSetFontSize)
				{
					rawDWriteTextLayoutSetFontSize = (pDWriteTextLayoutSetFontSize)vtable[35];
					if (rawDWriteTextLayoutSetFontSize)
					{
						bool failed = !TryDetourAttach(&rawDWriteTextLayoutSetFontSize, newDWriteTextLayoutSetFontSize);
						sg_hookedDWriteTextLayoutSetFontSize = !failed;
					}
				}
			}

			static void TryHookDWriteFactoryMethods(IUnknown* factory)
		{
			if (!factory)
			{
				return;
			}
			ScopedDetourErrorDialogSuppression suppressDetourErrorDialog;
			void** vtable = *(void***)factory;
			if (!vtable)
			{
				return;
			}
			if (!sg_hookedDWriteCreateTextFormat)
			{
				pDWriteFactoryCreateTextFormat target = (pDWriteFactoryCreateTextFormat)vtable[15];
				if (target)
				{
					rawDWriteFactoryCreateTextFormat = target;
					bool failed = !TryDetourAttach(&rawDWriteFactoryCreateTextFormat, newDWriteFactoryCreateTextFormat);
					sg_hookedDWriteCreateTextFormat = !failed;
				}
			}
			if (!sg_hookedDWriteCreateTextLayout)
			{
				pDWriteFactoryCreateTextLayout layoutTarget = (pDWriteFactoryCreateTextLayout)vtable[12];
				if (layoutTarget)
				{
					rawDWriteFactoryCreateTextLayout = layoutTarget;
					bool layoutFailed = !TryDetourAttach(&rawDWriteFactoryCreateTextLayout, newDWriteFactoryCreateTextLayout);
					sg_hookedDWriteCreateTextLayout = !layoutFailed;
				}
			}
			if (!sg_hookedDWriteCreateGdiCompatibleTextLayout)
			{
				pDWriteFactoryCreateGdiCompatibleTextLayout gdiLayoutTarget = (pDWriteFactoryCreateGdiCompatibleTextLayout)vtable[13];
				if (gdiLayoutTarget)
				{
					rawDWriteFactoryCreateGdiCompatibleTextLayout = gdiLayoutTarget;
					bool gdiLayoutFailed = !TryDetourAttach(&rawDWriteFactoryCreateGdiCompatibleTextLayout, newDWriteFactoryCreateGdiCompatibleTextLayout);
					sg_hookedDWriteCreateGdiCompatibleTextLayout = !gdiLayoutFailed;
				}
			}
		}

		void __stdcall newD2D1RenderTargetDrawText(void* renderTarget,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				const D2D1_RECT_F* layoutRect,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options,
				DWRITE_MEASURING_MODE measuringMode);
			void __stdcall newD2D1RenderTargetDrawTextLayout(void* renderTarget,
				D2D1_POINT_2F origin,
				void* textLayout,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options);
			HRESULT __stdcall newD2D1DeviceCreateDeviceContext(void* device, D2D1_DEVICE_CONTEXT_OPTIONS options, void** deviceContext);

			static void TryHookD2DDeviceMethods(void* device)
			{
				if (!device)
				{
					return;
				}
				ScopedDetourErrorDialogSuppression suppressDetourErrorDialog;
				void** vtable = *(void***)device;
				if (!vtable)
				{
					return;
				}
				if (!sg_hookedD2D1DeviceCreateDeviceContext)
				{
					rawD2D1DeviceCreateDeviceContext = (pD2D1DeviceCreateDeviceContext)vtable[4];
					if (rawD2D1DeviceCreateDeviceContext)
					{
						bool failed = !TryDetourAttach(&rawD2D1DeviceCreateDeviceContext, newD2D1DeviceCreateDeviceContext);
						sg_hookedD2D1DeviceCreateDeviceContext = !failed;
					}
				}
			}

			static void TryHookD2DRenderTargetMethods(void* renderTarget)
			{
				if (!renderTarget)
				{
					return;
				}
				void** vtable = *(void***)renderTarget;
				if (!vtable)
				{
					return;
				}
				if (!sg_hookedD2D1RenderTargetDrawText)
				{
					rawD2D1RenderTargetDrawText = (pD2D1RenderTargetDrawText)vtable[27];
					if (rawD2D1RenderTargetDrawText)
					{
						bool failed = !TryDetourAttach(&rawD2D1RenderTargetDrawText, newD2D1RenderTargetDrawText);
						sg_hookedD2D1RenderTargetDrawText = !failed;
					}
				}
				if (!sg_hookedD2D1RenderTargetDrawTextLayout)
				{
					rawD2D1RenderTargetDrawTextLayout = (pD2D1RenderTargetDrawTextLayout)vtable[28];
					if (rawD2D1RenderTargetDrawTextLayout)
					{
						bool failed = !TryDetourAttach(&rawD2D1RenderTargetDrawTextLayout, newD2D1RenderTargetDrawTextLayout);
						sg_hookedD2D1RenderTargetDrawTextLayout = !failed;
					}
				}
			}

			HRESULT __stdcall newD2D1RenderTargetDrawText_SehImpl(void* renderTarget,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				const D2D1_RECT_F* layoutRect,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options,
				DWRITE_MEASURING_MODE measuringMode)
			{
				if (!string || stringLength == 0)
				{
					rawD2D1RenderTargetDrawText(renderTarget, string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode);
					return S_OK;
				}
				std::wstring mapped = ProcessGlyphStageTextW(string, static_cast<int>(stringLength));
				if (mapped.size() == stringLength && wmemcmp(mapped.data(), string, stringLength) == 0)
				{
					rawD2D1RenderTargetDrawText(renderTarget, string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode);
					return S_OK;
				}
				rawD2D1RenderTargetDrawText(renderTarget, mapped.c_str(), static_cast<UINT32>(mapped.size()), textFormat, layoutRect, defaultFillBrush, options, measuringMode);
				return S_OK;
			}
			void __stdcall newD2D1RenderTargetDrawText(void* renderTarget,
				const WCHAR* string,
				UINT32 stringLength,
				void* textFormat,
				const D2D1_RECT_F* layoutRect,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options,
				DWRITE_MEASURING_MODE measuringMode)
			{
				__try { (void)newD2D1RenderTargetDrawText_SehImpl(renderTarget, string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode); }
				__except(EXCEPTION_EXECUTE_HANDLER) { rawD2D1RenderTargetDrawText(renderTarget, string, stringLength, textFormat, layoutRect, defaultFillBrush, options, measuringMode); }
			}

			void __stdcall newD2D1RenderTargetDrawTextLayout_SehImpl(void* renderTarget,
				D2D1_POINT_2F origin,
				void* textLayout,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options)
			{
				TryHookDWriteTextLayoutMethods(textLayout);
				rawD2D1RenderTargetDrawTextLayout(renderTarget, origin, textLayout, defaultFillBrush, options);
			}
			void __stdcall newD2D1RenderTargetDrawTextLayout(void* renderTarget,
				D2D1_POINT_2F origin,
				void* textLayout,
				void* defaultFillBrush,
				D2D1_DRAW_TEXT_OPTIONS options)
			{
				__try { newD2D1RenderTargetDrawTextLayout_SehImpl(renderTarget, origin, textLayout, defaultFillBrush, options); }
				__except(EXCEPTION_EXECUTE_HANDLER) { rawD2D1RenderTargetDrawTextLayout(renderTarget, origin, textLayout, defaultFillBrush, options); }
			}

			HRESULT __stdcall newD2D1DeviceCreateDeviceContext_SehImpl(void* device, D2D1_DEVICE_CONTEXT_OPTIONS options, void** deviceContext)
			{
				HRESULT hr = rawD2D1DeviceCreateDeviceContext(device, options, deviceContext);
				if (SUCCEEDED(hr) && deviceContext && *deviceContext)
				{
					TryHookD2DRenderTargetMethods(*deviceContext);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1DeviceCreateDeviceContext(void* device, D2D1_DEVICE_CONTEXT_OPTIONS options, void** deviceContext)
			{
				LogFontHookHit(L"D2D1DeviceCreateDeviceContext");
				__try { return newD2D1DeviceCreateDeviceContext_SehImpl(device, options, deviceContext); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1DeviceCreateDeviceContext(device, options, deviceContext); }
			}

			HRESULT __stdcall newD2D1Factory1CreateDevice_SehImpl(void* factory, IDXGIDevice* dxgiDevice, void** d2dDevice)
			{
				HRESULT hr = rawD2D1Factory1CreateDevice(factory, dxgiDevice, d2dDevice);
				if (SUCCEEDED(hr) && d2dDevice && *d2dDevice)
				{
					TryHookD2DDeviceMethods(*d2dDevice);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1Factory1CreateDevice(void* factory, IDXGIDevice* dxgiDevice, void** d2dDevice)
			{
				LogFontHookHit(L"D2D1Factory1CreateDevice");
				__try { return newD2D1Factory1CreateDevice_SehImpl(factory, dxgiDevice, d2dDevice); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1Factory1CreateDevice(factory, dxgiDevice, d2dDevice); }
			}

			HRESULT __stdcall newD2D1FactoryCreateHwndRenderTarget_SehImpl(void* factory, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, const D2D1_HWND_RENDER_TARGET_PROPERTIES* hwndRenderTargetProperties, void** hwndRenderTarget)
			{
				HRESULT hr = rawD2D1FactoryCreateHwndRenderTarget(factory, renderTargetProperties, hwndRenderTargetProperties, hwndRenderTarget);
				if (SUCCEEDED(hr) && hwndRenderTarget && *hwndRenderTarget)
				{
					TryHookD2DRenderTargetMethods(*hwndRenderTarget);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1FactoryCreateHwndRenderTarget(void* factory, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, const D2D1_HWND_RENDER_TARGET_PROPERTIES* hwndRenderTargetProperties, void** hwndRenderTarget)
			{
				LogFontHookHit(L"D2D1FactoryCreateHwndRenderTarget");
				__try { return newD2D1FactoryCreateHwndRenderTarget_SehImpl(factory, renderTargetProperties, hwndRenderTargetProperties, hwndRenderTarget); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1FactoryCreateHwndRenderTarget(factory, renderTargetProperties, hwndRenderTargetProperties, hwndRenderTarget); }
			}

			HRESULT __stdcall newD2D1FactoryCreateDxgiSurfaceRenderTarget_SehImpl(void* factory, IDXGISurface* dxgiSurface, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** renderTarget)
			{
				HRESULT hr = rawD2D1FactoryCreateDxgiSurfaceRenderTarget(factory, dxgiSurface, renderTargetProperties, renderTarget);
				if (SUCCEEDED(hr) && renderTarget && *renderTarget)
				{
					TryHookD2DRenderTargetMethods(*renderTarget);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1FactoryCreateDxgiSurfaceRenderTarget(void* factory, IDXGISurface* dxgiSurface, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** renderTarget)
			{
				LogFontHookHit(L"D2D1FactoryCreateDxgiSurfaceRenderTarget");
				__try { return newD2D1FactoryCreateDxgiSurfaceRenderTarget_SehImpl(factory, dxgiSurface, renderTargetProperties, renderTarget); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1FactoryCreateDxgiSurfaceRenderTarget(factory, dxgiSurface, renderTargetProperties, renderTarget); }
			}

			HRESULT __stdcall newD2D1FactoryCreateDCRenderTarget_SehImpl(void* factory, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** dcRenderTarget)
			{
				HRESULT hr = rawD2D1FactoryCreateDCRenderTarget(factory, renderTargetProperties, dcRenderTarget);
				if (SUCCEEDED(hr) && dcRenderTarget && *dcRenderTarget)
				{
					TryHookD2DRenderTargetMethods(*dcRenderTarget);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1FactoryCreateDCRenderTarget(void* factory, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** dcRenderTarget)
			{
				LogFontHookHit(L"D2D1FactoryCreateDCRenderTarget");
				__try { return newD2D1FactoryCreateDCRenderTarget_SehImpl(factory, renderTargetProperties, dcRenderTarget); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1FactoryCreateDCRenderTarget(factory, renderTargetProperties, dcRenderTarget); }
			}

			HRESULT __stdcall newD2D1FactoryCreateWicBitmapRenderTarget_SehImpl(void* factory, IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** renderTarget)
			{
				HRESULT hr = rawD2D1FactoryCreateWicBitmapRenderTarget(factory, target, renderTargetProperties, renderTarget);
				if (SUCCEEDED(hr) && renderTarget && *renderTarget)
				{
					TryHookD2DRenderTargetMethods(*renderTarget);
				}
				return hr;
			}
			HRESULT __stdcall newD2D1FactoryCreateWicBitmapRenderTarget(void* factory, IWICBitmap* target, const D2D1_RENDER_TARGET_PROPERTIES* renderTargetProperties, void** renderTarget)
			{
				LogFontHookHit(L"D2D1FactoryCreateWicBitmapRenderTarget");
				__try { return newD2D1FactoryCreateWicBitmapRenderTarget_SehImpl(factory, target, renderTargetProperties, renderTarget); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1FactoryCreateWicBitmapRenderTarget(factory, target, renderTargetProperties, renderTarget); }
			}

			static void TryHookD2DFactoryMethods(IUnknown* factory)
			{
				if (!factory)
				{
					return;
				}
				ScopedDetourErrorDialogSuppression suppressDetourErrorDialog;
				void** vtable = *(void***)factory;
				if (!vtable)
				{
					return;
				}
				if (!sg_hookedD2D1FactoryCreateWicBitmapRenderTarget)
				{
					rawD2D1FactoryCreateWicBitmapRenderTarget = (pD2D1FactoryCreateWicBitmapRenderTarget)vtable[13];
					if (rawD2D1FactoryCreateWicBitmapRenderTarget)
					{
						bool failed = !TryDetourAttach(&rawD2D1FactoryCreateWicBitmapRenderTarget, newD2D1FactoryCreateWicBitmapRenderTarget);
						sg_hookedD2D1FactoryCreateWicBitmapRenderTarget = !failed;
					}
				}
				if (!sg_hookedD2D1FactoryCreateHwndRenderTarget)
				{
					rawD2D1FactoryCreateHwndRenderTarget = (pD2D1FactoryCreateHwndRenderTarget)vtable[14];
					if (rawD2D1FactoryCreateHwndRenderTarget)
					{
						bool failed = !TryDetourAttach(&rawD2D1FactoryCreateHwndRenderTarget, newD2D1FactoryCreateHwndRenderTarget);
						sg_hookedD2D1FactoryCreateHwndRenderTarget = !failed;
					}
				}
				if (!sg_hookedD2D1FactoryCreateDxgiSurfaceRenderTarget)
				{
					rawD2D1FactoryCreateDxgiSurfaceRenderTarget = (pD2D1FactoryCreateDxgiSurfaceRenderTarget)vtable[15];
					if (rawD2D1FactoryCreateDxgiSurfaceRenderTarget)
					{
						bool failed = !TryDetourAttach(&rawD2D1FactoryCreateDxgiSurfaceRenderTarget, newD2D1FactoryCreateDxgiSurfaceRenderTarget);
						sg_hookedD2D1FactoryCreateDxgiSurfaceRenderTarget = !failed;
					}
				}
				if (!sg_hookedD2D1FactoryCreateDCRenderTarget)
				{
					rawD2D1FactoryCreateDCRenderTarget = (pD2D1FactoryCreateDCRenderTarget)vtable[16];
					if (rawD2D1FactoryCreateDCRenderTarget)
					{
						bool failed = !TryDetourAttach(&rawD2D1FactoryCreateDCRenderTarget, newD2D1FactoryCreateDCRenderTarget);
						sg_hookedD2D1FactoryCreateDCRenderTarget = !failed;
					}
				}
				if (!sg_hookedD2D1Factory1CreateDevice)
				{
					ID2D1Factory1* factory1 = nullptr;
					if (SUCCEEDED(factory->QueryInterface(__uuidof(ID2D1Factory1), reinterpret_cast<void**>(&factory1))) && factory1)
					{
						void** vtable1 = *(void***)factory1;
						if (vtable1)
						{
							rawD2D1Factory1CreateDevice = (pD2D1Factory1CreateDevice)vtable1[17];
							if (rawD2D1Factory1CreateDevice)
							{
								bool failed = !TryDetourAttach(&rawD2D1Factory1CreateDevice, newD2D1Factory1CreateDevice);
								sg_hookedD2D1Factory1CreateDevice = !failed;
							}
						}
						factory1->Release();
					}
				}
			}

			HRESULT WINAPI newD2D1CreateFactory_SehImpl(D2D1_FACTORY_TYPE factoryType, REFIID riid, const D2D1_FACTORY_OPTIONS* pFactoryOptions, void** ppIFactory)
			{
				if (!rawD2D1CreateFactory)
				{
					return E_FAIL;
				}
				HRESULT hr = rawD2D1CreateFactory(factoryType, riid, pFactoryOptions, ppIFactory);
				if (SUCCEEDED(hr) && ppIFactory && *ppIFactory)
				{
					TryHookD2DFactoryMethods(reinterpret_cast<IUnknown*>(*ppIFactory));
				}
				return hr;
			}
			HRESULT WINAPI newD2D1CreateFactory(D2D1_FACTORY_TYPE factoryType, REFIID riid, const D2D1_FACTORY_OPTIONS* pFactoryOptions, void** ppIFactory)
			{
				LogFontHookHit(L"D2D1CreateFactory");
				__try { return newD2D1CreateFactory_SehImpl(factoryType, riid, pFactoryOptions, ppIFactory); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1CreateFactory(factoryType, riid, pFactoryOptions, ppIFactory); }
			}

			HRESULT WINAPI newD2D1CreateDevice_SehImpl(IDXGIDevice* dxgiDevice, const D2D1_CREATION_PROPERTIES* creationProperties, void** d2dDevice)
			{
				if (!rawD2D1CreateDevice)
				{
					return E_FAIL;
				}
				HRESULT hr = rawD2D1CreateDevice(dxgiDevice, creationProperties, d2dDevice);
				if (SUCCEEDED(hr) && d2dDevice && *d2dDevice)
				{
					TryHookD2DDeviceMethods(*d2dDevice);
				}
				return hr;
			}
			HRESULT WINAPI newD2D1CreateDevice(IDXGIDevice* dxgiDevice, const D2D1_CREATION_PROPERTIES* creationProperties, void** d2dDevice)
			{
				LogFontHookHit(L"D2D1CreateDevice");
				__try { return newD2D1CreateDevice_SehImpl(dxgiDevice, creationProperties, d2dDevice); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1CreateDevice(dxgiDevice, creationProperties, d2dDevice); }
			}

			HRESULT WINAPI newD2D1CreateDeviceContext_SehImpl(IDXGISurface* dxgiSurface, const D2D1_CREATION_PROPERTIES* creationProperties, void** d2dDeviceContext)
			{
				if (!rawD2D1CreateDeviceContext)
				{
					return E_FAIL;
				}
				HRESULT hr = rawD2D1CreateDeviceContext(dxgiSurface, creationProperties, d2dDeviceContext);
				if (SUCCEEDED(hr) && d2dDeviceContext && *d2dDeviceContext)
				{
					TryHookD2DRenderTargetMethods(*d2dDeviceContext);
				}
				return hr;
			}
			HRESULT WINAPI newD2D1CreateDeviceContext(IDXGISurface* dxgiSurface, const D2D1_CREATION_PROPERTIES* creationProperties, void** d2dDeviceContext)
			{
				LogFontHookHit(L"D2D1CreateDeviceContext");
				__try { return newD2D1CreateDeviceContext_SehImpl(dxgiSurface, creationProperties, d2dDeviceContext); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawD2D1CreateDeviceContext(dxgiSurface, creationProperties, d2dDeviceContext); }
			}

			HRESULT WINAPI newDWriteCreateFactory_SehImpl(UINT factoryType, REFIID iid, IUnknown** factory)
			{
			if (!rawDWriteCreateFactory)
			{
				return E_FAIL;
			}
			HRESULT hr = rawDWriteCreateFactory(factoryType, iid, factory);
			if (SUCCEEDED(hr) && factory && *factory)
			{
				TryHookDWriteFactoryMethods(*factory);
			}
			return hr;
			}
			HRESULT WINAPI newDWriteCreateFactory(UINT factoryType, REFIID iid, IUnknown** factory)
			{
				LogFontHookHit(L"DWriteCreateFactory");
				__try { return newDWriteCreateFactory_SehImpl(factoryType, iid, factory); }
				__except(EXCEPTION_EXECUTE_HANDLER) { return rawDWriteCreateFactory(factoryType, iid, factory); }
			}


		int WINAPI newGdipCreateFontFamilyFromName_SehImpl(LPCWSTR name, void* fontCollection, void** fontFamily)
		{
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			LPCWSTR useName = TryGetForcedFontNameWForRequest(name, forcedFaceName, skipOverride) ? forcedFaceName : name;
			return rawGdipCreateFontFamilyFromName(useName, fontCollection, fontFamily);
		}
		int WINAPI newGdipCreateFontFamilyFromName(LPCWSTR name, void* fontCollection, void** fontFamily)
		{
			LogFontHookHit(L"GdipCreateFontFamilyFromName");
			__try { return newGdipCreateFontFamilyFromName_SehImpl(name, fontCollection, fontFamily); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFontFamilyFromName(name, fontCollection, fontFamily); }
		}


		int WINAPI newGdipCreateFontFromLogfontW_SehImpl(HDC hdc, const LOGFONTW* logfont, void** font)
		{
			LOGFONTW lf = {};
			if (logfont)
			{
				lf = *logfont;
			}
			ApplyOverrideToLogFontW(lf);
			return rawGdipCreateFontFromLogfontW(hdc, &lf, font);
		}
		int WINAPI newGdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW* logfont, void** font)
		{
			LogFontHookHit(L"GdipCreateFontFromLogfontW");
			__try { return newGdipCreateFontFromLogfontW_SehImpl(hdc, logfont, font); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFontFromLogfontW(hdc, logfont, font); }
		}


		int WINAPI newGdipCreateFontFromLogfontA_SehImpl(HDC hdc, const LOGFONTA* logfont, void** font)
		{
			LOGFONTA lf = {};
			if (logfont)
			{
				lf = *logfont;
			}
			ApplyOverrideToLogFontA(lf);
			return rawGdipCreateFontFromLogfontA(hdc, &lf, font);
		}
		int WINAPI newGdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA* logfont, void** font)
		{
			LogFontHookHit(L"GdipCreateFontFromLogfontA");
			__try { return newGdipCreateFontFromLogfontA_SehImpl(hdc, logfont, font); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFontFromLogfontA(hdc, logfont, font); }
		}


		int WINAPI newGdipCreateFontFromHFONT_SehImpl(HDC hdc, HFONT hfont, void** font)
		{
			return rawGdipCreateFontFromHFONT(hdc, hfont, font);
		}
		int WINAPI newGdipCreateFontFromHFONT(HDC hdc, HFONT hfont, void** font)
		{
			LogFontHookHit(L"GdipCreateFontFromHFONT");
			__try { return newGdipCreateFontFromHFONT_SehImpl(hdc, hfont, font); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFontFromHFONT(hdc, hfont, font); }
		}


		int WINAPI newGdipCreateFontFromDC_SehImpl(HDC hdc, void** font)
		{
			return rawGdipCreateFontFromDC(hdc, font);
		}
		int WINAPI newGdipCreateFontFromDC(HDC hdc, void** font)
		{
			LogFontHookHit(L"GdipCreateFontFromDC");
			__try { return newGdipCreateFontFromDC_SehImpl(hdc, font); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFontFromDC(hdc, font); }
		}


		int WINAPI newGdipCreateFont_SehImpl(const void* fontFamily, float emSize, int style, int unit, void** font)
		{
			bool applyScale = ShouldApplyFloatSizeScale() && !sg_runtimeFloatScaleActive;
			RuntimeFloatScaleScope scaleScope(applyScale);
			float useEmSize = applyScale ? (emSize * sg_fFontScale) : emSize;
			return rawGdipCreateFont(fontFamily, useEmSize, style, unit, font);
		}
		int WINAPI newGdipCreateFont(const void* fontFamily, float emSize, int style, int unit, void** font)
		{
			LogFontHookHit(L"GdipCreateFont");
			__try { return newGdipCreateFont_SehImpl(fontFamily, emSize, style, unit, font); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipCreateFont(fontFamily, emSize, style, unit, font); }
		}


		static std::wstring PrepareGdipGlyphStageText(const WCHAR* text, int length)
		{
			if (!text)
			{
				return L"";
			}
			return ProcessGlyphStageTextW(text, length);
		}

		static std::wstring PrepareGdipGlyphStageDriverText(const UINT16* text, int length)
		{
			if (!text)
			{
				return L"";
			}
			return ProcessGlyphStageTextW(reinterpret_cast<const wchar_t*>(text), length);
		}

		static void LogGdipGlyphStageDecision(const wchar_t* apiName, const wchar_t* source, int sourceLength, const std::wstring& mapped)
		{
			static volatile LONG s_gdipChangedLogCount = 0;
			static volatile LONG s_gdipUnchangedLogCount = 0;

			if (!source || sourceLength <= 0)
			{
				return;
			}

			const int previewLimit = 24;
			int sourcePreviewLength = sourceLength > previewLimit ? previewLimit : sourceLength;
			std::wstring sourcePreview(source, sourcePreviewLength);
			if (sourcePreviewLength < sourceLength)
			{
				sourcePreview += L"...";
			}

			int mappedLength = (int)mapped.size();
			int mappedPreviewLength = mappedLength > previewLimit ? previewLimit : mappedLength;
			std::wstring mappedPreview(mapped.c_str(), mappedPreviewLength);
			if (mappedPreviewLength < mappedLength)
			{
				mappedPreview += L"...";
			}

			if (mappedLength == sourceLength && mapped == std::wstring(source, sourceLength))
			{
				LONG logIndex = InterlockedIncrement(&s_gdipUnchangedLogCount);
				if (logIndex <= 10)
				{
					LogMessage(LogLevel::Debug,
						L"GDI+Glyph(%s): unchanged #%ld len=%d text=\"%s\"",
						apiName,
						logIndex,
						sourceLength,
						sourcePreview.c_str());
				}
				return;
			}

			LONG logIndex = InterlockedIncrement(&s_gdipChangedLogCount);
			if (logIndex <= 20)
			{
				LogMessage(LogLevel::Debug,
					L"GDI+Glyph(%s): changed #%ld len=%d src=\"%s\" dst=\"%s\"",
					apiName,
					logIndex,
					sourceLength,
					sourcePreview.c_str(),
					mappedPreview.c_str());
			}
		}

		static bool AreGdipBuffersEqual(const wchar_t* source, int sourceLength, const std::wstring& mapped)
		{
			return source != nullptr
				&& sourceLength > 0
				&& mapped.size() == static_cast<size_t>(sourceLength)
				&& wmemcmp(source, mapped.c_str(), static_cast<size_t>(sourceLength)) == 0;
		}

		struct GdipMappedTextContext
		{
			const wchar_t* apiName;
			const wchar_t* source;
			int length;
			bool driverText;
			std::wstring* mapped;
			int sourceLength;
			bool changed;
		};

		static bool ExecutePrepareMappedGdipText(void* rawContext)
		{
			GdipMappedTextContext* context = reinterpret_cast<GdipMappedTextContext*>(rawContext);
			if (!context || !context->source || !context->mapped)
			{
				return false;
			}

			context->sourceLength = context->length < 0 ? (int)wcslen(context->source) : context->length;
			if (context->sourceLength <= 0)
			{
				context->changed = false;
				return true;
			}

			*context->mapped = context->driverText
				? PrepareGdipGlyphStageDriverText(reinterpret_cast<const UINT16*>(context->source), context->length)
				: PrepareGdipGlyphStageText(context->source, context->length);
			LogGdipGlyphStageDecision(context->apiName, context->source, context->sourceLength, *context->mapped);
			context->changed = !context->mapped->empty() && !AreGdipBuffersEqual(context->source, context->sourceLength, *context->mapped);
			return true;
		}

		static bool InvokeSehGuardedBool(bool (*callback)(void*), void* context)
		{
			__try
			{
				return callback != nullptr && callback(context);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		static bool TryPrepareMappedGdipText(const wchar_t* apiName, const wchar_t* source, int length, bool driverText, std::wstring& mapped, int& sourceLength)
		{
			mapped.clear();
			sourceLength = 0;
			if (!IsCnJpMapEnabled() || !source)
			{
				return false;
			}

			GdipMappedTextContext context =
			{
				apiName,
				source,
				length,
				driverText,
				&mapped,
				0,
				false,
			};

			if (!InvokeSehGuardedBool(ExecutePrepareMappedGdipText, &context))
			{
				LogMessage(LogLevel::Warn,
					L"GDI+Glyph(%s): mapping path raised SEH, fallback to raw call",
					apiName ? apiName : L"(unknown)");
				mapped.clear();
				sourceLength = 0;
				return false;
			}

			sourceLength = context.sourceLength;
			return context.changed;
		}

		int WINAPI newGdipDrawString_SehImpl(void* graphics, const WCHAR* string, int length, const void* font, const void* layoutRect, const void* stringFormat, const void* brush)
		{
			std::wstring mapped;
			int sourceLength = 0;
			if (TryPrepareMappedGdipText(L"GdipDrawString", string, length, false, mapped, sourceLength))
			{
				return rawGdipDrawString(graphics, mapped.c_str(), (int)mapped.length(), font, layoutRect, stringFormat, brush);
			}
			return rawGdipDrawString(graphics, string, length, font, layoutRect, stringFormat, brush);
		}
		int WINAPI newGdipDrawString(void* graphics, const WCHAR* string, int length, const void* font, const void* layoutRect, const void* stringFormat, const void* brush)
		{
			LogFontHookHit(L"GdipDrawString");
			__try { return newGdipDrawString_SehImpl(graphics, string, length, font, layoutRect, stringFormat, brush); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipDrawString(graphics, string, length, font, layoutRect, stringFormat, brush); }
		}


		int WINAPI newGdipDrawDriverString_SehImpl(void* graphics, const UINT16* text, int length, const void* font, const void* brush, const void* positions, int flags, const void* matrix)
		{
			std::wstring mapped;
			int sourceLength = 0;
			if (TryPrepareMappedGdipText(L"GdipDrawDriverString", reinterpret_cast<const wchar_t*>(text), length, true, mapped, sourceLength))
			{
				return rawGdipDrawDriverString(graphics, reinterpret_cast<const UINT16*>(mapped.c_str()), (int)mapped.length(), font, brush, positions, flags, matrix);
			}
			return rawGdipDrawDriverString(graphics, text, length, font, brush, positions, flags, matrix);
		}
		int WINAPI newGdipDrawDriverString(void* graphics, const UINT16* text, int length, const void* font, const void* brush, const void* positions, int flags, const void* matrix)
		{
			LogFontHookHit(L"GdipDrawDriverString");
			__try { return newGdipDrawDriverString_SehImpl(graphics, text, length, font, brush, positions, flags, matrix); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipDrawDriverString(graphics, text, length, font, brush, positions, flags, matrix); }
		}


		int WINAPI newGdipMeasureString_SehImpl(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, void* boundingBox, INT* codepointsFitted, INT* linesFilled)
		{
			std::wstring mapped;
			int sourceLength = 0;
			if (TryPrepareMappedGdipText(L"GdipMeasureString", string, length, false, mapped, sourceLength))
			{
				return rawGdipMeasureString(graphics, mapped.c_str(), (INT)mapped.length(), font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled);
			}
			return rawGdipMeasureString(graphics, string, length, font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled);
		}
		int WINAPI newGdipMeasureString(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, void* boundingBox, INT* codepointsFitted, INT* linesFilled)
		{
			LogFontHookHit(L"GdipMeasureString");
			__try { return newGdipMeasureString_SehImpl(graphics, string, length, font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipMeasureString(graphics, string, length, font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled); }
		}


		int WINAPI newGdipMeasureCharacterRanges_SehImpl(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, INT regionCount, void** regions)
		{
			std::wstring mapped;
			int sourceLength = 0;
			if (TryPrepareMappedGdipText(L"GdipMeasureCharacterRanges", string, length, false, mapped, sourceLength))
			{
				return rawGdipMeasureCharacterRanges(graphics, mapped.c_str(), (INT)mapped.length(), font, layoutRect, stringFormat, regionCount, regions);
			}
			return rawGdipMeasureCharacterRanges(graphics, string, length, font, layoutRect, stringFormat, regionCount, regions);
		}
		int WINAPI newGdipMeasureCharacterRanges(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, INT regionCount, void** regions)
		{
			LogFontHookHit(L"GdipMeasureCharacterRanges");
			__try { return newGdipMeasureCharacterRanges_SehImpl(graphics, string, length, font, layoutRect, stringFormat, regionCount, regions); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipMeasureCharacterRanges(graphics, string, length, font, layoutRect, stringFormat, regionCount, regions); }
		}


		int WINAPI newGdipMeasureDriverString_SehImpl(void* graphics, const UINT16* text, INT length, const void* font, const void* positions, INT flags, const void* matrix, void* boundingBox)
		{
			std::wstring mapped;
			int sourceLength = 0;
			if (TryPrepareMappedGdipText(L"GdipMeasureDriverString", reinterpret_cast<const wchar_t*>(text), length, true, mapped, sourceLength))
			{
				return rawGdipMeasureDriverString(graphics, reinterpret_cast<const UINT16*>(mapped.c_str()), (INT)mapped.length(), font, positions, flags, matrix, boundingBox);
			}
			return rawGdipMeasureDriverString(graphics, text, length, font, positions, flags, matrix, boundingBox);
		}
		int WINAPI newGdipMeasureDriverString(void* graphics, const UINT16* text, INT length, const void* font, const void* positions, INT flags, const void* matrix, void* boundingBox)
		{
			LogFontHookHit(L"GdipMeasureDriverString");
			__try { return newGdipMeasureDriverString_SehImpl(graphics, text, length, font, positions, flags, matrix, boundingBox); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawGdipMeasureDriverString(graphics, text, length, font, positions, flags, matrix, boundingBox); }
		}


		static void TryHookLateLoadedModules(const wchar_t* moduleName)
		{
			if (!moduleName)
			{
				return;
			}
			const bool shouldHookDWrite = wcsstr(moduleName, L"dwrite") || wcsstr(moduleName, L"DWRITE");
			const bool shouldHookD2D = wcsstr(moduleName, L"d2d1") || wcsstr(moduleName, L"D2D1");
			const bool shouldHookGdiplus = wcsstr(moduleName, L"gdiplus") || wcsstr(moduleName, L"GDIPLUS");
			const bool shouldHookComdlg = sg_unlockFontSelection && (wcsstr(moduleName, L"comdlg32") || wcsstr(moduleName, L"COMDLG32"));
			if (!shouldHookDWrite && !shouldHookD2D && !shouldHookGdiplus && !shouldHookComdlg)
			{
				return;
			}

			ScopedDetourErrorDialogSuppression suppressDetourErrorDialog;
			const bool detourBatchStarted = BeginDetourBatch();

			if (shouldHookDWrite)
			{
				HookDWriteCreateFactory();
			}
			if (shouldHookD2D)
				{
					HookD2D1CreateFactory();
				}
				if (shouldHookGdiplus)
			{
				HookGdipCreateFontFamilyFromName();
				HookGdipCreateFontFromLogfontW();
				HookGdipCreateFontFromLogfontA();
				HookGdipCreateFontFromHFONT();
				HookGdipCreateFontFromDC();
				HookGdipCreateFont();
				HookGdipDrawString();
				HookGdipDrawDriverString();
				HookGdipMeasureString();
				HookGdipMeasureCharacterRanges();
				HookGdipMeasureDriverString();
			}
			if (shouldHookComdlg)
			{
				HookChooseFontA();
				HookChooseFontW();
			}

			if (detourBatchStarted && !EndDetourBatch(L"Late-loaded font detour batch"))
			{
				LogMessage(LogLevel::Warn, L"TryHookLateLoadedModules: detour batch commit failed for %s", moduleName);
			}
		}

		HMODULE WINAPI newLoadLibraryW_SehImpl(LPCWSTR lpLibFileName)
		{
			HMODULE hModule = rawLoadLibraryW(lpLibFileName);
			if (hModule)
			{
				TryHookLateLoadedModules(lpLibFileName);
			}
			return hModule;
		}
		HMODULE WINAPI newLoadLibraryW(LPCWSTR lpLibFileName)
		{
			LogFontHookHit(L"LoadLibraryW");
			__try { return newLoadLibraryW_SehImpl(lpLibFileName); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawLoadLibraryW(lpLibFileName); }
		}


		HMODULE WINAPI newLoadLibraryExW_SehImpl(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
		{
			HMODULE hModule = rawLoadLibraryExW(lpLibFileName, hFile, dwFlags);
			if (hModule && (dwFlags & LOAD_LIBRARY_AS_DATAFILE) == 0 && (dwFlags & LOAD_LIBRARY_AS_IMAGE_RESOURCE) == 0)
			{
				TryHookLateLoadedModules(lpLibFileName);
			}
			return hModule;
		}
		HMODULE WINAPI newLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
		{
			LogFontHookHit(L"LoadLibraryExW");
			__try { return newLoadLibraryExW_SehImpl(lpLibFileName, hFile, dwFlags); }
			__except(EXCEPTION_EXECUTE_HANDLER) { return rawLoadLibraryExW(lpLibFileName, hFile, dwFlags); }
		}


		bool HookCreateFontIndirectExA()
		{
			return !TryDetourAttach(&rawCreateFontIndirectExA, newCreateFontIndirectExA);
		}

		bool HookCreateFontIndirectExW()
		{
			return !TryDetourAttach(&rawCreateFontIndirectExW, newCreateFontIndirectExW);
		}

		bool HookGetObjectA()
		{
			return !TryDetourAttach(&rawGetObjectA, newGetObjectA);
		}

		bool HookGetObjectW()
		{
			return !TryDetourAttach(&rawGetObjectW, newGetObjectW);
		}

		bool HookGetTextFaceA()
		{
			return !TryDetourAttach(&rawGetTextFaceA, newGetTextFaceA);
		}

		bool HookGetTextFaceW()
		{
			return !TryDetourAttach(&rawGetTextFaceW, newGetTextFaceW);
		}

		bool HookGetTextMetricsA()
		{
			return !TryDetourAttach(&rawGetTextMetricsA, newGetTextMetricsA);
		}

		bool HookGetTextMetricsW()
		{
			return !TryDetourAttach(&rawGetTextMetricsW, newGetTextMetricsW);
		}

		bool HookGetCharABCWidthsA()
		{
			return !TryDetourAttach(&rawGetCharABCWidthsA, newGetCharABCWidthsA);
		}

		bool HookGetCharABCWidthsW()
		{
			return !TryDetourAttach(&rawGetCharABCWidthsW, newGetCharABCWidthsW);
		}

		bool HookGetCharABCWidthsFloatA()
		{
			return !TryDetourAttach(&rawGetCharABCWidthsFloatA, newGetCharABCWidthsFloatA);
		}

		bool HookGetCharABCWidthsFloatW()
		{
			return !TryDetourAttach(&rawGetCharABCWidthsFloatW, newGetCharABCWidthsFloatW);
		}

		bool HookGetCharWidthA()
		{
			return !TryDetourAttach(&rawGetCharWidthA, newGetCharWidthA);
		}

		bool HookGetCharWidthW()
		{
			return !TryDetourAttach(&rawGetCharWidthW, newGetCharWidthW);
		}

		bool HookGetCharWidth32A()
		{
			return !TryDetourAttach(&rawGetCharWidth32A, newGetCharWidth32A);
		}

		bool HookGetCharWidth32W()
		{
			return !TryDetourAttach(&rawGetCharWidth32W, newGetCharWidth32W);
		}

		bool HookGetKerningPairsA()
		{
			return !TryDetourAttach(&rawGetKerningPairsA, newGetKerningPairsA);
		}

		bool HookGetKerningPairsW()
		{
			return !TryDetourAttach(&rawGetKerningPairsW, newGetKerningPairsW);
		}

		bool HookGetOutlineTextMetricsA()
		{
			return !TryDetourAttach(&rawGetOutlineTextMetricsA, newGetOutlineTextMetricsA);
		}

		bool HookGetOutlineTextMetricsW()
		{
			return !TryDetourAttach(&rawGetOutlineTextMetricsW, newGetOutlineTextMetricsW);
		}

		bool HookAddFontResourceA()
		{
			return !TryDetourAttach(&rawAddFontResourceA, newAddFontResourceA);
		}

		bool HookAddFontResourceW()
		{
			return !TryDetourAttach(&rawAddFontResourceW, newAddFontResourceW);
		}

		bool HookAddFontResourceExA()
		{
			return !TryDetourAttach(&rawAddFontResourceExA, newAddFontResourceExA);
		}

		bool HookAddFontMemResourceEx()
		{
			return !TryDetourAttach(&rawAddFontMemResourceEx, newAddFontMemResourceEx);
		}

		bool HookRemoveFontResourceA()
		{
			return !TryDetourAttach(&rawRemoveFontResourceA, newRemoveFontResourceA);
		}

		bool HookRemoveFontResourceW()
		{
			return !TryDetourAttach(&rawRemoveFontResourceW, newRemoveFontResourceW);
		}

		bool HookRemoveFontResourceExA()
		{
			return !TryDetourAttach(&rawRemoveFontResourceExA, newRemoveFontResourceExA);
		}

		bool HookRemoveFontMemResourceEx()
		{
			return !TryDetourAttach(&rawRemoveFontMemResourceEx, newRemoveFontMemResourceEx);
		}

		bool HookEnumFontsA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontsA, newEnumFontsA);
		}

		bool HookEnumFontsW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontsW, newEnumFontsW);
		}

		bool HookEnumFontFamiliesA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontFamiliesA, newEnumFontFamiliesA);
		}

		bool HookEnumFontFamiliesW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return !TryDetourAttach(&rawEnumFontFamiliesW, newEnumFontFamiliesW);
		}

		bool HookChooseFontA()
		{
			if (sg_hookedChooseFontA)
			{
				return false;
			}
			HMODULE hComdlg = GetModuleHandleW(L"comdlg32.dll");
			if (!hComdlg)
			{
				return false;
			}
			rawChooseFontA = (pChooseFontA)GetProcAddress(hComdlg, "ChooseFontA");
			if (!rawChooseFontA)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawChooseFontA, newChooseFontA);
			sg_hookedChooseFontA = !failed;
			return failed;
		}

		bool HookChooseFontW()
		{
			if (sg_hookedChooseFontW)
			{
				return false;
			}
			HMODULE hComdlg = GetModuleHandleW(L"comdlg32.dll");
			if (!hComdlg)
			{
				return false;
			}
			rawChooseFontW = (pChooseFontW)GetProcAddress(hComdlg, "ChooseFontW");
			if (!rawChooseFontW)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawChooseFontW, newChooseFontW);
			sg_hookedChooseFontW = !failed;
			return failed;
		}

		bool HookGetCharWidthFloatA()
		{
			return !TryDetourAttach(&rawGetCharWidthFloatA, newGetCharWidthFloatA);
		}

		bool HookGetCharWidthFloatW()
		{
			return !TryDetourAttach(&rawGetCharWidthFloatW, newGetCharWidthFloatW);
		}

		bool HookGetCharWidthI()
		{
			return !TryDetourAttach(&rawGetCharWidthI, newGetCharWidthI);
		}

		bool HookGetCharABCWidthsI()
		{
			return !TryDetourAttach(&rawGetCharABCWidthsI, newGetCharABCWidthsI);
		}

		bool HookGetTextExtentPointI()
		{
			return !TryDetourAttach(&rawGetTextExtentPointI, newGetTextExtentPointI);
		}

		bool HookGetTextExtentExPointI()
		{
			return !TryDetourAttach(&rawGetTextExtentExPointI, newGetTextExtentExPointI);
		}

		bool HookGetFontData()
		{
			return !TryDetourAttach(&rawGetFontData, newGetFontData);
		}

		bool HookGetFontLanguageInfo()
		{
			return !TryDetourAttach(&rawGetFontLanguageInfo, newGetFontLanguageInfo);
		}

		bool HookGetFontUnicodeRanges()
		{
			return !TryDetourAttach(&rawGetFontUnicodeRanges, newGetFontUnicodeRanges);
		}

		bool HookDWriteCreateFactory()
		{
			if (sg_hookedDWriteCreateFactory)
			{
				return false;
			}
			HMODULE hDWrite = GetModuleHandleW(L"dwrite.dll");
			if (!hDWrite)
			{
				return false;
			}
			rawDWriteCreateFactory = (pDWriteCreateFactory)GetProcAddress(hDWrite, "DWriteCreateFactory");
			if (!rawDWriteCreateFactory)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawDWriteCreateFactory, newDWriteCreateFactory);
			sg_hookedDWriteCreateFactory = !failed;
			return failed;
		}

		bool HookD2D1CreateFactory()
			{
				if (sg_hookedD2D1CreateFactory && sg_hookedD2D1CreateDevice && sg_hookedD2D1CreateDeviceContext)
				{
					return false;
				}
				HMODULE hD2D = GetModuleHandleW(L"d2d1.dll");
				if (!hD2D)
				{
					return false;
				}
				bool failed = false;
				if (!sg_hookedD2D1CreateFactory)
				{
					rawD2D1CreateFactory = (pD2D1CreateFactory)GetProcAddress(hD2D, "D2D1CreateFactory");
					if (rawD2D1CreateFactory)
					{
						bool attachFailed = !TryDetourAttach(&rawD2D1CreateFactory, newD2D1CreateFactory);
						sg_hookedD2D1CreateFactory = !attachFailed;
						failed = failed || attachFailed;
					}
				}
				if (!sg_hookedD2D1CreateDevice)
				{
					rawD2D1CreateDevice = (pD2D1CreateDevice)GetProcAddress(hD2D, "D2D1CreateDevice");
					if (rawD2D1CreateDevice)
					{
						bool attachFailed = !TryDetourAttach(&rawD2D1CreateDevice, newD2D1CreateDevice);
						sg_hookedD2D1CreateDevice = !attachFailed;
						failed = failed || attachFailed;
					}
				}
				if (!sg_hookedD2D1CreateDeviceContext)
				{
					rawD2D1CreateDeviceContext = (pD2D1CreateDeviceContext)GetProcAddress(hD2D, "D2D1CreateDeviceContext");
					if (rawD2D1CreateDeviceContext)
					{
						bool attachFailed = !TryDetourAttach(&rawD2D1CreateDeviceContext, newD2D1CreateDeviceContext);
						sg_hookedD2D1CreateDeviceContext = !attachFailed;
						failed = failed || attachFailed;
					}
				}
				return failed;
			}

			bool HookGdipCreateFontFamilyFromName()
		{
			if (sg_hookedGdipCreateFontFamily)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFontFamilyFromName = (pGdipCreateFontFamilyFromName)GetProcAddress(hGdiplus, "GdipCreateFontFamilyFromName");
			if (!rawGdipCreateFontFamilyFromName)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFontFamilyFromName, newGdipCreateFontFamilyFromName);
			sg_hookedGdipCreateFontFamily = !failed;
			return failed;
		}

		bool HookGdipCreateFontFromLogfontW()
		{
			if (sg_hookedGdipCreateFontFromLogfontW)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFontFromLogfontW = (pGdipCreateFontFromLogfontW)GetProcAddress(hGdiplus, "GdipCreateFontFromLogfontW");
			if (!rawGdipCreateFontFromLogfontW)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFontFromLogfontW, newGdipCreateFontFromLogfontW);
			sg_hookedGdipCreateFontFromLogfontW = !failed;
			return failed;
		}

		bool HookGdipCreateFontFromLogfontA()
		{
			if (sg_hookedGdipCreateFontFromLogfontA)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFontFromLogfontA = (pGdipCreateFontFromLogfontA)GetProcAddress(hGdiplus, "GdipCreateFontFromLogfontA");
			if (!rawGdipCreateFontFromLogfontA)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFontFromLogfontA, newGdipCreateFontFromLogfontA);
			sg_hookedGdipCreateFontFromLogfontA = !failed;
			return failed;
		}

		bool HookGdipCreateFontFromHFONT()
		{
			if (sg_hookedGdipCreateFontFromHFONT)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFontFromHFONT = (pGdipCreateFontFromHFONT)GetProcAddress(hGdiplus, "GdipCreateFontFromHFONT");
			if (!rawGdipCreateFontFromHFONT)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFontFromHFONT, newGdipCreateFontFromHFONT);
			sg_hookedGdipCreateFontFromHFONT = !failed;
			return failed;
		}

		bool HookGdipCreateFontFromDC()
		{
			if (sg_hookedGdipCreateFontFromDC)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFontFromDC = (pGdipCreateFontFromDC)GetProcAddress(hGdiplus, "GdipCreateFontFromDC");
			if (!rawGdipCreateFontFromDC)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFontFromDC, newGdipCreateFontFromDC);
			sg_hookedGdipCreateFontFromDC = !failed;
			return failed;
		}

		bool HookGdipCreateFont()
		{
			if (sg_hookedGdipCreateFont)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipCreateFont = (pGdipCreateFont)GetProcAddress(hGdiplus, "GdipCreateFont");
			if (!rawGdipCreateFont)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipCreateFont, newGdipCreateFont);
			sg_hookedGdipCreateFont = !failed;
			return failed;
		}

		bool HookGdipDrawString()
		{
			if (sg_hookedGdipDrawString)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipDrawString = (pGdipDrawString)GetProcAddress(hGdiplus, "GdipDrawString");
			if (!rawGdipDrawString)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipDrawString, newGdipDrawString);
			sg_hookedGdipDrawString = !failed;
			return failed;
		}

		bool HookGdipDrawDriverString()
		{
			if (sg_hookedGdipDrawDriverString)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipDrawDriverString = (pGdipDrawDriverString)GetProcAddress(hGdiplus, "GdipDrawDriverString");
			if (!rawGdipDrawDriverString)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipDrawDriverString, newGdipDrawDriverString);
			sg_hookedGdipDrawDriverString = !failed;
			return failed;
		}

		bool HookGdipMeasureString()
		{
			if (sg_hookedGdipMeasureString)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipMeasureString = (pGdipMeasureString)GetProcAddress(hGdiplus, "GdipMeasureString");
			if (!rawGdipMeasureString)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipMeasureString, newGdipMeasureString);
			sg_hookedGdipMeasureString = !failed;
			return failed;
		}

		bool HookGdipMeasureCharacterRanges()
		{
			if (sg_hookedGdipMeasureCharacterRanges)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipMeasureCharacterRanges = (pGdipMeasureCharacterRanges)GetProcAddress(hGdiplus, "GdipMeasureCharacterRanges");
			if (!rawGdipMeasureCharacterRanges)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipMeasureCharacterRanges, newGdipMeasureCharacterRanges);
			sg_hookedGdipMeasureCharacterRanges = !failed;
			return failed;
		}

		bool HookGdipMeasureDriverString()
		{
			if (sg_hookedGdipMeasureDriverString)
			{
				return false;
			}
			HMODULE hGdiplus = GetModuleHandleW(L"gdiplus.dll");
			if (!hGdiplus)
			{
				return false;
			}
			rawGdipMeasureDriverString = (pGdipMeasureDriverString)GetProcAddress(hGdiplus, "GdipMeasureDriverString");
			if (!rawGdipMeasureDriverString)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawGdipMeasureDriverString, newGdipMeasureDriverString);
			sg_hookedGdipMeasureDriverString = !failed;
			return failed;
		}

		bool HookLoadLibraryW()
		{
			if (sg_hookedLoadLibraryW)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawLoadLibraryW, newLoadLibraryW);
			sg_hookedLoadLibraryW = !failed;
			return failed;
		}

		bool HookLoadLibraryExW()
		{
			if (sg_hookedLoadLibraryExW)
			{
				return false;
			}
			bool failed = !TryDetourAttach(&rawLoadLibraryExW, newLoadLibraryExW);
			sg_hookedLoadLibraryExW = !failed;
			return failed;
		}

