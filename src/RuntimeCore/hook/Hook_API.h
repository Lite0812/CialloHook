#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		enum class LogLevel : uint32_t
		{
			Debug = 0,
			Info = 1,
			Warn = 2,
			Error = 3
		};

		void InitLogger(const wchar_t* moduleName, bool enableDebug, bool enableFile, bool enableConsole = false);
		void ShutdownLogger();
		void LogMessage(LogLevel level, const wchar_t* format, ...);
		void LogMessageA(LogLevel level, const char* format, ...);
		void SetHookRuntimeShuttingDown(bool shuttingDown);
		bool IsHookRuntimeShuttingDown();
		std::wstring GetRuntimeCacheRoot(const wchar_t* fallbackBaseDir = nullptr);
		std::wstring GetRuntimeCacheDir(const wchar_t* fallbackBaseDir, const wchar_t* relativePath);
		bool RemoveDirectoryTreeIfEmpty(const wchar_t* path, uint32_t maxLevels);
		bool IsManagedRuntimeCachePath(const wchar_t* path);

		bool LoadFontFromFile(const wchar_t* wpFontFilePath, bool showError = true);
		void SetFontHookRules(
			const wchar_t* const* skipFontNames,
			size_t skipCount,
			const wchar_t* const* redirectFromFontNames,
			const wchar_t* const* redirectToFontNames,
			size_t redirectCount);
		
		bool HookCreateFontA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight = 0, int iWidth = 0, int iWeight = 0, float fScale = 1.0f, float fSpacingScale = 1.0f, float fGlyphAspectRatio = 1.0f, int iGlyphOffsetX = 0, int iGlyphOffsetY = 0, int iMetricsOffsetLeft = 0, int iMetricsOffsetRight = 0, int iMetricsOffsetTop = 0, int iMetricsOffsetBottom = 0);
		bool HookCreateFontIndirectA(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const char* cpFontName, int iHeight = 0, int iWidth = 0, int iWeight = 0, float fScale = 1.0f, float fSpacingScale = 1.0f, float fGlyphAspectRatio = 1.0f, int iGlyphOffsetX = 0, int iGlyphOffsetY = 0, int iMetricsOffsetLeft = 0, int iMetricsOffsetRight = 0, int iMetricsOffsetTop = 0, int iMetricsOffsetBottom = 0);
		
		bool HookCreateFontW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight = 0, int iWidth = 0, int iWeight = 0, float fScale = 1.0f, float fSpacingScale = 1.0f, float fGlyphAspectRatio = 1.0f, int iGlyphOffsetX = 0, int iGlyphOffsetY = 0, int iMetricsOffsetLeft = 0, int iMetricsOffsetRight = 0, int iMetricsOffsetTop = 0, int iMetricsOffsetBottom = 0);
		bool HookCreateFontIndirectW(const uint32_t uiCharSet, bool enableCharsetSpoof, uint32_t spoofFromCharSet, uint32_t spoofToCharSet, const wchar_t* wpFontName, int iHeight = 0, int iWidth = 0, int iWeight = 0, float fScale = 1.0f, float fSpacingScale = 1.0f, float fGlyphAspectRatio = 1.0f, int iGlyphOffsetX = 0, int iGlyphOffsetY = 0, int iMetricsOffsetLeft = 0, int iMetricsOffsetRight = 0, int iMetricsOffsetTop = 0, int iMetricsOffsetBottom = 0);
		bool HookEnumFontFamiliesExA(bool unlockSelection);
		bool HookEnumFontFamiliesExW(bool unlockSelection);
		bool HookCreateFontIndirectExA();
		bool HookCreateFontIndirectExW();
		bool HookGetObjectA();
		bool HookGetObjectW();
		bool HookGetTextFaceA();
		bool HookGetTextFaceW();
		bool HookGetTextMetricsA();
		bool HookGetTextMetricsW();
		bool HookGetCharABCWidthsA();
		bool HookGetCharABCWidthsW();
		bool HookGetCharABCWidthsFloatA();
		bool HookGetCharABCWidthsFloatW();
		bool HookGetCharWidthA();
		bool HookGetCharWidthW();
		bool HookGetCharWidth32A();
		bool HookGetCharWidth32W();
		bool HookGetKerningPairsA();
		bool HookGetKerningPairsW();
		bool HookGetOutlineTextMetricsA();
		bool HookGetOutlineTextMetricsW();
		bool HookAddFontResourceA();
		bool HookAddFontResourceW();
		bool HookAddFontResourceExA();
		bool HookAddFontMemResourceEx();
		bool HookRemoveFontResourceA();
		bool HookRemoveFontResourceW();
		bool HookRemoveFontResourceExA();
		bool HookRemoveFontMemResourceEx();
		bool HookEnumFontsA(bool unlockSelection);
		bool HookEnumFontsW(bool unlockSelection);
		bool HookEnumFontFamiliesA(bool unlockSelection);
		bool HookEnumFontFamiliesW(bool unlockSelection);
		bool HookGetCharWidthFloatA();
		bool HookGetCharWidthFloatW();
		bool HookGetCharWidthI();
		bool HookGetCharABCWidthsI();
		bool HookGetTextExtentPointI();
		bool HookGetTextExtentExPointI();
		bool HookGetFontData();
		bool HookGetFontLanguageInfo();
		bool HookGetFontUnicodeRanges();
		bool HookDWriteCreateFactory();
		bool HookGdipCreateFontFamilyFromName();
		bool HookGdipCreateFontFromLogfontW();
		bool HookGdipCreateFontFromLogfontA();
		bool HookGdipCreateFontFromHFONT();
		bool HookGdipCreateFontFromDC();
		bool HookGdipCreateFont();
		bool HookGdipDrawString();
		bool HookGdipDrawDriverString();
		bool HookGdipMeasureString();
		bool HookGdipMeasureCharacterRanges();
		bool HookGdipMeasureDriverString();
		bool HookLoadLibraryW();
		bool HookLoadLibraryExW();
		
		// 文字替换功能
		void AddTextReplaceRule(const char* original, const char* replacement);
		void AddTextReplaceRuleW(const wchar_t* original, const wchar_t* replacement);
		void SetTextReplaceEncoding(uint32_t codePage);
		void SetTextReplaceEncodings(uint32_t readCodePage, uint32_t writeCodePage);
		void EnableTextReplaceVerboseLog(bool enable);
		void SetTextReplaceBypass(bool bypass);
		void SetCnJpMapEncoding(uint32_t codePage);
		void SetWaffleGetTextCrashPatchEnabled(bool enable);
		void EnableCnJpMap(bool enable);
		void EnableCnJpMapVerboseLog(bool enable);
		bool LoadCnJpMapFile(const wchar_t* jsonFilePath);
		bool IsCnJpMapEnabled();
		std::wstring ProcessGlyphStageTextW(const wchar_t* text, int length);
		
		bool HookTextOutA();
		bool HookTextOutW();
		bool HookExtTextOutA();
		bool HookExtTextOutW();
		bool HookDrawTextA();
		bool HookDrawTextW();
		bool HookDrawTextExA();
		bool HookDrawTextExW();
		bool HookPolyTextOutA();
		bool HookPolyTextOutW();
		bool HookTabbedTextOutA();
		bool HookTabbedTextOutW();
		bool HookGetTabbedTextExtentA();
		bool HookGetTabbedTextExtentW();
		bool HookGetTextExtentPoint32A();
		bool HookGetTextExtentPoint32W();
		bool HookGetTextExtentExPointA();
		bool HookGetTextExtentExPointW();
		bool HookGetTextExtentPointA();
		bool HookGetTextExtentPointW();
		bool HookGetCharacterPlacementA();
		bool HookGetCharacterPlacementW();
		bool HookGetGlyphIndicesA();
		bool HookGetGlyphIndicesW();
		bool HookGetGlyphOutlineA();
		bool HookGetGlyphOutlineW();
		
		// 窗口标题替换功能
		void AddWindowTitleRule(const wchar_t* originalTitle, const wchar_t* newTitle);
		void SetWindowTitleEncoding(uint32_t codePage);
		void SetWindowTitleEncodings(uint32_t readCodePage, uint32_t writeCodePage);
		void EnableWindowTitleVerboseLog(bool enable);
		void SetWindowTitleReplaceBypass(bool bypass);
		void EnableStartupWindowGate(bool enable, uint32_t bypassThreadId);
		void ReleaseStartupWindowGate();
		bool HookWindowTitleAPIs(int mode = 2);
		int ShowExternalStartupConsentDialog(const wchar_t* title, const wchar_t* body);
		bool BringProcessMainWindowToFront(uint32_t processId, uint32_t waitTimeoutMs = 8000);

		// Siglus XOR 密钥提取功能
		void SetKeyExtractConfig(const wchar_t* gameexePath, const wchar_t* outputPath, bool showMsgBox);
		bool EnableSiglusKeyExtract();
		bool IsSiglusKeyExtracted();
			
		// 代码页转换功能
		void SetCodePageMapping(uint32_t fromCodePage, uint32_t toCodePage);
		bool HookCodePageAPIs();
		
		// 文件热补丁功能
		void SetPatchFolder(const wchar_t* folderPath, bool enableLog = false);
		void SetPatchFolders(const wchar_t* const* folderPaths, size_t folderCount, bool enableLog = false);
		void SetSpoofRules(const wchar_t* const* filePaths, size_t fileCount, const wchar_t* const* directoryPaths, size_t directoryCount, bool enableLog = false);
		void SetDirectoryRedirectRules(const wchar_t* const* sourceDirectories, const wchar_t* const* targetDirectories, size_t ruleCount, bool enableLog = false);
		bool LoadVirtualRegistryFile(const wchar_t* regFilePath, bool enableLog = false);
		bool LoadVirtualRegistryFiles(const wchar_t* const* regFilePaths, size_t count, bool enableLog = false);
		void SetCustomPakVFS(bool enable, const wchar_t* const* pakPaths, size_t pakCount, bool enableLog = false);
		void SetCustomPakReadMode(int mode);
		bool TryGetCustomPakDiskCachePath(const wchar_t* sourcePath, std::wstring& cachePathOut);
		void CleanupCustomPakCacheOnShutdown();
		bool HookFileAPIs();
		bool UnhookFileAPIs();
		bool HookRegistryAPIs();
	}
}
