#pragma once

#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace CialloHook
{
	struct FontRedirectRule
	{
		std::wstring sourceFont;
		std::wstring targetFont;
		std::wstring targetFontNameOverride;
	};

	struct FontSettings
	{
		uint32_t charset = 0x00;
		bool enableCharsetSpoof = false;
		uint32_t spoofFromCharset = 0x80;
		uint32_t spoofToCharset = 0x01;
		std::wstring font;
		std::wstring fontNameOverride;
		std::vector<std::wstring> skipFonts;
		std::vector<FontRedirectRule> redirectRules;
		bool hookCreateFontA = true;
		bool hookCreateFontIndirectA = true;
		bool hookCreateFontW = false;
		bool hookCreateFontIndirectW = false;
		bool hookEnumFontFamiliesExA = true;
		bool hookEnumFontFamiliesExW = false;
		bool hookCreateFontIndirectExA = true;
		bool hookCreateFontIndirectExW = true;
		bool hookGetObjectA = true;
		bool hookGetObjectW = true;
		bool hookGetTextFaceA = true;
		bool hookGetTextFaceW = true;
		bool hookGetTextMetricsA = true;
		bool hookGetTextMetricsW = true;
		bool hookGetCharABCWidthsA = true;
		bool hookGetCharABCWidthsW = true;
		bool hookGetCharABCWidthsFloatA = true;
		bool hookGetCharABCWidthsFloatW = true;
		bool hookGetCharWidthA = true;
		bool hookGetCharWidthW = true;
		bool hookGetCharWidth32A = true;
		bool hookGetCharWidth32W = true;
		bool hookGetKerningPairsA = true;
		bool hookGetKerningPairsW = true;
		bool hookGetOutlineTextMetricsA = true;
		bool hookGetOutlineTextMetricsW = true;
		bool hookAddFontResourceA = true;
		bool hookAddFontResourceW = true;
		bool hookAddFontResourceExA = true;
		bool hookAddFontMemResourceEx = true;
		bool hookRemoveFontResourceA = true;
		bool hookRemoveFontResourceW = true;
		bool hookRemoveFontResourceExA = true;
		bool hookRemoveFontMemResourceEx = true;
		bool hookEnumFontsA = true;
		bool hookEnumFontsW = true;
		bool hookEnumFontFamiliesA = true;
		bool hookEnumFontFamiliesW = true;
		bool hookGetCharWidthFloatA = true;
		bool hookGetCharWidthFloatW = true;
		bool hookGetCharWidthI = true;
		bool hookGetCharABCWidthsI = true;
		bool hookGetTextExtentPointI = true;
		bool hookGetTextExtentExPointI = true;
		bool hookGetFontData = true;
		bool hookGetFontLanguageInfo = true;
		bool hookGetFontUnicodeRanges = true;
		bool hookDWriteCreateFactory = true;
		bool hookGdipCreateFontFamilyFromName = true;
		bool hookGdipCreateFontFromLogfontW = true;
		bool hookGdipCreateFontFromLogfontA = true;
		bool hookGdipCreateFontFromHFONT = true;
		bool hookGdipCreateFontFromDC = true;
		bool hookGdipCreateFont = true;
		bool hookGdipDrawString = true;
		bool hookGdipDrawDriverString = true;
		bool hookGdipMeasureString = true;
		bool hookGdipMeasureCharacterRanges = true;
		bool hookGdipMeasureDriverString = true;
		bool hookLoadLibraryW = true;
		bool hookLoadLibraryExW = true;
		bool unlockFontSelection = false;
		bool enableCnJpMap = false;
		bool cnJpMapVerboseLog = false;
		std::wstring cnJpMapJson = L"subs_cn_jp.json";
		uint32_t cnJpMapEncoding = 0;
		uint32_t cnJpMapReadEncoding = 0;
		int fontHeight = 0;
		int fontWidth = 0;
		int fontWeight = 0;
		float fontScale = 1.0f;
		float fontSpacingScale = 1.0f;
		float glyphAspectRatio = 1.0f;
		int glyphOffsetX = 0;
		int glyphOffsetY = 0;
		int metricsOffsetLeft = 0;
		int metricsOffsetRight = 0;
		int metricsOffsetTop = 0;
		int metricsOffsetBottom = 0;
	};

	struct TextReplaceSettings
	{
		std::vector<std::pair<std::wstring, std::wstring>> rules;
		uint32_t encoding = 0;
		uint32_t readEncoding = 0;
		uint32_t writeEncoding = 0;
		bool enableVerboseLog = false;
		bool hookTextOutA = true;
		bool hookTextOutW = true;
		bool hookExtTextOutA = true;
		bool hookExtTextOutW = true;
		bool hookDrawTextA = true;
		bool hookDrawTextW = true;
		bool hookDrawTextExA = true;
		bool hookDrawTextExW = true;
		bool hookPolyTextOutA = true;
		bool hookPolyTextOutW = true;
		bool hookTabbedTextOutA = true;
		bool hookTabbedTextOutW = true;
		bool hookGetTabbedTextExtentA = true;
		bool hookGetTabbedTextExtentW = true;
		bool hookGetTextExtentPoint32A = true;
		bool hookGetTextExtentPoint32W = true;
		bool hookGetTextExtentExPointA = true;
		bool hookGetTextExtentExPointW = true;
		bool hookGetTextExtentPointA = true;
		bool hookGetTextExtentPointW = true;
		bool hookGetCharacterPlacementA = true;
		bool hookGetCharacterPlacementW = true;
		bool hookGetGlyphIndicesA = true;
		bool hookGetGlyphIndicesW = true;
		bool hookGetGlyphOutlineA = true;
		bool hookGetGlyphOutlineW = true;
		bool hookMessageBoxA = true;
		bool hookSetDlgItemTextA = true;
		bool hookSendDlgItemMessageA = true;
		bool hookSendDlgItemMessageW = true;
		bool hookSendMessageA = true;
		bool hookSendMessageW = true;
		bool hookAppendMenuA = true;
		bool hookModifyMenuA = true;
		bool hookInsertMenuA = true;
		bool hookInsertMenuItemA = true;
		bool hookSetMenuItemInfoA = true;
		bool hookMessageBoxIndirectA = true;
		bool hookDrawThemeText = true;
		bool hookDrawThemeTextEx = true;
		bool hookDefWindowProcA = true;
		bool hookDefWindowProcW = true;
		bool hookDialogBoxParamA = true;
		bool hookDialogBoxParamW = true;
		bool hookCreateDialogParamA = true;
		bool hookCreateDialogParamW = true;
		bool hookDialogBoxIndirectParamA = true;
		bool hookDialogBoxIndirectParamW = true;
		bool hookCreateDialogIndirectParamA = true;
		bool hookCreateDialogIndirectParamW = true;
		bool hookPropertySheetA = false;
		bool hookExitProcessGuard = false;
	};

	struct WindowTitleSettings
	{
		std::vector<std::pair<std::wstring, std::wstring>> rules;
		int titleMode = 2;
		uint32_t encoding = 0;
		uint32_t readEncoding = 0;
		uint32_t writeEncoding = 0;
		bool enableVerboseLog = false;
	};

	struct StartupMessageSettings
	{
		bool enable = false;
		int style = 1;
		std::wstring title = L"CialloHook";
		std::wstring author;
		std::wstring text;
	};

	struct SiglusKeyExtractSettings
	{
		bool enable = false;
		std::wstring gameexePath = L"Gameexe.dat";
		std::wstring keyOutputPath = L"siglus_key.txt";
		bool showMessageBox = true;
		bool debugMode = false;
	};

	struct FilePatchSettings
	{
		bool enable = false;
		std::vector<std::wstring> patchFolders;
		bool enableLog = false;
		bool debugMode = false;
		bool customPakEnable = false;
		std::vector<std::wstring> customPakFiles;
		int vfsMode = 1;
	};

	struct FileSpoofSettings
	{
		bool enable = false;
		std::vector<std::wstring> spoofFiles;
		std::vector<std::wstring> spoofDirectories;
		bool enableLog = false;
	};

	struct DirectoryRedirectSettings
	{
		bool enable = false;
		std::vector<std::pair<std::wstring, std::wstring>> rules;
		bool enableLog = false;
	};

	struct RegistrySettings
	{
		bool enable = false;
		std::vector<std::wstring> files;
		bool enableLog = false;
	};

	struct RegistryBootstrapRule
	{
		std::wstring root = L"HKCU";
		std::wstring key;
		std::wstring valueName;
		std::wstring type = L"SZ";
		std::wstring data;
	};

	struct RegistryBootstrapSettings
	{
		bool enable = false;
		bool cleanupOnExit = true;
		bool enableLog = false;
		std::vector<RegistryBootstrapRule> rules;
	};

	struct CodePageSettings
	{
		bool enable = false;
		uint32_t fromCodePage = 932;
		uint32_t toCodePage = 936;
		bool hookMultiByteToWideChar = true;
		bool hookWideCharToMultiByte = true;
	};

	struct DebugSettings
	{
		bool enable = false;
		bool logToFile = false;
		bool logToConsole = false;
	};

	struct LoadModeSettings
	{
		std::wstring mode = L"proxy";
	};

	struct StartupTimingSettings
	{
		std::wstring attachMode = L"immediate";
		uint32_t delayMs = 0;
		bool waitForGuiReady = false;
		bool enableStartupWindowGate = false;
	};

	struct LocaleEmulatorSettings
	{
		bool enable = false;
		uint32_t ansiCodePage = 932;
		uint32_t oemCodePage = 932;
		uint32_t localeID = 0x411;
		uint32_t defaultCharset = 128;
		uint32_t hookUILanguageAPI = 0;
		std::wstring timezone = L"Tokyo Standard Time";
	};

	struct AliceSystem3xSettings
	{
		bool enable = false;
		std::vector<std::wstring> patchFolders = { L"patch" };
		bool enableLog = false;
		bool hookExistsCheck = false;
		uint32_t maxFileSize = 268435456;
	};

	struct RioShiinaSettings
	{
		bool enable = false;
		int mode = 0;
		std::vector<std::wstring> patchNames;
		std::wstring extractOutputDir = L"rio_extract";
		std::vector<std::wstring> archivesToExtract;
		bool skipInvalidFileName = true;
		bool enableLog = false;
		bool processReg = true;
		bool processDvd = false;
		uint64_t specDvdFileSize = 0;
	};

	struct EngineCacheSettings
	{
		bool med = false;
		bool majiro = false;
	};

	struct EnginePatchSettings
	{
		bool enableKrkrPatch = false;
		bool krkrPatchVerboseLog = false;
		bool krkrBootstrapBypass = false;
		bool enableKrkrCxdecBridge = false;
		std::vector<std::wstring> krkrPatchNames;
		bool enableWafflePatch = false;
		bool waffleFixGetTextCrash = true;
	};

	struct SplashImageSettings
	{
		bool enable = false;
		std::wstring imageFile = L"splash.png";
		int width = 800;
		int height = 600;
		int entryEffect = 1;
		int exitEffect = 1;
		int entryMs = 1200;
		int holdMs = 1800;
		int exitMs = 1500;
		int durationMs = 0;
		int position = 1;
		int interactionMode = 0;
	};

	struct AppSettings
	{
		FontSettings font;
		TextReplaceSettings textReplace;
		WindowTitleSettings windowTitle;
		StartupMessageSettings startupMessage;
		SplashImageSettings splashImage;
		SiglusKeyExtractSettings siglusKeyExtract;
		FilePatchSettings filePatch;
		FileSpoofSettings fileSpoof;
		DirectoryRedirectSettings directoryRedirect;
		RegistrySettings registry;
		RegistryBootstrapSettings registryBootstrap;
		CodePageSettings codePage;
		DebugSettings debug;
		LoadModeSettings loadMode;
		StartupTimingSettings startupTiming;
		LocaleEmulatorSettings localeEmulator;
		AliceSystem3xSettings aliceSystem3x;
		RioShiinaSettings rioShiina;
		EngineCacheSettings engineCache;
		EnginePatchSettings enginePatches;
	};
}
