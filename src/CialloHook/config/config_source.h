#pragma once

#include "settings.h"
#include "../../CialloLauncher/LauncherTypes.h"

#include <cwchar>

namespace CialloHook
{
	// 使用说明：
	// 1. 默认保持 IniFile，继续读取 CialloHook.ini / version.ini / winmm.ini / CialloLauncher.ini。
	// 2. 如果要把配置直接编进 DLL / Launcher，把下面的 mode 改成 BuiltIn。
	// 3. Proxy 模式只需要修改 ApplyBuiltInConfig。
	// 4. Loader 模式除了 ApplyBuiltInConfig 里的 settings.loadMode.mode = L"loader"，
	//    还必须修改 ApplyBuiltInLauncherConfig 里的 targetExe 等启动器参数。
	// 5. targetDllNames 只放“额外注入 DLL”；CialloLauncher 会自动再注入一次 CialloHook.dll，
	//    所以通常不要把 CialloHook.dll 自己重复写进去。
	// 6. 如果你只是想换一个外部 ini 路径，不想硬编码，保持 IniFile 并填写 iniPathOverride 即可。

	enum class ConfigSourceMode
	{
		IniFile,
		BuiltIn
	};

	struct ConfigSourceSelection
	{
		ConfigSourceMode mode = ConfigSourceMode::IniFile;
		const wchar_t* iniPathOverride = nullptr;
	};

	inline ConfigSourceSelection GetConfigSourceSelection()
	{
		ConfigSourceSelection selection = {};
		selection.mode = ConfigSourceMode::IniFile;
		selection.iniPathOverride = nullptr;
		// selection.mode = ConfigSourceMode::BuiltIn;
		// selection.iniPathOverride = L".\\MyConfig.ini";
		return selection;
	}

	inline void SetBuiltInLauncherTimezone(CialloLauncher::LEB& leb, const wchar_t* timezoneName)
	{
		const wchar_t* effectiveName = (timezoneName && timezoneName[0] != L'\0')
			? timezoneName
			: L"Tokyo Standard Time";
		wcsncpy_s(reinterpret_cast<wchar_t*>(leb.Timezone.StandardName), 32, effectiveName, _TRUNCATE);
		wcsncpy_s(reinterpret_cast<wchar_t*>(leb.Timezone.DaylightName), 32, effectiveName, _TRUNCATE);
	}

