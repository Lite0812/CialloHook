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
	// 5. targetDllNames 是启动器的完整注入列表；需要 CialloHook 功能时，
	//    请显式写入 CialloHook.dll，也可以只写自己的 DLL。
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

	//把这里改成想要的模式即可，上面是定义
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

		// ============================================================
		//  内置配置（与 config/CialloHook.ini 的配置项顺序和默认值保持一致）
		//  使用方式：GetConfigSourceSelection() 中把 mode 改为 BuiltIn 后生效。
		//  说明：Count 类配置在代码里体现为 vector 元素数量；注释项保持为空 vector。
		// ============================================================

		// ======================== [CialloHook] 字体设置 ========================
		// 字符集编码: 0x86(简中), 0x88(繁中), 0x80(日文), 0x00(默认)。
		settings.font.charset = 0x00;

		// 字体名称或路径（留空则不 Hook 字体）。系统字体可写 SimHei/Microsoft YaHei/SimSun 等；
		// 外部字体可写 C:\Fonts\xxx.ttf 或 .\fonts\xxx.otf；仅填文件名时优先在补丁目录/封包中查找。
		settings.font.font = L"SimHei";
		// 外部字体的实际名称（可选，作为手动覆盖项，优先级高于自动识别结果）。
		settings.font.fontNameOverride = L"";

		// 命中这些原始字体名时，不进行字体 Hook（大小写不敏感，优先级高于重定向规则）。
		settings.font.skipFonts = {
			// L"MS Gothic",
			// L"Arial",
		};

		// 命中这些原始字体名时，改 Hook 到指定目标字体；目标可写系统字体名或字体文件路径。
		settings.font.redirectRules = {
			// FontRedirectRule{ L"MS UI Gothic", L"Microsoft YaHei", L"" },
			// FontRedirectRule{ L"Arial", L".\\fonts\\SourceHanSansCN-Regular.otf", L"Source Han Sans CN Regular" },
		};

		// EnableCharsetSpoof: 条件伪装字符集；仅在原始字符集等于 SpoofFromCharset 时替换为 SpoofToCharset。
		settings.font.enableCharsetSpoof = true;
		settings.font.spoofFromCharset = 0x80;
		settings.font.spoofToCharset = 0x01;

		// 可选：日繁映射。启用后读取映射 json，在字形查询、字宽计算和实际绘制阶段把右值字符映射到左值字形。
		// 例如 json 中 "头": "頭" 时，字体请求 "頭" 的字形时会改取 "头" 的字形；兼容 uif_config.json 映射格式。
		settings.font.enableCnJpMap = false;
		settings.font.cnJpMapVerboseLog = false;
		settings.font.cnJpMapJson = L"subs_cn_jp.json";
		settings.font.cnJpMapEncoding = 0;
		// 常见值: 932(日文), 936(简中), 950(繁中), 65001(UTF-8)。
		settings.font.cnJpMapReadEncoding = 932;

		// 字体选择解锁：true=枚举时不过滤字体/字符集，false=按当前字体与字符集约束。
		settings.font.unlockFontSelection = false;
		// 字体 Hook 命中日志：输出具体命中的字体相关 API，日志量较大，排查时再开。
		settings.font.fontHookVerboseLog = false;

		// ---------- 字体尺寸调整 ----------
		// FontHeight: 负数=字符高度，正数=单元高度，0=不固定；FontWidth: 0=不固定，>0=固定宽度。
		settings.font.fontHeight = 0;
		settings.font.fontWidth = 0;
		// FontScale: 字体大小缩放倍数；FontSpacingScale: 字符间距缩放倍数；1.0=不调整。
		settings.font.fontScale = 1.0f;
		settings.font.fontSpacingScale = 1.0f;
		// FontWeight: 0=不改，400=正常，700=粗体；GlyphAspectRatio: 字形宽高比，1.0=不调整。
		settings.font.fontWeight = 0;
		settings.font.glyphAspectRatio = 1.0f;

		// ---------- 字形调整（可能不生效） ----------
		settings.font.glyphOffsetX = 0;
		settings.font.glyphOffsetY = 0;
		settings.font.metricsOffsetLeft = 0;
		settings.font.metricsOffsetRight = 0;
		settings.font.metricsOffsetTop = 0;
		settings.font.metricsOffsetBottom = 0;

		// ---------- API Hook 分组开关 ----------
		// HookGroupCreate/Enumerate/Metrics/Resource/Modern/LateLoad 均与 CialloHook.ini 默认一致为 true。
		const bool hookGroupCreate = true;
		const bool hookGroupEnumerate = true;
		const bool hookGroupMetrics = true;
		const bool hookGroupResource = true;
		const bool hookGroupModern = true;
		const bool hookGroupLateLoad = true;

		// ---------- 单 API 覆盖（按需添加，不写则跟随分组；这里展开为最终内置默认值） ----------
		settings.font.hookCreateFontA = true;
		settings.font.hookCreateFontIndirectA = true;
		settings.font.hookEnumFontFamiliesExA = true;
		settings.font.hookEnumFontFamiliesExW = true;
		settings.font.hookDWriteCreateFactory = true;
		settings.font.hookGdipCreateFontFamilyFromName = true;
		settings.font.hookGdipCreateFontFromLogfontW = true;
		settings.font.hookGdipMeasureDriverString = true;
		// W/IW 不稳定，没 hook 上字体的时候再自行决定开启。
		settings.font.hookCreateFontW = false;
		settings.font.hookCreateFontIndirectW = false;
		settings.font.hookCreateFontIndirectExA = hookGroupCreate;
		settings.font.hookCreateFontIndirectExW = hookGroupCreate;
		settings.font.hookGetObjectA = hookGroupMetrics;
		settings.font.hookGetObjectW = hookGroupMetrics;
		settings.font.hookGetTextFaceA = hookGroupMetrics;
		settings.font.hookGetTextFaceW = hookGroupMetrics;
		settings.font.hookGetTextMetricsA = hookGroupMetrics;
		settings.font.hookGetTextMetricsW = hookGroupMetrics;
		settings.font.hookGetCharABCWidthsA = hookGroupMetrics;
		settings.font.hookGetCharABCWidthsW = hookGroupMetrics;
		settings.font.hookGetCharABCWidthsFloatA = hookGroupMetrics;
		settings.font.hookGetCharABCWidthsFloatW = hookGroupMetrics;
		settings.font.hookGetCharWidthA = hookGroupMetrics;
		settings.font.hookGetCharWidthW = hookGroupMetrics;
		settings.font.hookGetCharWidth32A = hookGroupMetrics;
		settings.font.hookGetCharWidth32W = hookGroupMetrics;
		settings.font.hookGetKerningPairsA = hookGroupMetrics;
		settings.font.hookGetKerningPairsW = hookGroupMetrics;
		settings.font.hookGetOutlineTextMetricsA = hookGroupMetrics;
		settings.font.hookGetOutlineTextMetricsW = hookGroupMetrics;
		settings.font.hookAddFontResourceA = hookGroupResource;
		settings.font.hookAddFontResourceW = hookGroupResource;
		settings.font.hookAddFontResourceExA = hookGroupResource;
		settings.font.hookAddFontMemResourceEx = hookGroupResource;
		settings.font.hookRemoveFontResourceA = hookGroupResource;
		settings.font.hookRemoveFontResourceW = hookGroupResource;
		settings.font.hookRemoveFontResourceExA = hookGroupResource;
		settings.font.hookRemoveFontMemResourceEx = hookGroupResource;
		settings.font.hookEnumFontsA = hookGroupEnumerate;
		settings.font.hookEnumFontsW = hookGroupEnumerate;
		settings.font.hookEnumFontFamiliesA = hookGroupEnumerate;
		settings.font.hookEnumFontFamiliesW = hookGroupEnumerate;
		settings.font.hookGetCharWidthFloatA = hookGroupMetrics;
		settings.font.hookGetCharWidthFloatW = hookGroupMetrics;
		settings.font.hookGetCharWidthI = hookGroupMetrics;
		settings.font.hookGetCharABCWidthsI = hookGroupMetrics;
		settings.font.hookGetTextExtentPointI = hookGroupMetrics;
		settings.font.hookGetTextExtentExPointI = hookGroupMetrics;
		settings.font.hookGetFontData = hookGroupMetrics;
		settings.font.hookGetFontLanguageInfo = hookGroupMetrics;
		settings.font.hookGetFontUnicodeRanges = hookGroupMetrics;
		settings.font.hookGdipCreateFontFromLogfontA = hookGroupModern;
		// CialloHook.ini 未覆盖该项，config_manager 默认 false，内置配置也保持 false。
		settings.font.hookGdipCreateFontFromHFONT = false;
		settings.font.hookGdipCreateFontFromDC = hookGroupModern;
		settings.font.hookGdipCreateFont = hookGroupModern;
		settings.font.hookGdipDrawString = hookGroupModern;
		settings.font.hookGdipDrawDriverString = hookGroupModern;
		settings.font.hookGdipMeasureString = hookGroupModern;
		settings.font.hookGdipMeasureCharacterRanges = hookGroupModern;
		settings.font.hookLoadLibraryW = hookGroupLateLoad;
		settings.font.hookLoadLibraryExW = hookGroupLateLoad;

		// ======================== [CodePage] 代码页转换 ========================
		// 将源代码页转换为目标代码页（Hook MultiByteToWideChar / WideCharToMultiByte）。
		settings.codePage.enable = false;
		settings.codePage.fromCodePage = 932;
		settings.codePage.toCodePage = 936;
		settings.codePage.hookMultiByteToWideChar = true;
		settings.codePage.hookWideCharToMultiByte = true;

		// ======================== [TextReplace] 文字替换 ========================
		// 检测并替换显示文字，支持通配符 * 和 ?；默认 ReplaceCount=0，不替换。
		settings.textReplace.rules = {
			// { L"原始文字", L"替换文字" },
		};
		settings.textReplace.encoding = 0;
		settings.textReplace.readEncoding = 936;
		settings.textReplace.writeEncoding = 936;
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
		settings.textReplace.hookMessageBoxA = true;
		settings.textReplace.hookSetDlgItemTextA = true;
		settings.textReplace.hookSendDlgItemMessageA = true;
		settings.textReplace.hookSendDlgItemMessageW = true;
		settings.textReplace.hookSendMessageA = true;
		settings.textReplace.hookSendMessageW = true;
		settings.textReplace.hookAppendMenuA = true;
		settings.textReplace.hookModifyMenuA = true;
		settings.textReplace.hookInsertMenuA = true;
		settings.textReplace.hookInsertMenuItemA = true;
		settings.textReplace.hookSetMenuItemInfoA = true;
		settings.textReplace.hookMessageBoxIndirectA = true;
		settings.textReplace.hookDrawThemeText = true;
		settings.textReplace.hookDrawThemeTextEx = true;
		settings.textReplace.hookDefWindowProcA = true;
		settings.textReplace.hookDefWindowProcW = true;
		settings.textReplace.hookDialogBoxParamA = true;
		settings.textReplace.hookDialogBoxParamW = true;
		settings.textReplace.hookCreateDialogParamA = true;
		settings.textReplace.hookCreateDialogParamW = true;
		settings.textReplace.hookDialogBoxIndirectParamA = true;
		settings.textReplace.hookDialogBoxIndirectParamW = true;
		settings.textReplace.hookCreateDialogIndirectParamA = true;
		settings.textReplace.hookCreateDialogIndirectParamW = true;
		settings.textReplace.hookPropertySheetA = false;
		settings.textReplace.hookExitProcessGuard = false;

		// ======================== [WindowTitle] 窗口标题替换 ========================
		// CialloHook.ini 中 ReplaceCount=1 但示例规则被注释，因此实际默认无规则。
		settings.windowTitle.rules = {
			// { L"原标题", L"新标题" },
		};
		settings.windowTitle.encoding = 0;
		settings.windowTitle.readEncoding = 936;
		settings.windowTitle.writeEncoding = 936;
		settings.windowTitle.enableVerboseLog = false;
		settings.windowTitle.titleMode = 2;

		// ======================== [FilePatch] 文件热补丁 ========================
		// 游戏读取文件时优先从补丁文件夹查找；用途：汉化补丁、资源替换、MOD 安装。
		settings.filePatch.enable = true;
		settings.filePatch.patchFolders = {
			L"patch",
		};
		// 自定义封包支持 cpk/xp3/lpk，编号越后优先级越高；CialloHook 自身资源也按 PatchFolder > CustomPak > 根目录查找。
		settings.filePatch.customPakEnable = true;
		settings.filePatch.customPakFiles = {
			L"patch.cpk",
		};
		// VFSMode: 0=物理读取模式，1=内存读取模式。
		settings.filePatch.vfsMode = 1;
		settings.filePatch.enableLog = false;
		settings.filePatch.debugMode = false;

		// ======================== [FileSpoof] 文件欺骗 ========================
		// 命中后返回文件/目录不存在。
		settings.fileSpoof.enable = false;
		settings.fileSpoof.spoofFiles = {
			// L"system/license.dat",
		};
		settings.fileSpoof.spoofDirectories = {
			// L"system/secret",
		};
		settings.fileSpoof.enableLog = false;

		// ======================== [DirectoryRedirect] 目录重定向 ========================
		// 将命中的原目录重定向到目标目录，支持 %SAVEDGAMES%/%DOCUMENTS%/%APPDATA% 等环境变量。
		settings.directoryRedirect.enable = false;
		settings.directoryRedirect.enableLog = false;
		settings.directoryRedirect.rules = {
			// { L"%APPDATA%\\GameCompany\\GameName", L"savedata" },
		};

		// ======================== [Registry] 虚拟注册表 ========================
		// 将 UTF-16LE .reg 文件加载到进程内虚拟注册表层，实现免安装运行。
		settings.registry.enable = false;
		settings.registry.files = {
			// L"game.reg",
		};
		settings.registry.enableLog = false;

		// ======================== [RegistryBootstrap] 临时真实注册表 ========================
		// 启动时临时创建真实注册表键值，退出时按 CleanupOnExit 回滚。
		settings.registryBootstrap.enable = false;
		settings.registryBootstrap.cleanupOnExit = true;
		settings.registryBootstrap.enableLog = false;
		settings.registryBootstrap.rules = {
			// { L"HKCU", L"Software\\Vendor\\Game", L"InstallPath", L"SZ", L".\\" },
		};

		// ======================== [BinaryPatch] x64dbg .1337 内存补丁 ========================
		// 启动后自动应用 x64dbg 导出的 .1337 字节补丁；地址按模块基址 + RVA 计算。
		settings.binaryPatch.enable = false;
		settings.binaryPatch.patchFiles = {
			// L"patches\\main.1337",
		};
		settings.binaryPatch.enableLog = false;
		settings.binaryPatch.verifyOldBytes = true;
		settings.binaryPatch.preferCustomPak = false;
		settings.binaryPatch.failOnMissingModule = false;
		settings.binaryPatch.failOnWriteError = false;
		settings.binaryPatch.enableHwbp = false;
		settings.binaryPatch.hwbpModule = L"";
		settings.binaryPatch.hwbpRva = 0;

		// ======================== [SiglusKeyExtract] Siglus 密钥提取 ========================
		settings.siglusKeyExtract.enable = false;
		settings.siglusKeyExtract.gameexePath = L"Gameexe.dat";
		settings.siglusKeyExtract.keyOutputPath = L"siglus_key.txt";
		settings.siglusKeyExtract.showMessageBox = true;
		settings.siglusKeyExtract.debugMode = false;

		// ======================== [AliceSystem3x] Alice System3.x ALD 松散文件覆盖 ========================
		settings.aliceSystem3x.enable = false;
		settings.aliceSystem3x.patchFolders = {
			L"patch",
		};
		settings.aliceSystem3x.enableLog = false;
		settings.aliceSystem3x.hookExistsCheck = false;
		settings.aliceSystem3x.maxFileSize = 268435456;

		// ======================== [RioShiina] RioShiina 引擎资源覆盖 / WARC 解包 ========================
		settings.rioShiina.enable = false;
		settings.rioShiina.mode = 0;
		settings.rioShiina.patchNames = {
			L"unencrypted",
		};
		settings.rioShiina.extractOutputDir = L"rio_extract";
		settings.rioShiina.archivesToExtract = {};
		settings.rioShiina.skipInvalidFileName = true;
		settings.rioShiina.processReg = true;
		settings.rioShiina.processDvd = false;
		settings.rioShiina.specDvdFileSize = 0;
		settings.rioShiina.enableLog = false;

		// ======================== [GLOBAL] 引擎兼容补丁 ========================
		settings.engineCache.med = false;
		settings.engineCache.majiro = false;
		settings.enginePatches.enableKrkrPatch = false;
		settings.enginePatches.krkrPatchVerboseLog = false;
		settings.enginePatches.krkrPatchNames = {
			L"unencrypted",
		};
		settings.enginePatches.krkrBootstrapBypass = false;
		settings.enginePatches.enableKrkrCxdecBridge = false;
		settings.enginePatches.enableWafflePatch = false;
		settings.enginePatches.waffleFixGetTextCrash = true;

		// ======================== [LocaleEmulator] 转区 ========================
		// proxy 模式下由 winmm.dll/version.dll 直接重启转区；loader 模式下由 CialloLauncher 处理。
		settings.localeEmulator.enable = false;
		settings.localeEmulator.ansiCodePage = 932;
		settings.localeEmulator.oemCodePage = 932;
		settings.localeEmulator.localeID = 0x411;
		settings.localeEmulator.defaultCharset = 128;
		settings.localeEmulator.hookUILanguageAPI = 0;
		settings.localeEmulator.timezone = L"Tokyo Standard Time";

		// ======================== [StartupMessage] 启动弹窗 ========================
		settings.startupMessage.enable = false;
		settings.startupMessage.style = 1;
		settings.startupMessage.title = L"CialloHook";
		settings.startupMessage.author = L"";
		settings.startupMessage.text = L"";

		// ======================== [SplashImage] 启动图片弹窗 ========================
		// 图片查找顺序：PatchFolder -> cpk/xp3/lpk CustomPak -> 游戏根目录。
		settings.splashImage.enable = false;
		settings.splashImage.imageFile = L"splash.png";
		settings.splashImage.width = 800;
		settings.splashImage.height = 600;
		settings.splashImage.entryEffect = 1;
		settings.splashImage.entryMs = 1200;
		settings.splashImage.holdMs = 1800;
		settings.splashImage.exitEffect = 1;
		settings.splashImage.exitMs = 1500;
		settings.splashImage.durationMs = 0;
		settings.splashImage.position = 1;
		settings.splashImage.interactionMode = 0;

		// ======================== [ScreenCaptureProtection] 防截图 / 防录屏 ========================
		settings.screenCaptureProtection.enable = false;
		settings.screenCaptureProtection.mode = L"exclude";
		settings.screenCaptureProtection.fallbackToMonitor = true;
		settings.screenCaptureProtection.applyExistingWindows = true;
		settings.screenCaptureProtection.protectToolWindows = false;
		settings.screenCaptureProtection.protectOwnedWindows = false;
		settings.screenCaptureProtection.enableVerboseLog = false;

		// ======================== [LoadMode] 加载模式 ========================
		settings.loadMode.mode = L"proxy";

		// ======================== [CialloLauncher] 配置 ========================
		// AppSettings 不保存 TargetEXE/TargetDLLName_i；loader 内置配置见 ApplyBuiltInLauncherConfig()。

		// ======================== [StartupTiming] 延迟附加 ========================
		settings.startupTiming.attachMode = L"immediate";
		settings.startupTiming.delayMs = 0;
		settings.startupTiming.waitForGuiReady = false;
		settings.startupTiming.enableStartupWindowGate = false;

		// ======================== [Debug] 调试日志 ========================
		settings.debug.enable = true;
		settings.debug.logToFile = true;
		settings.debug.logToConsole = true;
	}

	inline void ApplyBuiltInLauncherConfig(CialloLauncher::LauncherConfig& config)
	{
		config = CialloLauncher::LauncherConfig{};

		// ======================== [CialloLauncher] 配置 ========================
		// 仅在 LoadMode.Mode = loader 时生效。
		// TargetEXE: 目标游戏 EXE；可写相对路径或绝对路径。
		config.targetExe = L"game.exe";
		// DebugMode: CialloLauncher 自身调试模式。
		config.debugMode = false;
		// LogToFile / LogToConsole: 跟随 [Debug] 默认值。
		config.logToFile = true;
		config.logToConsole = true;
		// TargetDLLName_i 是完整注入列表；需要 CialloHook 功能时显式写入 CialloHook.dll。
		config.targetDllNames = {
			L"CialloHook.dll",
		};
		config.targetDllCount = static_cast<uint32_t>(config.targetDllNames.size());

		// ======================== [StartupMessage] 启动弹窗 ========================
		// Loader 模式下可在拉起游戏前显示确认框；默认关闭。
		config.startupMessage.enable = false;
		config.startupMessage.title = L"CialloHook";
		config.startupMessage.body = L"";

		// ======================== [FilePatch] 文件热补丁 ========================
		// 供 Loader 提前解析补丁目录与 CustomPak，默认与 CialloHook.ini 一致。
		config.patchFolders = {
			L"patch",
		};
		config.customPakEnable = true;
		config.customPakFiles = {
			L"patch.cpk",
		};

		// ======================== [LocaleEmulator] 转区 ========================
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