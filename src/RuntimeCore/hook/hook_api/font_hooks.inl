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
		static bool TryCopyWideFaceName(const wchar_t* source, wchar_t (&buffer)[LF_FACESIZE])
		{
			buffer[0] = L'\0';
			if (!source)
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
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				buffer[0] = L'\0';
				return false;
			}
			buffer[0] = L'\0';
			return false;
		}
		static std::wstring TrimWideCopy(std::wstring value)
		{
			size_t begin = 0;
			while (begin < value.size() && iswspace(static_cast<wint_t>(value[begin])) != 0)
			{
				++begin;
			}
			size_t end = value.size();
			while (end > begin && iswspace(static_cast<wint_t>(value[end - 1])) != 0)
			{
				--end;
			}
			return value.substr(begin, end - begin);
		}
		static std::wstring NormalizeFontRuleKey(std::wstring value)
		{
			value = TrimWideCopy(std::move(value));
			if (!value.empty())
			{
				CharUpperBuffW(&value[0], (DWORD)value.size());
			}
			return value;
		}
		static std::wstring NormalizeFontRuleKey(const wchar_t* value)
		{
			return NormalizeFontRuleKey(value ? std::wstring(value) : std::wstring());
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
		static bool TryGetForcedFontNameWForRequest(const wchar_t* requestedFaceName, wchar_t (&buffer)[LF_FACESIZE], bool& skipOverride)
		{
			if (TryResolveRedirectFontNameW(requestedFaceName, buffer, skipOverride))
			{
				return true;
			}
			if (skipOverride)
			{
				return false;
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
		static pCreateFontA rawCreateFontA = CreateFontA;
		
		HFONT WINAPI newCreateFontA(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
		{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			FontCreateNestingScope scope;
			bool skipOverride = false;
			std::string redirectFaceName;
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (TryResolveRedirectFontNameW(AnsiToWide(pszFaceName).c_str(), redirectedFaceNameW, skipOverride))
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

		bool HookCreateFontA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontName = cpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return DetourAttachFunc(&rawCreateFontA, newCreateFontA);
		}
		//*********END Hook CreateFontA*********


		//*********Start Hook CreateFontIndirectA*******
		static pCreateFontIndirectA rawCreateFontIndirectA = CreateFontIndirectA;
		HFONT WINAPI newCreateFontIndirectA(LOGFONTA* lplf)
		{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectA(lplf);
			}
			FontCreateNestingScope scope;
			if (!lplf)
			{
				return rawCreateFontIndirectA(lplf);
			}
			bool skipOverride = false;
			std::string forcedFaceName;
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (TryResolveRedirectFontNameW(AnsiToWide(lplf->lfFaceName).c_str(), redirectedFaceNameW, skipOverride))
			{
				forcedFaceName = WideFaceNameToAnsi(redirectedFaceNameW);
			}
			if (skipOverride)
			{
				return rawCreateFontIndirectA(lplf);
			}
			LOGFONTA local = *lplf;
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

		bool HookCreateFontIndirectA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontName = cpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return DetourAttachFunc(&rawCreateFontIndirectA, newCreateFontIndirectA);
		}
		//*********END Hook CreateFontIndirectA*********

		//*********Start Hook CreateFontW*******
		static pCreateFontW rawCreateFontW = CreateFontW;
		HFONT WINAPI newCreateFontW(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
		{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
			}
			FontCreateNestingScope scope;
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			bool hasForcedFaceName = TryGetForcedFontNameWForRequest(pszFaceName, forcedFaceName, skipOverride);
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

		bool HookCreateFontW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontNameW = wpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return DetourAttachFunc(&rawCreateFontW, newCreateFontW);
		}
		//*********END Hook CreateFontW*******


		//*********Start Hook CreateFontIndirectW*******
		static pCreateFontIndirectW rawCreateFontIndirectW = CreateFontIndirectW;
		HFONT WINAPI newCreateFontIndirectW(LOGFONTW* lplf)
		{
			if (sg_fontCreateNesting > 0)
			{
				return rawCreateFontIndirectW(lplf);
			}
			FontCreateNestingScope scope;
			if (!lplf)
			{
				return rawCreateFontIndirectW(lplf);
			}
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			bool hasForcedFaceName = TryGetForcedFontNameWForRequest(lplf->lfFaceName, forcedFaceName, skipOverride);
			if (skipOverride)
			{
				return rawCreateFontIndirectW(lplf);
			}
			LOGFONTW local = *lplf;
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

		bool HookCreateFontIndirectW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight, int iWidth, int iWeight, float fScale, float fSpacingScale, float fGlyphAspectRatio, int iGlyphOffsetX, int iGlyphOffsetY, int iMetricsOffsetLeft, int iMetricsOffsetRight, int iMetricsOffsetTop, int iMetricsOffsetBottom)
		{
			sg_lpFontNameW = wpFontName;
			SetFontAdjustments(uiCharSet, enableCharsetSpoof, spoofFromCharSet, spoofToCharSet, iHeight, iWidth, iWeight, fScale, fSpacingScale, fGlyphAspectRatio, iGlyphOffsetX, iGlyphOffsetY, iMetricsOffsetLeft, iMetricsOffsetRight, iMetricsOffsetTop, iMetricsOffsetBottom);
			return DetourAttachFunc(&rawCreateFontIndirectW, newCreateFontIndirectW);
		}
		//*********END Hook CreateFontIndirectW*********

		static bool sg_unlockFontSelection = false;
		static pEnumFontFamiliesExA rawEnumFontFamiliesExA = EnumFontFamiliesExA;
		static pEnumFontFamiliesExW rawEnumFontFamiliesExW = EnumFontFamiliesExW;
		struct EnumFontDedupContextA
		{
			pFONTENUMPROCA callback = nullptr;
			LPARAM originalLParam = 0;
			std::unordered_map<std::wstring, bool> seenFaceNames;
		};
		struct EnumFontDedupContextW
		{
			pFONTENUMPROCW callback = nullptr;
			LPARAM originalLParam = 0;
			std::unordered_map<std::wstring, bool> seenFaceNames;
		};
		static std::wstring NormalizeEnumFaceNameA(const LOGFONTA* lpelfe)
		{
			if (!lpelfe || lpelfe->lfFaceName[0] == '\0')
			{
				return L"";
			}
			std::wstring faceName = AnsiToWide(lpelfe->lfFaceName);
			if (!faceName.empty())
			{
				CharUpperBuffW(&faceName[0], (DWORD)faceName.size());
			}
			return faceName;
		}
		static std::wstring NormalizeEnumFaceNameW(const LOGFONTW* lpelfe)
		{
			if (!lpelfe || lpelfe->lfFaceName[0] == L'\0')
			{
				return L"";
			}
			std::wstring faceName(lpelfe->lfFaceName);
			CharUpperBuffW(&faceName[0], (DWORD)faceName.size());
			return faceName;
		}
		static int CALLBACK EnumFontDedupProcA(const LOGFONTA* lpelfe, const TEXTMETRICA* lpntme, DWORD fontType, LPARAM lParam)
		{
			EnumFontDedupContextA* context = reinterpret_cast<EnumFontDedupContextA*>(lParam);
			if (!context || !context->callback)
			{
				return 1;
			}
			std::wstring faceName = NormalizeEnumFaceNameA(lpelfe);
			if (!faceName.empty())
			{
				if (context->seenFaceNames.find(faceName) != context->seenFaceNames.end())
				{
					return 1;
				}
				context->seenFaceNames.emplace(std::move(faceName), true);
			}
			return context->callback(lpelfe, lpntme, fontType, context->originalLParam);
		}
		static int CALLBACK EnumFontDedupProcW(const LOGFONTW* lpelfe, const TEXTMETRICW* lpntme, DWORD fontType, LPARAM lParam)
		{
			EnumFontDedupContextW* context = reinterpret_cast<EnumFontDedupContextW*>(lParam);
			if (!context || !context->callback)
			{
				return 1;
			}
			std::wstring faceName = NormalizeEnumFaceNameW(lpelfe);
			if (!faceName.empty())
			{
				if (context->seenFaceNames.find(faceName) != context->seenFaceNames.end())
				{
					return 1;
				}
				context->seenFaceNames.emplace(std::move(faceName), true);
			}
			return context->callback(lpelfe, lpntme, fontType, context->originalLParam);
		}

		int WINAPI newEnumFontFamiliesExA(HDC hdc, LPLOGFONTA lpLogfont, pFONTENUMPROCA lpProc, LPARAM lParam, DWORD dwFlags)
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
			else
			{
				ApplyOverrideToLogFontA(target, false);
			}
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextA context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				return rawEnumFontFamiliesExA(hdc, &target, EnumFontDedupProcA, reinterpret_cast<LPARAM>(&context), dwFlags);
			}
			return rawEnumFontFamiliesExA(hdc, &target, lpProc, lParam, dwFlags);
		}

		int WINAPI newEnumFontFamiliesExW(HDC hdc, LPLOGFONTW lpLogfont, pFONTENUMPROCW lpProc, LPARAM lParam, DWORD dwFlags)
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
			else
			{
				ApplyOverrideToLogFontW(target, false);
			}
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextW context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				return rawEnumFontFamiliesExW(hdc, &target, EnumFontDedupProcW, reinterpret_cast<LPARAM>(&context), dwFlags);
			}
			return rawEnumFontFamiliesExW(hdc, &target, lpProc, lParam, dwFlags);
		}

		bool HookEnumFontFamiliesExA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontFamiliesExA, newEnumFontFamiliesExA);
		}

		bool HookEnumFontFamiliesExW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontFamiliesExW, newEnumFontFamiliesExW);
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
			if (TryResolveRedirectFontNameW(AnsiToWide(lf.lfFaceName).c_str(), redirectedFaceNameW, skipOverride))
			{
				forcedFaceName = WideFaceNameToAnsi(redirectedFaceNameW);
			}
			else if (skipOverride)
			{
				return false;
			}
			else
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
			wchar_t redirectedFaceNameW[LF_FACESIZE] = {};
			if (TryResolveRedirectFontNameW(AnsiToWide(requestedFaceName).c_str(), redirectedFaceNameW, skipOverride))
			{
				return WideFaceNameToAnsi(redirectedFaceNameW);
			}
			if (skipOverride)
			{
				return "";
			}
			return GetForcedFontNameA();
		}
		static HFONT ReplaceHdcFont(HDC hdc, HFONT* pOldFont)
		{
			if (pOldFont)
			{
				*pOldFont = nullptr;
			}
			if (!hdc)
			{
				return nullptr;
			}
			HFONT hCurFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
			if (!hCurFont)
			{
				return nullptr;
			}
			LOGFONTW lf = {};
			if (GetObjectW(hCurFont, sizeof(lf), &lf) == 0)
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
			if (applySizeScale && IsAnyFontSizeOverrideEnabled())
			{
				MarkFontHandleScaled(hNewFont);
			}
			if (pOldFont)
			{
				*pOldFont = (HFONT)rawSelectObject(hdc, hNewFont);
			}
			return hNewFont;
		}

		static void RestoreHdcFont(HDC hdc, HFONT hOldFont, HFONT hNewFont)
		{
			if (!hNewFont)
			{
				return;
			}
			if (hOldFont)
			{
				rawSelectObject(hdc, hOldFont);
			}
			DeleteObject(hNewFont);
		}

		static pGetObjectA rawGetObjectA = GetObjectA;
		static pGetObjectW rawGetObjectW = GetObjectW;
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
		static pDWriteFactoryCreateTextFormat rawDWriteFactoryCreateTextFormat = nullptr;
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

		int WINAPI newGetObjectA(HANDLE h, int c, LPVOID pv)
		{
			int ret = rawGetObjectA(h, c, pv);
			if (ret >= (int)sizeof(LOGFONTA) && pv && c >= (int)sizeof(LOGFONTA) && IsFontObjectHandle(h))
			{
				LOGFONTA* lf = (LOGFONTA*)pv;
				ApplyOverrideToLogFontA(*lf, false);
			}
			return ret;
		}

		int WINAPI newGetObjectW(HANDLE h, int c, LPVOID pv)
		{
			int ret = rawGetObjectW(h, c, pv);
			if (ret >= (int)sizeof(LOGFONTW) && pv && c >= (int)sizeof(LOGFONTW) && IsFontObjectHandle(h))
			{
				LOGFONTW* lf = (LOGFONTW*)pv;
				ApplyOverrideToLogFontW(*lf, false);
			}
			return ret;
		}

		int WINAPI newGetTextFaceA(HDC hdc, int c, LPSTR lpName)
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

		int WINAPI newGetTextFaceW(HDC hdc, int c, LPWSTR lpName)
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

		BOOL WINAPI newGetTextMetricsA(HDC hdc, LPTEXTMETRICA lptm)
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

		BOOL WINAPI newGetTextMetricsW(HDC hdc, LPTEXTMETRICW lptm)
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

		HFONT WINAPI newCreateFontIndirectExA(const ENUMLOGFONTEXDVA* penumlfex)
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

		HFONT WINAPI newCreateFontIndirectExW(const ENUMLOGFONTEXDVW* penumlfex)
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

		BOOL WINAPI newGetCharABCWidthsA(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
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

		BOOL WINAPI newGetCharABCWidthsW(HDC hdc, UINT wFirst, UINT wLast, LPABC lpABC)
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

		BOOL WINAPI newGetCharABCWidthsFloatA(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
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

		BOOL WINAPI newGetCharABCWidthsFloatW(HDC hdc, UINT iFirst, UINT iLast, LPABCFLOAT lpABC)
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

		BOOL WINAPI newGetCharWidthA(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
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

		BOOL WINAPI newGetCharWidthW(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
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

		BOOL WINAPI newGetCharWidth32A(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
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

		BOOL WINAPI newGetCharWidth32W(HDC hdc, UINT iFirst, UINT iLast, LPINT lpBuffer)
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

		DWORD WINAPI newGetKerningPairsA(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetKerningPairsA(hdc, nPairs, lpKerningPairs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		DWORD WINAPI newGetKerningPairsW(HDC hdc, DWORD nPairs, LPKERNINGPAIR lpKerningPairs)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetKerningPairsW(hdc, nPairs, lpKerningPairs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		UINT WINAPI newGetOutlineTextMetricsA(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICA lpotm)
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

		UINT WINAPI newGetOutlineTextMetricsW(HDC hdc, UINT cjCopy, LPOUTLINETEXTMETRICW lpotm)
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

		int WINAPI newAddFontResourceA(LPCSTR lpFileName)
		{
			return rawAddFontResourceA(lpFileName);
		}

		int WINAPI newAddFontResourceW(LPCWSTR lpFileName)
		{
			return rawAddFontResourceW(lpFileName);
		}

		int WINAPI newAddFontResourceExA(LPCSTR name, DWORD fl, PVOID pdv)
		{
			return rawAddFontResourceExA(name, fl, pdv);
		}

		HANDLE WINAPI newAddFontMemResourceEx(PVOID pbFont, DWORD cbFont, PVOID pdv, DWORD* pcFonts)
		{
			return rawAddFontMemResourceEx(pbFont, cbFont, pdv, pcFonts);
		}

		BOOL WINAPI newRemoveFontResourceA(LPCSTR lpFileName)
		{
			return rawRemoveFontResourceA(lpFileName);
		}

		BOOL WINAPI newRemoveFontResourceW(LPCWSTR lpFileName)
		{
			return rawRemoveFontResourceW(lpFileName);
		}

		BOOL WINAPI newRemoveFontResourceExA(LPCSTR name, DWORD fl, PVOID pdv)
		{
			return rawRemoveFontResourceExA(name, fl, pdv);
		}

		BOOL WINAPI newRemoveFontMemResourceEx(HANDLE h)
		{
			return rawRemoveFontMemResourceEx(h);
		}

		int WINAPI newEnumFontsA(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			LPCSTR useFaceName = lpFaceName;
			bool skipOverride = false;
			std::string forcedName = GetForcedFontNameAForRequest(lpFaceName, skipOverride);
			if (sg_unlockFontSelection)
			{
				useFaceName = nullptr;
			}
			else if (!forcedName.empty())
			{
				useFaceName = forcedName.c_str();
			}
			int ret = 0;
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextA context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				ret = rawEnumFontsA(hdc, useFaceName, EnumFontDedupProcA, reinterpret_cast<LPARAM>(&context));
			}
			else
			{
				ret = rawEnumFontsA(hdc, useFaceName, lpProc, lParam);
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		int WINAPI newEnumFontsW(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			LPCWSTR useFaceName = lpFaceName;
			bool skipOverride = false;
			if (sg_unlockFontSelection)
			{
				useFaceName = nullptr;
			}
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			if (!sg_unlockFontSelection && TryGetForcedFontNameWForRequest(lpFaceName, forcedFaceName, skipOverride))
			{
				useFaceName = forcedFaceName;
			}
			int ret = 0;
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextW context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				ret = rawEnumFontsW(hdc, useFaceName, EnumFontDedupProcW, reinterpret_cast<LPARAM>(&context));
			}
			else
			{
				ret = rawEnumFontsW(hdc, useFaceName, lpProc, lParam);
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		int WINAPI newEnumFontFamiliesA(HDC hdc, LPCSTR lpFaceName, FONTENUMPROCA lpProc, LPARAM lParam)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			LPCSTR useFaceName = lpFaceName;
			bool skipOverride = false;
			std::string forcedName = GetForcedFontNameAForRequest(lpFaceName, skipOverride);
			if (sg_unlockFontSelection)
			{
				useFaceName = nullptr;
			}
			else if (!forcedName.empty())
			{
				useFaceName = forcedName.c_str();
			}
			int ret = 0;
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextA context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				ret = rawEnumFontFamiliesA(hdc, useFaceName, EnumFontDedupProcA, reinterpret_cast<LPARAM>(&context));
			}
			else
			{
				ret = rawEnumFontFamiliesA(hdc, useFaceName, lpProc, lParam);
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		int WINAPI newEnumFontFamiliesW(HDC hdc, LPCWSTR lpFaceName, FONTENUMPROCW lpProc, LPARAM lParam)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			LPCWSTR useFaceName = lpFaceName;
			bool skipOverride = false;
			if (sg_unlockFontSelection)
			{
				useFaceName = nullptr;
			}
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			if (!sg_unlockFontSelection && TryGetForcedFontNameWForRequest(lpFaceName, forcedFaceName, skipOverride))
			{
				useFaceName = forcedFaceName;
			}
			int ret = 0;
			if (sg_unlockFontSelection && lpProc)
			{
				EnumFontDedupContextW context = {};
				context.callback = lpProc;
				context.originalLParam = lParam;
				ret = rawEnumFontFamiliesW(hdc, useFaceName, EnumFontDedupProcW, reinterpret_cast<LPARAM>(&context));
			}
			else
			{
				ret = rawEnumFontFamiliesW(hdc, useFaceName, lpProc, lParam);
			}
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		BOOL WINAPI newGetCharWidthFloatA(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
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

		BOOL WINAPI newGetCharWidthFloatW(HDC hdc, UINT iFirst, UINT iLast, PFLOAT lpBuffer)
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

		BOOL WINAPI newGetCharWidthI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPINT piWidths)
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

		BOOL WINAPI newGetCharABCWidthsI(HDC hdc, UINT giFirst, UINT cgi, LPWORD pgi, LPABC lpabc)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetCharABCWidthsI(hdc, giFirst, cgi, pgi, lpabc);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		BOOL WINAPI newGetTextExtentPointI(HDC hdc, LPWORD pgiIn, int cgi, LPSIZE pSize)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextExtentPointI(hdc, pgiIn, cgi, pSize);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		BOOL WINAPI newGetTextExtentExPointI(HDC hdc, LPWORD lpwszString, int cwchString, int nMaxExtent, LPINT lpnFit, LPINT lpnDx, LPSIZE lpSize)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			BOOL ret = rawGetTextExtentExPointI(hdc, lpwszString, cwchString, nMaxExtent, lpnFit, lpnDx, lpSize);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		DWORD WINAPI newGetFontData(HDC hdc, DWORD dwTable, DWORD dwOffset, PVOID pvBuffer, DWORD cjBuffer)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontData(hdc, dwTable, dwOffset, pvBuffer, cjBuffer);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		DWORD WINAPI newGetFontLanguageInfo(HDC hdc)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontLanguageInfo(hdc);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
		}

		DWORD WINAPI newGetFontUnicodeRanges(HDC hdc, LPGLYPHSET lpgs)
		{
			HFONT hOld = nullptr;
			HFONT hNew = ReplaceHdcFont(hdc, &hOld);
			DWORD ret = rawGetFontUnicodeRanges(hdc, lpgs);
			RestoreHdcFont(hdc, hOld, hNew);
			return ret;
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
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			LPCWSTR useName = TryGetForcedFontNameWForRequest(fontFamilyName, forcedFaceName, skipOverride) ? forcedFaceName : fontFamilyName;
			bool applyScale = ShouldApplyFloatSizeScale() && !sg_runtimeFloatScaleActive;
			RuntimeFloatScaleScope scaleScope(applyScale);
			float useFontSize = applyScale ? (fontSize * sg_fFontScale) : fontSize;
			return rawDWriteFactoryCreateTextFormat(factory, useName, fontCollection, fontWeight, fontStyle, fontStretch, useFontSize, localeName, textFormat);
		}

		static void TryHookDWriteFactoryMethods(IUnknown* factory)
		{
			if (!factory || sg_hookedDWriteCreateTextFormat)
			{
				return;
			}
			void** vtable = *(void***)factory;
			if (!vtable)
			{
				return;
			}
			pDWriteFactoryCreateTextFormat target = (pDWriteFactoryCreateTextFormat)vtable[15];
			if (!target)
			{
				return;
			}
			rawDWriteFactoryCreateTextFormat = target;
			bool failed = DetourAttachFunc(&rawDWriteFactoryCreateTextFormat, newDWriteFactoryCreateTextFormat);
			sg_hookedDWriteCreateTextFormat = !failed;
		}

		HRESULT WINAPI newDWriteCreateFactory(UINT factoryType, REFIID iid, IUnknown** factory)
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

		int WINAPI newGdipCreateFontFamilyFromName(LPCWSTR name, void* fontCollection, void** fontFamily)
		{
			bool skipOverride = false;
			wchar_t forcedFaceName[LF_FACESIZE] = {};
			LPCWSTR useName = TryGetForcedFontNameWForRequest(name, forcedFaceName, skipOverride) ? forcedFaceName : name;
			return rawGdipCreateFontFamilyFromName(useName, fontCollection, fontFamily);
		}

		int WINAPI newGdipCreateFontFromLogfontW(HDC hdc, const LOGFONTW* logfont, void** font)
		{
			LOGFONTW lf = {};
			if (logfont)
			{
				lf = *logfont;
			}
			ApplyOverrideToLogFontW(lf);
			return rawGdipCreateFontFromLogfontW(hdc, &lf, font);
		}

		int WINAPI newGdipCreateFontFromLogfontA(HDC hdc, const LOGFONTA* logfont, void** font)
		{
			LOGFONTA lf = {};
			if (logfont)
			{
				lf = *logfont;
			}
			ApplyOverrideToLogFontA(lf);
			return rawGdipCreateFontFromLogfontA(hdc, &lf, font);
		}

		int WINAPI newGdipCreateFontFromHFONT(HDC hdc, HFONT hfont, void** font)
		{
			return rawGdipCreateFontFromHFONT(hdc, hfont, font);
		}

		int WINAPI newGdipCreateFontFromDC(HDC hdc, void** font)
		{
			return rawGdipCreateFontFromDC(hdc, font);
		}

		int WINAPI newGdipCreateFont(const void* fontFamily, float emSize, int style, int unit, void** font)
		{
			bool applyScale = ShouldApplyFloatSizeScale() && !sg_runtimeFloatScaleActive;
			RuntimeFloatScaleScope scaleScope(applyScale);
			float useEmSize = applyScale ? (emSize * sg_fFontScale) : emSize;
			return rawGdipCreateFont(fontFamily, useEmSize, style, unit, font);
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

		int WINAPI newGdipDrawString(void* graphics, const WCHAR* string, int length, const void* font, const void* layoutRect, const void* stringFormat, const void* brush)
		{
			if (IsCnJpMapEnabled())
			{
				std::wstring mapped = PrepareGdipGlyphStageText(string, length);
				int sourceLength = length < 0 ? (string ? (int)wcslen(string) : 0) : length;
				LogGdipGlyphStageDecision(L"GdipDrawString", string, sourceLength, mapped);
				if (sourceLength > 0 && !mapped.empty() && mapped != std::wstring(string, sourceLength))
				{
					return rawGdipDrawString(graphics, mapped.c_str(), (int)mapped.length(), font, layoutRect, stringFormat, brush);
				}
			}
			return rawGdipDrawString(graphics, string, length, font, layoutRect, stringFormat, brush);
		}

		int WINAPI newGdipDrawDriverString(void* graphics, const UINT16* text, int length, const void* font, const void* brush, const void* positions, int flags, const void* matrix)
		{
			if (IsCnJpMapEnabled())
			{
				std::wstring mapped = PrepareGdipGlyphStageDriverText(text, length);
				int sourceLength = length < 0 ? (text ? (int)wcslen(reinterpret_cast<const wchar_t*>(text)) : 0) : length;
				LogGdipGlyphStageDecision(L"GdipDrawDriverString", reinterpret_cast<const wchar_t*>(text), sourceLength, mapped);
				if (sourceLength > 0 && !mapped.empty() && mapped != std::wstring(reinterpret_cast<const wchar_t*>(text), sourceLength))
				{
					return rawGdipDrawDriverString(graphics, reinterpret_cast<const UINT16*>(mapped.c_str()), (int)mapped.length(), font, brush, positions, flags, matrix);
				}
			}
			return rawGdipDrawDriverString(graphics, text, length, font, brush, positions, flags, matrix);
		}

		int WINAPI newGdipMeasureString(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, void* boundingBox, INT* codepointsFitted, INT* linesFilled)
		{
			if (IsCnJpMapEnabled())
			{
				std::wstring mapped = PrepareGdipGlyphStageText(string, length);
				int sourceLength = length < 0 ? (string ? (int)wcslen(string) : 0) : length;
				LogGdipGlyphStageDecision(L"GdipMeasureString", string, sourceLength, mapped);
				if (sourceLength > 0 && !mapped.empty() && mapped != std::wstring(string, sourceLength))
				{
					return rawGdipMeasureString(graphics, mapped.c_str(), (INT)mapped.length(), font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled);
				}
			}
			return rawGdipMeasureString(graphics, string, length, font, layoutRect, stringFormat, boundingBox, codepointsFitted, linesFilled);
		}

		int WINAPI newGdipMeasureCharacterRanges(void* graphics, const WCHAR* string, INT length, const void* font, const void* layoutRect, const void* stringFormat, INT regionCount, void** regions)
		{
			if (IsCnJpMapEnabled())
			{
				std::wstring mapped = PrepareGdipGlyphStageText(string, length);
				int sourceLength = length < 0 ? (string ? (int)wcslen(string) : 0) : length;
				LogGdipGlyphStageDecision(L"GdipMeasureCharacterRanges", string, sourceLength, mapped);
				if (sourceLength > 0 && !mapped.empty() && mapped != std::wstring(string, sourceLength))
				{
					return rawGdipMeasureCharacterRanges(graphics, mapped.c_str(), (INT)mapped.length(), font, layoutRect, stringFormat, regionCount, regions);
				}
			}
			return rawGdipMeasureCharacterRanges(graphics, string, length, font, layoutRect, stringFormat, regionCount, regions);
		}

		int WINAPI newGdipMeasureDriverString(void* graphics, const UINT16* text, INT length, const void* font, const void* positions, INT flags, const void* matrix, void* boundingBox)
		{
			if (IsCnJpMapEnabled())
			{
				std::wstring mapped = PrepareGdipGlyphStageDriverText(text, length);
				int sourceLength = length < 0 ? (text ? (int)wcslen(reinterpret_cast<const wchar_t*>(text)) : 0) : length;
				LogGdipGlyphStageDecision(L"GdipMeasureDriverString", reinterpret_cast<const wchar_t*>(text), sourceLength, mapped);
				if (sourceLength > 0 && !mapped.empty() && mapped != std::wstring(reinterpret_cast<const wchar_t*>(text), sourceLength))
				{
					return rawGdipMeasureDriverString(graphics, reinterpret_cast<const UINT16*>(mapped.c_str()), (INT)mapped.length(), font, positions, flags, matrix, boundingBox);
				}
			}
			return rawGdipMeasureDriverString(graphics, text, length, font, positions, flags, matrix, boundingBox);
		}

		static void TryHookLateLoadedModules(const wchar_t* moduleName)
		{
			if (!moduleName)
			{
				return;
			}
			if (wcsstr(moduleName, L"dwrite") || wcsstr(moduleName, L"DWRITE"))
			{
				HookDWriteCreateFactory();
			}
			if (wcsstr(moduleName, L"gdiplus") || wcsstr(moduleName, L"GDIPLUS"))
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
		}

		HMODULE WINAPI newLoadLibraryW(LPCWSTR lpLibFileName)
		{
			HMODULE hModule = rawLoadLibraryW(lpLibFileName);
			if (hModule)
			{
				TryHookLateLoadedModules(lpLibFileName);
			}
			return hModule;
		}

		HMODULE WINAPI newLoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
		{
			HMODULE hModule = rawLoadLibraryExW(lpLibFileName, hFile, dwFlags);
			if (hModule && (dwFlags & LOAD_LIBRARY_AS_DATAFILE) == 0 && (dwFlags & LOAD_LIBRARY_AS_IMAGE_RESOURCE) == 0)
			{
				TryHookLateLoadedModules(lpLibFileName);
			}
			return hModule;
		}

		bool HookCreateFontIndirectExA()
		{
			return DetourAttachFunc(&rawCreateFontIndirectExA, newCreateFontIndirectExA);
		}

		bool HookCreateFontIndirectExW()
		{
			return DetourAttachFunc(&rawCreateFontIndirectExW, newCreateFontIndirectExW);
		}

		bool HookGetObjectA()
		{
			return DetourAttachFunc(&rawGetObjectA, newGetObjectA);
		}

		bool HookGetObjectW()
		{
			return DetourAttachFunc(&rawGetObjectW, newGetObjectW);
		}

		bool HookGetTextFaceA()
		{
			return DetourAttachFunc(&rawGetTextFaceA, newGetTextFaceA);
		}

		bool HookGetTextFaceW()
		{
			return DetourAttachFunc(&rawGetTextFaceW, newGetTextFaceW);
		}

		bool HookGetTextMetricsA()
		{
			return DetourAttachFunc(&rawGetTextMetricsA, newGetTextMetricsA);
		}

		bool HookGetTextMetricsW()
		{
			return DetourAttachFunc(&rawGetTextMetricsW, newGetTextMetricsW);
		}

		bool HookGetCharABCWidthsA()
		{
			return DetourAttachFunc(&rawGetCharABCWidthsA, newGetCharABCWidthsA);
		}

		bool HookGetCharABCWidthsW()
		{
			return DetourAttachFunc(&rawGetCharABCWidthsW, newGetCharABCWidthsW);
		}

		bool HookGetCharABCWidthsFloatA()
		{
			return DetourAttachFunc(&rawGetCharABCWidthsFloatA, newGetCharABCWidthsFloatA);
		}

		bool HookGetCharABCWidthsFloatW()
		{
			return DetourAttachFunc(&rawGetCharABCWidthsFloatW, newGetCharABCWidthsFloatW);
		}

		bool HookGetCharWidthA()
		{
			return DetourAttachFunc(&rawGetCharWidthA, newGetCharWidthA);
		}

		bool HookGetCharWidthW()
		{
			return DetourAttachFunc(&rawGetCharWidthW, newGetCharWidthW);
		}

		bool HookGetCharWidth32A()
		{
			return DetourAttachFunc(&rawGetCharWidth32A, newGetCharWidth32A);
		}

		bool HookGetCharWidth32W()
		{
			return DetourAttachFunc(&rawGetCharWidth32W, newGetCharWidth32W);
		}

		bool HookGetKerningPairsA()
		{
			return DetourAttachFunc(&rawGetKerningPairsA, newGetKerningPairsA);
		}

		bool HookGetKerningPairsW()
		{
			return DetourAttachFunc(&rawGetKerningPairsW, newGetKerningPairsW);
		}

		bool HookGetOutlineTextMetricsA()
		{
			return DetourAttachFunc(&rawGetOutlineTextMetricsA, newGetOutlineTextMetricsA);
		}

		bool HookGetOutlineTextMetricsW()
		{
			return DetourAttachFunc(&rawGetOutlineTextMetricsW, newGetOutlineTextMetricsW);
		}

		bool HookAddFontResourceA()
		{
			return DetourAttachFunc(&rawAddFontResourceA, newAddFontResourceA);
		}

		bool HookAddFontResourceW()
		{
			return DetourAttachFunc(&rawAddFontResourceW, newAddFontResourceW);
		}

		bool HookAddFontResourceExA()
		{
			return DetourAttachFunc(&rawAddFontResourceExA, newAddFontResourceExA);
		}

		bool HookAddFontMemResourceEx()
		{
			return DetourAttachFunc(&rawAddFontMemResourceEx, newAddFontMemResourceEx);
		}

		bool HookRemoveFontResourceA()
		{
			return DetourAttachFunc(&rawRemoveFontResourceA, newRemoveFontResourceA);
		}

		bool HookRemoveFontResourceW()
		{
			return DetourAttachFunc(&rawRemoveFontResourceW, newRemoveFontResourceW);
		}

		bool HookRemoveFontResourceExA()
		{
			return DetourAttachFunc(&rawRemoveFontResourceExA, newRemoveFontResourceExA);
		}

		bool HookRemoveFontMemResourceEx()
		{
			return DetourAttachFunc(&rawRemoveFontMemResourceEx, newRemoveFontMemResourceEx);
		}

		bool HookEnumFontsA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontsA, newEnumFontsA);
		}

		bool HookEnumFontsW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontsW, newEnumFontsW);
		}

		bool HookEnumFontFamiliesA(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontFamiliesA, newEnumFontFamiliesA);
		}

		bool HookEnumFontFamiliesW(bool unlockSelection)
		{
			sg_unlockFontSelection = unlockSelection;
			return DetourAttachFunc(&rawEnumFontFamiliesW, newEnumFontFamiliesW);
		}

		bool HookGetCharWidthFloatA()
		{
			return DetourAttachFunc(&rawGetCharWidthFloatA, newGetCharWidthFloatA);
		}

		bool HookGetCharWidthFloatW()
		{
			return DetourAttachFunc(&rawGetCharWidthFloatW, newGetCharWidthFloatW);
		}

		bool HookGetCharWidthI()
		{
			return DetourAttachFunc(&rawGetCharWidthI, newGetCharWidthI);
		}

		bool HookGetCharABCWidthsI()
		{
			return DetourAttachFunc(&rawGetCharABCWidthsI, newGetCharABCWidthsI);
		}

		bool HookGetTextExtentPointI()
		{
			return DetourAttachFunc(&rawGetTextExtentPointI, newGetTextExtentPointI);
		}

		bool HookGetTextExtentExPointI()
		{
			return DetourAttachFunc(&rawGetTextExtentExPointI, newGetTextExtentExPointI);
		}

		bool HookGetFontData()
		{
			return DetourAttachFunc(&rawGetFontData, newGetFontData);
		}

		bool HookGetFontLanguageInfo()
		{
			return DetourAttachFunc(&rawGetFontLanguageInfo, newGetFontLanguageInfo);
		}

		bool HookGetFontUnicodeRanges()
		{
			return DetourAttachFunc(&rawGetFontUnicodeRanges, newGetFontUnicodeRanges);
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
			bool failed = DetourAttachFunc(&rawDWriteCreateFactory, newDWriteCreateFactory);
			sg_hookedDWriteCreateFactory = !failed;
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
			bool failed = DetourAttachFunc(&rawGdipCreateFontFamilyFromName, newGdipCreateFontFamilyFromName);
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
			bool failed = DetourAttachFunc(&rawGdipCreateFontFromLogfontW, newGdipCreateFontFromLogfontW);
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
			bool failed = DetourAttachFunc(&rawGdipCreateFontFromLogfontA, newGdipCreateFontFromLogfontA);
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
			bool failed = DetourAttachFunc(&rawGdipCreateFontFromHFONT, newGdipCreateFontFromHFONT);
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
			bool failed = DetourAttachFunc(&rawGdipCreateFontFromDC, newGdipCreateFontFromDC);
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
			bool failed = DetourAttachFunc(&rawGdipCreateFont, newGdipCreateFont);
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
			bool failed = DetourAttachFunc(&rawGdipDrawString, newGdipDrawString);
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
			bool failed = DetourAttachFunc(&rawGdipDrawDriverString, newGdipDrawDriverString);
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
			bool failed = DetourAttachFunc(&rawGdipMeasureString, newGdipMeasureString);
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
			bool failed = DetourAttachFunc(&rawGdipMeasureCharacterRanges, newGdipMeasureCharacterRanges);
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
			bool failed = DetourAttachFunc(&rawGdipMeasureDriverString, newGdipMeasureDriverString);
			sg_hookedGdipMeasureDriverString = !failed;
			return failed;
		}

		bool HookLoadLibraryW()
		{
			if (sg_hookedLoadLibraryW)
			{
				return false;
			}
			bool failed = DetourAttachFunc(&rawLoadLibraryW, newLoadLibraryW);
			sg_hookedLoadLibraryW = !failed;
			return failed;
		}

		bool HookLoadLibraryExW()
		{
			if (sg_hookedLoadLibraryExW)
			{
				return false;
			}
			bool failed = DetourAttachFunc(&rawLoadLibraryExW, newLoadLibraryExW);
			sg_hookedLoadLibraryExW = !failed;
			return failed;
		}