	inline void ApplyBuiltInConfig(AppSettings& settings)
	{
		settings = AppSettings{};

		// ---------------- [CialloHook] 字体与字体 Hook ----------------
		settings.font.charset = 0x00;
		settings.font.enableCharsetSpoof = false;
		settings.font.spoofFromCharset = 0x80;
		settings.font.spoofToCharset = 0x01;
		settings.font.font = L"";
		settings.font.fontNameOverride = L"";
		settings.font.skipFonts = {
			// L"MS UI Gothic",
		};
		settings.font.redirectRules = {
			// FontRedirectRule{ L"MS UI Gothic", L"Microsoft YaHei", L"" },
		};
		settings.font.hookCreateFontA = true;
		settings.font.hookCreateFontIndirectA = true;
		settings.font.hookCreateFontW = false;
		settings.font.hookCreateFontIndirectW = false;
		settings.font.hookEnumFontFamiliesExA = true;
		settings.font.hookEnumFontFamiliesExW = false;
		settings.font.hookCreateFontIndirectExA = true;
		settings.font.hookCreateFontIndirectExW = true;
		settings.font.hookGetObjectA = true;
		settings.font.hookGetObjectW = true;
		settings.font.hookGetTextFaceA = true;
		settings.font.hookGetTextFaceW = true;
		settings.font.hookGetTextMetricsA = true;
		settings.font.hookGetTextMetricsW = true;
		settings.font.hookGetCharABCWidthsA = true;
		settings.font.hookGetCharABCWidthsW = true;
		settings.font.hookGetCharABCWidthsFloatA = true;
		settings.font.hookGetCharABCWidthsFloatW = true;
		settings.font.hookGetCharWidthA = true;
		settings.font.hookGetCharWidthW = true;
		settings.font.hookGetCharWidth32A = true;
		settings.font.hookGetCharWidth32W = true;
		settings.font.hookGetKerningPairsA = true;
		settings.font.hookGetKerningPairsW = true;
		settings.font.hookGetOutlineTextMetricsA = true;
		settings.font.hookGetOutlineTextMetricsW = true;
		settings.font.hookAddFontResourceA = true;
		settings.font.hookAddFontResourceW = true;
		settings.font.hookAddFontResourceExA = true;
		settings.font.hookAddFontMemResourceEx = true;
		settings.font.hookRemoveFontResourceA = true;
		settings.font.hookRemoveFontResourceW = true;
		settings.font.hookRemoveFontResourceExA = true;
		settings.font.hookRemoveFontMemResourceEx = true;
		settings.font.hookEnumFontsA = true;
		settings.font.hookEnumFontsW = true;
		settings.font.hookEnumFontFamiliesA = true;
		settings.font.hookEnumFontFamiliesW = true;
		settings.font.hookGetCharWidthFloatA = true;
		settings.font.hookGetCharWidthFloatW = true;
		settings.font.hookGetCharWidthI = true;
		settings.font.hookGetCharABCWidthsI = true;
		settings.font.hookGetTextExtentPointI = true;
		settings.font.hookGetTextExtentExPointI = true;
		settings.font.hookGetFontData = true;
		settings.font.hookGetFontLanguageInfo = true;
		settings.font.hookGetFontUnicodeRanges = true;
		settings.font.hookDWriteCreateFactory = true;
		settings.font.hookGdipCreateFontFamilyFromName = true;
		settings.font.hookGdipCreateFontFromLogfontW = true;
		settings.font.hookGdipCreateFontFromLogfontA = true;
		settings.font.hookGdipCreateFontFromHFONT = true;
		settings.font.hookGdipCreateFontFromDC = true;
		settings.font.hookGdipCreateFont = true;
		settings.font.hookGdipDrawString = true;
		settings.font.hookGdipDrawDriverString = true;
		settings.font.hookGdipMeasureString = true;
		settings.font.hookGdipMeasureCharacterRanges = true;
		settings.font.hookGdipMeasureDriverString = true;
		settings.font.hookLoadLibraryW = true;
		settings.font.hookLoadLibraryExW = true;
		settings.font.unlockFontSelection = true;
		settings.font.enableCnJpMap = false;
		settings.font.cnJpMapVerboseLog = false;
		settings.font.cnJpMapJson = L"subs_cn_jp.json";
		settings.font.cnJpMapEncoding = 0;
		settings.font.cnJpMapReadEncoding = 0;
		settings.font.fontHeight = 0;
		settings.font.fontWidth = 0;
		settings.font.fontWeight = 0;
		settings.font.fontScale = 1.0f;
		settings.font.fontSpacingScale = 1.0f;
		settings.font.glyphAspectRatio = 1.0f;
		settings.font.glyphOffsetX = 0;
		settings.font.glyphOffsetY = 0;
		settings.font.metricsOffsetLeft = 0;
		settings.font.metricsOffsetRight = 0;
		settings.font.metricsOffsetTop = 0;
		settings.font.metricsOffsetBottom = 0;

		// ---------------- [TextReplace] 文本替换 ----------------
		settings.textReplace.rules = {
			// { L"こんにちは", L"你好" },
		};
		settings.textReplace.encoding = 0;
		settings.textReplace.readEncoding = 0;
		settings.textReplace.writeEncoding = 0;
		settings.textReplace.enableVerboseLog = false;
		settings.textReplace.hookTextOutA = true;
		settings.textReplace.hookTextOutW = true;
		settings.textReplace.hookExtTextOutA = true;
		settings.textReplace.hookExtTextOutW = true;
		settings.textReplace.hookDrawTextA = true;
		settings.textReplace.hookDrawTextW = true;
		settings.textReplace.hookDrawTextExA = true;
		settings.textReplace.hookDrawTextExW = true;
		settings.textReplace.hookPolyTextOutA = true;
		settings.textReplace.hookPolyTextOutW = true;
		settings.textReplace.hookTabbedTextOutA = true;
		settings.textReplace.hookTabbedTextOutW = true;
		settings.textReplace.hookGetTabbedTextExtentA = true;
		settings.textReplace.hookGetTabbedTextExtentW = true;
		settings.textReplace.hookGetTextExtentPoint32A = true;
		settings.textReplace.hookGetTextExtentPoint32W = true;
		settings.textReplace.hookGetTextExtentExPointA = true;
		settings.textReplace.hookGetTextExtentExPointW = true;
		settings.textReplace.hookGetTextExtentPointA = true;
		settings.textReplace.hookGetTextExtentPointW = true;
		settings.textReplace.hookGetCharacterPlacementA = true;
		settings.textReplace.hookGetCharacterPlacementW = true;
		settings.textReplace.hookGetGlyphIndicesA = true;
		settings.textReplace.hookGetGlyphIndicesW = true;
		settings.textReplace.hookGetGlyphOutlineA = true;
		settings.textReplace.hookGetGlyphOutlineW = true;

		// ---------------- [WindowTitle] 窗口标题 ----------------
		settings.windowTitle.rules = {
			// { L"*", L"新的窗口标题" },
		};
		settings.windowTitle.titleMode = 2;
		settings.windowTitle.encoding = 0;
		settings.windowTitle.readEncoding = 0;
		settings.windowTitle.writeEncoding = 0;
		settings.windowTitle.enableVerboseLog = false;

		// ---------------- [StartupMessage] 启动声明 ----------------
		settings.startupMessage.enable = false;
		settings.startupMessage.style = 1;
		settings.startupMessage.title = L"CialloHook";
		settings.startupMessage.author = L"";
		settings.startupMessage.text = L"";

		// ---------------- [SiglusKeyExtract] Siglus Key 提取 ----------------
		settings.siglusKeyExtract.enable = false;
		settings.siglusKeyExtract.gameexePath = L"Gameexe.dat";
		settings.siglusKeyExtract.keyOutputPath = L"siglus_key.txt";
		settings.siglusKeyExtract.showMessageBox = true;
		settings.siglusKeyExtract.debugMode = false;

		// ---------------- [FilePatch] 文件补丁 / CustomPak ----------------
		settings.filePatch.enable = false;
		settings.filePatch.patchFolders = {
			L"patch",
		};
		settings.filePatch.enableLog = false;
		settings.filePatch.debugMode = false;
		settings.filePatch.customPakEnable = false;
		settings.filePatch.customPakFiles = {
			// L"patch.cpk",
		};
		settings.filePatch.vfsMode = 1;

		// ---------------- [FileSpoof] 文件/目录伪装 ----------------
		settings.fileSpoof.enable = false;
		settings.fileSpoof.spoofFiles = {
			// L"system.arc",
		};
		settings.fileSpoof.spoofDirectories = {
			// L"save",
		};
		settings.fileSpoof.enableLog = false;

		// ---------------- [DirectoryRedirect] 目录重定向 ----------------
		settings.directoryRedirect.enable = false;
		settings.directoryRedirect.rules = {
			// { L"save", L".\\save_cn" },
		};
		settings.directoryRedirect.enableLog = false;

		// ---------------- [Registry] 注册表导入 ----------------
		settings.registry.enable = false;
		settings.registry.files = {
			// L"game.reg",
		};
		settings.registry.enableLog = false;

		// ---------------- [CodePage] 代码页伪装 ----------------
		settings.codePage.enable = false;
		settings.codePage.fromCodePage = 932;
		settings.codePage.toCodePage = 936;

		// ---------------- [Debug] 调试日志 ----------------
		settings.debug.enable = false;
		settings.debug.logToFile = false;
		settings.debug.logToConsole = false;

		// ---------------- [LoadMode] 加载模式 ----------------
		settings.loadMode.mode = L"proxy";
		// 如果使用 Loader 模式，这里改成：settings.loadMode.mode = L"loader";

		// ---------------- [LocaleEmulator] 转区 ----------------
		settings.localeEmulator.enable = false;
		settings.localeEmulator.ansiCodePage = 932;
		settings.localeEmulator.oemCodePage = 932;
		settings.localeEmulator.localeID = 0x411;
		settings.localeEmulator.defaultCharset = 128;
		settings.localeEmulator.hookUILanguageAPI = 0;
		settings.localeEmulator.timezone = L"Tokyo Standard Time";

		// ---------------- [GLOBAL] / [EnginePatches] 引擎补丁 ----------------
		settings.engineCache.med = false;
		settings.engineCache.majiro = false;
		settings.enginePatches.enableKrkrPatch = false;
		settings.enginePatches.krkrPatchVerboseLog = false;
		settings.enginePatches.krkrBootstrapBypass = false;
		settings.enginePatches.krkrPatchNames = {
			// L"patch.xp3",
		};
		settings.enginePatches.enableWafflePatch = false;
		settings.enginePatches.waffleFixGetTextCrash = true;
	}

	inline void ApplyBuiltInLauncherConfig(CialloLauncher::LauncherConfig& config)
	{
		config = CialloLauncher::LauncherConfig{};

		// ---------------- [CialloLauncher] 启动器模式 ----------------
		// Loader 模式必须填写 targetExe；可以写相对路径，也可以写绝对路径。
		config.targetExe = L"";
		config.debugMode = false;
		config.logToFile = false;
		config.logToConsole = false;
		config.targetDllNames = {
			// L"MyExtraHook.dll",
		};
		config.targetDllCount = static_cast<uint32_t>(config.targetDllNames.size());

		// Loader 外部声明框。如果想在 CialloLauncher.exe 拉起游戏前先弹确认框，就改这里。
		config.startupMessage.enable = false;
		config.startupMessage.title = L"CialloHook";
		config.startupMessage.body = L"";

		// 对应 [FilePatch]，供 Loader 提前解析 patch / CustomPak。
		config.patchFolders = {
			L"patch",
		};
		config.customPakEnable = false;
		config.customPakFiles = {
			// L"patch.cpk",
		};

		// 对应 [LocaleEmulator]。
		config.enableLocaleEmulator = false;
		config.localeEmulatorBlock = {};
		config.localeEmulatorBlock.AnsiCodePage = 932;
		config.localeEmulatorBlock.OemCodePage = 932;
		config.localeEmulatorBlock.LocaleID = 0x411;
		config.localeEmulatorBlock.DefaultCharset = 128;
		config.localeEmulatorBlock.HookUILanguageAPI = 0;
		SetBuiltInLauncherTimezone(config.localeEmulatorBlock, L"Tokyo Standard Time");
	}

	inline const wchar_t* GetBuiltInConfigSourceLabel()
	{
		return L"built-in(config/config_source.h)";
	}

	inline const wchar_t* GetBuiltInLauncherConfigSourceLabel()
	{
		return L"built-in(config/config_source.h :: launcher)";
	}
}