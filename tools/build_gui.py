from __future__ import annotations

import configparser
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PyQt6.QtCore import QProcess, QProcessEnvironment, Qt, pyqtSignal
from PyQt6.QtGui import QFont, QPalette
from PyQt6.QtWidgets import (
    QApplication,
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QSpinBox,
    QStackedWidget,
    QTabWidget,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)


ROOT = Path(__file__).resolve().parents[1]
OPTIONS_PATH = ROOT / "src" / "CialloHook" / "config" / "build_options.h"
PROPS_PATH = ROOT / "src" / "CialloHook" / "config" / "build_options.props"
OVERRIDES_PATH = ROOT / "src" / "CialloHook" / "config" / "gui_config_overrides.h"
STATE_PATH = ROOT / "tools" / "build_gui_state.ini"
BUILD_SCRIPT = ROOT / "build_all.ps1"
WEBM_BUILD_SCRIPT = ROOT / "build_webm.ps1"
CIALLOPAK_TOOL_PATH = ROOT / "tools" / "CialloPAK_tool.py"
LITEPAK_TOOL_PATH = ROOT / "tools" / "LitePAK_tool.py"
XP3_TOOL_PATH = ROOT / "tools" / "XP3_tool.py"
PLATFORM_ALL = "all"
LEGACY_PLATFORM_ALL = "x86/x64 全量"
THEME_SYSTEM = "跟随系统"
THEME_LIGHT = "明亮"
THEME_DARK = "黑暗"
PACK_ARCHIVE_FORMATS = {"cpk", "lpk", "xp3"}
PACK_COMPRESSION_OPTIONS = {
    "cpk": ("auto", "raw", "zlib", "zstd", "lzma"),
    "lpk": ("auto", "raw", "zlib", "zstd", "lzma"),
    "xp3": ("auto", "raw", "zlib"),
}


class WheelPassthroughComboBox(QComboBox):
    def wheelEvent(self, event) -> None:  # type: ignore[override]
        event.ignore()


class WheelPassthroughSpinBox(QSpinBox):
    def wheelEvent(self, event) -> None:  # type: ignore[override]
        event.ignore()


class WheelPassthroughDoubleSpinBox(QDoubleSpinBox):
    def wheelEvent(self, event) -> None:  # type: ignore[override]
        event.ignore()


class DragDropLineEdit(QLineEdit):
    pathsDropped = pyqtSignal(list, str)

    def __init__(self, role: str) -> None:
        super().__init__()
        self.role = role
        self.setAcceptDrops(True)

    def dragEnterEvent(self, event) -> None:  # type: ignore[override]
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            super().dragEnterEvent(event)

    def dragMoveEvent(self, event) -> None:  # type: ignore[override]
        if event.mimeData().hasUrls():
            event.acceptProposedAction()
        else:
            super().dragMoveEvent(event)

    def dropEvent(self, event) -> None:  # type: ignore[override]
        paths = [url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()]
        paths = [path for path in paths if path]
        if paths:
            self.pathsDropped.emit(paths, self.role)
            event.acceptProposedAction()
            return
        super().dropEvent(event)


@dataclass(frozen=True)
class Feature:
    key: str
    label: str
    description: str
    minimal: bool = False


@dataclass(frozen=True)
class Field:
    key: str
    label: str
    kind: str
    default: Any
    path: str
    minimum: float | int | None = None
    maximum: float | int | None = None
    options: tuple[str, ...] = ()


@dataclass(frozen=True)
class TableSpec:
    key: str
    label: str
    columns: tuple[str, ...]
    default: tuple[tuple[str, ...], ...]
    path: str
    cpp_kind: str


FEATURES: list[Feature] = [
    Feature("CIALLOHOOK_FEATURE_FONT", "字体 Hook", "字体替换、字符集伪装、字形度量、日繁映射。", True),
    Feature("CIALLOHOOK_FEATURE_TEXT", "文本 Hook", "文本替换，以及文本绘制/测量 API Hook。", True),
    Feature("CIALLOHOOK_FEATURE_WINDOW_TITLE", "窗口标题", "窗口标题替换和启动窗口门控。"),
    Feature("CIALLOHOOK_FEATURE_SCREEN_CAPTURE_PROTECTION", "防截图/录屏", "基于 SetWindowDisplayAffinity 的截图保护。"),
    Feature("CIALLOHOOK_FEATURE_FILE_PATCH", "文件补丁/VFS", "补丁目录、自定义封包、文件伪装、目录重定向。"),
    Feature("CIALLOHOOK_FEATURE_CUSTOM_PAK", "CustomPak/资源包", "cpk/lpk/xp3 封包读取、LitePAK 解包和压缩解码支持。"),
    Feature("CIALLOHOOK_FEATURE_REGISTRY", "虚拟注册表", "进程内 .reg 虚拟注册表。"),
    Feature("CIALLOHOOK_FEATURE_REGISTRY_BOOTSTRAP", "注册表临时引导", "启动时写真实注册表，退出时回滚。"),
    Feature("CIALLOHOOK_FEATURE_CODEPAGE", "代码页转换", "MultiByteToWideChar / WideCharToMultiByte 代码页重定向。", True),
    Feature("CIALLOHOOK_FEATURE_LOCALE_EMULATOR", "Locale Emulator", "LE/LEP 转区重启和语言 API Hook。"),
    Feature("CIALLOHOOK_FEATURE_STARTUP_MESSAGE", "启动声明弹窗", "启动确认/免责声明弹窗。"),
    Feature("CIALLOHOOK_FEATURE_SPLASH_IMAGE", "启动图/WebM", "启动图片动画和 WebM 透明视频支持。"),
    Feature("CIALLOHOOK_FEATURE_SIGLUS_KEY_EXTRACT", "Siglus 密钥提取", "SiglusEngine XOR key 提取 Hook。"),
    Feature("CIALLOHOOK_FEATURE_ALICE_SYSTEM3X", "Alice System3.x", "Alice System3.x / System39 松散文件覆盖。"),
    Feature("CIALLOHOOK_FEATURE_RIO_SHIINA", "RioShiina", "RioShiina 资源覆盖、WARC 解包、注册表/DVD 辅助。"),
    Feature("CIALLOHOOK_FEATURE_BINARY_PATCH", "二进制补丁", "预入口/运行时二进制补丁和硬件断点触发。"),
    Feature("CIALLOHOOK_FEATURE_CODECRYPT_PATCH", "Release 代码加密补丁", "编译后加密 .lpksc 代码段；体积影响小，但更容易触发启发式误报。"),
    Feature("CIALLOHOOK_FEATURE_ENGINE_CACHE", "引擎缓存/Waffle", "MED / MAJIRO 字体缓存清理，以及 Waffle 文本崩溃修复。"),
    Feature("CIALLOHOOK_FEATURE_KRKR_PATCH", "Krkr Patch", "KRKR 补丁、Bootstrap 绕过和 Cxdec StorageMedia 桥接。"),
    Feature("CIALLOHOOK_FEATURE_PROXY_EXPORTS", "winmm/version 代理导出", "需要把 DLL 改名为 winmm.dll 或 version.dll 代理加载时启用；普通注入/Launcher 可关闭以减小 DLL。"),
]


SOURCE_ITEM_PROPS = {
    "CIALLOHOOK_FEATURE_BINARY_PATCH": "CialloHookFeatureBinaryPatch",
    "CIALLOHOOK_FEATURE_ALICE_SYSTEM3X": "CialloHookFeatureAliceSystem3x",
    "CIALLOHOOK_FEATURE_RIO_SHIINA": "CialloHookFeatureRioShiina",
    "CIALLOHOOK_FEATURE_CUSTOM_PAK": "CialloHookFeatureCustomPak",
    "CIALLOHOOK_FEATURE_CODECRYPT_PATCH": "CialloHookFeatureCodeCryptPatch",
    "CIALLOHOOK_FEATURE_ENGINE_CACHE": "CialloHookFeatureEngineCache",
    "CIALLOHOOK_FEATURE_KRKR_PATCH": "CialloHookFeatureKrkrPatch",
    "CIALLOHOOK_FEATURE_PROXY_EXPORTS": "CialloHookFeatureProxyExports",
}

FEATURE_LABELS = {feature.key: feature.label for feature in FEATURES}

FEATURE_DEPENDENCIES: dict[str, tuple[str, ...]] = {
    "CIALLOHOOK_FEATURE_CUSTOM_PAK": ("CIALLOHOOK_FEATURE_FILE_PATCH",),
}

FEATURE_DEPENDENTS: dict[str, tuple[str, ...]] = {}
for child_feature, parent_features in FEATURE_DEPENDENCIES.items():
    for parent_feature in parent_features:
        FEATURE_DEPENDENTS[parent_feature] = (*FEATURE_DEPENDENTS.get(parent_feature, ()), child_feature)

FEATURE_WARNINGS: tuple[dict[str, Any], ...] = (
    {
        "feature": "CIALLOHOOK_FEATURE_BINARY_PATCH",
        "missing_any": ("CIALLOHOOK_FEATURE_CUSTOM_PAK",),
        "detail": "binary_prefer_pak",
        "message": "PreferCustomPak 需要 CustomPak/资源包；关闭后只会使用普通补丁文件。",
    },
    {
        "feature": "CIALLOHOOK_FEATURE_ENGINE_CACHE",
        "missing_any": ("CIALLOHOOK_FEATURE_TEXT",),
        "detail": "waffle_patch_enable",
        "message": "Waffle 补丁依赖文本 Hook；关闭文本 Hook 后不会生效。",
    },
    {
        "feature": "CIALLOHOOK_FEATURE_SPLASH_IMAGE",
        "missing_any": ("CIALLOHOOK_FEATURE_CUSTOM_PAK",),
        "message": "封包内启动图/WebM 不可用；本地文件不受影响。",
    },
    {
        "feature": "CIALLOHOOK_FEATURE_LOCALE_EMULATOR",
        "missing_all": ("CIALLOHOOK_FEATURE_FILE_PATCH", "CIALLOHOOK_FEATURE_CUSTOM_PAK"),
        "message": "从补丁目录或封包查找 LE/LEP loader 的能力不可用；本地 loader 不受影响。",
    },
    {
        "feature": "CIALLOHOOK_FEATURE_REGISTRY",
        "missing_all": ("CIALLOHOOK_FEATURE_FILE_PATCH", "CIALLOHOOK_FEATURE_CUSTOM_PAK"),
        "message": "从补丁目录或封包查找 .reg 的能力不可用；本地 .reg 不受影响。",
    },
)


FONT_HOOKS = [
    "hookCreateFontA",
    "hookCreateFontIndirectA",
    "hookCreateFontW",
    "hookCreateFontIndirectW",
    "hookEnumFontFamiliesExA",
    "hookEnumFontFamiliesExW",
    "hookCreateFontIndirectExA",
    "hookCreateFontIndirectExW",
    "hookGetObjectA",
    "hookGetObjectW",
    "hookGetTextFaceA",
    "hookGetTextFaceW",
    "hookGetTextMetricsA",
    "hookGetTextMetricsW",
    "hookGetCharABCWidthsA",
    "hookGetCharABCWidthsW",
    "hookGetCharABCWidthsFloatA",
    "hookGetCharABCWidthsFloatW",
    "hookGetCharWidthA",
    "hookGetCharWidthW",
    "hookGetCharWidth32A",
    "hookGetCharWidth32W",
    "hookGetKerningPairsA",
    "hookGetKerningPairsW",
    "hookGetOutlineTextMetricsA",
    "hookGetOutlineTextMetricsW",
    "hookAddFontResourceA",
    "hookAddFontResourceW",
    "hookAddFontResourceExA",
    "hookAddFontMemResourceEx",
    "hookRemoveFontResourceA",
    "hookRemoveFontResourceW",
    "hookRemoveFontResourceExA",
    "hookRemoveFontMemResourceEx",
    "hookEnumFontsA",
    "hookEnumFontsW",
    "hookEnumFontFamiliesA",
    "hookEnumFontFamiliesW",
    "hookGetCharWidthFloatA",
    "hookGetCharWidthFloatW",
    "hookGetCharWidthI",
    "hookGetCharABCWidthsI",
    "hookGetTextExtentPointI",
    "hookGetTextExtentExPointI",
    "hookGetFontData",
    "hookGetFontLanguageInfo",
    "hookGetFontUnicodeRanges",
    "hookDWriteCreateFactory",
    "hookGdipCreateFontFamilyFromName",
    "hookGdipCreateFontFromLogfontW",
    "hookGdipCreateFontFromLogfontA",
    "hookGdipCreateFontFromHFONT",
    "hookGdipCreateFontFromDC",
    "hookGdipCreateFont",
    "hookGdipDrawString",
    "hookGdipDrawDriverString",
    "hookGdipMeasureString",
    "hookGdipMeasureCharacterRanges",
    "hookGdipMeasureDriverString",
    "hookLoadLibraryW",
    "hookLoadLibraryExW",
]


TEXT_HOOKS = [
    "hookTextOutA",
    "hookTextOutW",
    "hookExtTextOutA",
    "hookExtTextOutW",
    "hookDrawTextA",
    "hookDrawTextW",
    "hookDrawTextExA",
    "hookDrawTextExW",
    "hookPolyTextOutA",
    "hookPolyTextOutW",
    "hookTabbedTextOutA",
    "hookTabbedTextOutW",
    "hookGetTabbedTextExtentA",
    "hookGetTabbedTextExtentW",
    "hookGetTextExtentPoint32A",
    "hookGetTextExtentPoint32W",
    "hookGetTextExtentExPointA",
    "hookGetTextExtentExPointW",
    "hookGetTextExtentPointA",
    "hookGetTextExtentPointW",
    "hookGetCharacterPlacementA",
    "hookGetCharacterPlacementW",
    "hookGetGlyphIndicesA",
    "hookGetGlyphIndicesW",
    "hookGetGlyphOutlineA",
    "hookGetGlyphOutlineW",
    "hookMessageBoxA",
    "hookSetDlgItemTextA",
    "hookSendDlgItemMessageA",
    "hookSendDlgItemMessageW",
    "hookSendMessageA",
    "hookSendMessageW",
    "hookAppendMenuA",
    "hookModifyMenuA",
    "hookInsertMenuA",
    "hookInsertMenuItemA",
    "hookSetMenuItemInfoA",
    "hookMessageBoxIndirectA",
    "hookDrawThemeText",
    "hookDrawThemeTextEx",
    "hookDefWindowProcA",
    "hookDefWindowProcW",
    "hookDialogBoxParamA",
    "hookDialogBoxParamW",
    "hookCreateDialogParamA",
    "hookCreateDialogParamW",
    "hookDialogBoxIndirectParamA",
    "hookDialogBoxIndirectParamW",
    "hookCreateDialogIndirectParamA",
    "hookCreateDialogIndirectParamW",
    "hookPropertySheetA",
    "hookExitProcessGuard",
]


FONT_HOOK_DEFAULTS = {name: True for name in FONT_HOOKS}
FONT_HOOK_DEFAULTS.update(
    {
        "hookCreateFontW": False,
        "hookCreateFontIndirectW": False,
        "hookGdipCreateFontFromHFONT": False,
    }
)
TEXT_HOOK_DEFAULTS = {name: True for name in TEXT_HOOKS}
TEXT_HOOK_DEFAULTS.update({"hookPropertySheetA": False, "hookExitProcessGuard": False})


FIELDS: list[Field] = [
    Field("load_mode", "加载模式", "choice", "proxy", "loadMode.mode", options=("proxy", "loader")),
    Field("debug_enable", "启用调试日志", "bool", True, "debug.enable"),
    Field("debug_file", "写入日志文件", "bool", True, "debug.logToFile"),
    Field("debug_console", "打开控制台日志", "bool", True, "debug.logToConsole"),
    Field("startup_attach_mode", "附加时机", "choice", "immediate", "startupTiming.attachMode", options=("immediate", "delay", "entrypoint")),
    Field("startup_delay_ms", "延迟毫秒", "int", 0, "startupTiming.delayMs", 0, 30000),
    Field("startup_wait_gui", "等待 GUI 就绪", "bool", False, "startupTiming.waitForGuiReady"),
    Field("startup_window_gate", "启用启动窗口门控", "bool", False, "startupTiming.enableStartupWindowGate"),
    Field("font_charset", "强制 Charset", "hexint", "0x00", "font.charset"),
    Field("font_name", "字体/字体文件", "text", "SimHei", "font.font"),
    Field("font_name_override", "字体内部名覆盖", "text", "", "font.fontNameOverride"),
    Field("font_charset_spoof", "启用字符集伪装", "bool", True, "font.enableCharsetSpoof"),
    Field("font_spoof_from", "SpoofFromCharset", "hexint", "0x80", "font.spoofFromCharset"),
    Field("font_spoof_to", "SpoofToCharset", "hexint", "0x01", "font.spoofToCharset"),
    Field("font_unlock", "解锁字体枚举", "bool", False, "font.unlockFontSelection"),
    Field("font_verbose", "字体 Hook 详细日志", "bool", False, "font.fontHookVerboseLog"),
    Field("font_cnjp_enable", "启用日繁/异体字映射", "bool", False, "font.enableCnJpMap"),
    Field("font_cnjp_verbose", "映射详细日志", "bool", False, "font.cnJpMapVerboseLog"),
    Field("font_cnjp_json", "映射文件", "text", "subs_cn_jp.json", "font.cnJpMapJson"),
    Field("font_cnjp_read_encoding", "映射读取编码", "int", 932, "font.cnJpMapReadEncoding", 0, 65001),
    Field("font_height", "字体高度", "int", 0, "font.fontHeight", -4096, 4096),
    Field("font_width", "字体宽度", "int", 0, "font.fontWidth", -4096, 4096),
    Field("font_weight", "字重", "int", 0, "font.fontWeight", 0, 1000),
    Field("font_scale", "字号缩放", "float", 1.0, "font.fontScale", 0.05, 8.0),
    Field("font_spacing_scale", "字符间距缩放", "float", 1.0, "font.fontSpacingScale", 0.05, 8.0),
    Field("font_glyph_aspect", "字形宽高比", "float", 1.0, "font.glyphAspectRatio", 0.05, 8.0),
    Field("font_glyph_offset_x", "字形 X 偏移", "int", 0, "font.glyphOffsetX", -4096, 4096),
    Field("font_glyph_offset_y", "字形 Y 偏移", "int", 0, "font.glyphOffsetY", -4096, 4096),
    Field("font_metrics_left", "度量左偏移", "int", 0, "font.metricsOffsetLeft", -4096, 4096),
    Field("font_metrics_right", "度量右偏移", "int", 0, "font.metricsOffsetRight", -4096, 4096),
    Field("font_metrics_top", "度量上偏移", "int", 0, "font.metricsOffsetTop", -4096, 4096),
    Field("font_metrics_bottom", "度量下偏移", "int", 0, "font.metricsOffsetBottom", -4096, 4096),
    Field("text_encoding", "文本通用编码", "int", 0, "textReplace.encoding", 0, 65001),
    Field("text_read_encoding", "文本读取编码", "int", 936, "textReplace.readEncoding", 0, 65001),
    Field("text_write_encoding", "文本写回编码", "int", 936, "textReplace.writeEncoding", 0, 65001),
    Field("text_verbose", "文本替换详细日志", "bool", False, "textReplace.enableVerboseLog"),
    Field("title_mode", "标题模式", "int", 2, "windowTitle.titleMode", 0, 10),
    Field("title_encoding", "标题通用编码", "int", 0, "windowTitle.encoding", 0, 65001),
    Field("title_read_encoding", "标题读取编码", "int", 936, "windowTitle.readEncoding", 0, 65001),
    Field("title_write_encoding", "标题写回编码", "int", 936, "windowTitle.writeEncoding", 0, 65001),
    Field("title_verbose", "标题替换详细日志", "bool", False, "windowTitle.enableVerboseLog"),
    Field("capture_enable", "启用防截图/录屏", "bool", False, "screenCaptureProtection.enable"),
    Field("capture_mode", "保护模式", "choice", "exclude", "screenCaptureProtection.mode", options=("exclude", "monitor")),
    Field("capture_fallback", "失败时回退到 monitor", "bool", True, "screenCaptureProtection.fallbackToMonitor"),
    Field("capture_existing", "应用到已有窗口", "bool", True, "screenCaptureProtection.applyExistingWindows"),
    Field("capture_tool_windows", "保护工具窗口", "bool", False, "screenCaptureProtection.protectToolWindows"),
    Field("capture_owned_windows", "保护 owned 窗口", "bool", False, "screenCaptureProtection.protectOwnedWindows"),
    Field("capture_verbose", "防截图详细日志", "bool", False, "screenCaptureProtection.enableVerboseLog"),
    Field("file_patch_enable", "启用文件热补丁", "bool", True, "filePatch.enable"),
    Field("file_patch_log", "文件热补丁日志", "bool", False, "filePatch.enableLog"),
    Field("file_patch_debug", "文件热补丁调试模式", "bool", False, "filePatch.debugMode"),
    Field("file_custom_pak_enable", "启用自定义封包", "bool", True, "filePatch.customPakEnable"),
    Field("file_vfs_mode", "VFS 模式", "int", 1, "filePatch.vfsMode", 0, 3),
    Field("file_spoof_enable", "启用文件/目录欺骗", "bool", False, "fileSpoof.enable"),
    Field("file_spoof_log", "欺骗日志", "bool", False, "fileSpoof.enableLog"),
    Field("dir_redirect_enable", "启用目录重定向", "bool", False, "directoryRedirect.enable"),
    Field("dir_redirect_log", "目录重定向日志", "bool", False, "directoryRedirect.enableLog"),
    Field("registry_enable", "启用虚拟注册表", "bool", False, "registry.enable"),
    Field("registry_log", "虚拟注册表日志", "bool", False, "registry.enableLog"),
    Field("registry_bootstrap_enable", "启用真实注册表引导", "bool", False, "registryBootstrap.enable"),
    Field("registry_bootstrap_cleanup", "退出时清理", "bool", True, "registryBootstrap.cleanupOnExit"),
    Field("registry_bootstrap_log", "注册表引导日志", "bool", False, "registryBootstrap.enableLog"),
    Field("codepage_enable", "启用代码页转换", "bool", False, "codePage.enable"),
    Field("codepage_from", "源代码页", "int", 932, "codePage.fromCodePage", 0, 65001),
    Field("codepage_to", "目标代码页", "int", 936, "codePage.toCodePage", 0, 65001),
    Field("codepage_hook_mbtowc", "Hook MultiByteToWideChar", "bool", True, "codePage.hookMultiByteToWideChar"),
    Field("codepage_hook_wctomb", "Hook WideCharToMultiByte", "bool", True, "codePage.hookWideCharToMultiByte"),
    Field("locale_enable", "启用 Locale Emulator", "bool", False, "localeEmulator.enable"),
    Field("locale_acp", "AnsiCodePage", "int", 932, "localeEmulator.ansiCodePage", 0, 65001),
    Field("locale_oem", "OemCodePage", "int", 932, "localeEmulator.oemCodePage", 0, 65001),
    Field("locale_id", "LocaleID", "hexint", "0x411", "localeEmulator.localeID"),
    Field("locale_charset", "DefaultCharset", "int", 128, "localeEmulator.defaultCharset", 0, 255),
    Field("locale_hook_ui", "HookUILanguageAPI", "int", 0, "localeEmulator.hookUILanguageAPI", 0, 1),
    Field("locale_timezone", "Timezone", "text", "Tokyo Standard Time", "localeEmulator.timezone"),
    Field("startup_msg_enable", "启用启动声明", "bool", False, "startupMessage.enable"),
    Field("startup_msg_style", "声明样式", "int", 1, "startupMessage.style", 1, 2),
    Field("startup_msg_title", "声明标题", "text", "CialloHook", "startupMessage.title"),
    Field("startup_msg_author", "补丁作者", "text", "", "startupMessage.author"),
    Field("startup_msg_text", "声明正文", "multiline", "", "startupMessage.text"),
    Field("splash_enable", "启用启动图/WebM", "bool", False, "splashImage.enable"),
    Field("splash_file", "图片/视频文件", "text", "splash.png", "splashImage.imageFile"),
    Field("splash_width", "宽度", "int", 800, "splashImage.width", 1, 8192),
    Field("splash_height", "高度", "int", 600, "splashImage.height", 1, 8192),
    Field("splash_entry_effect", "入场效果", "int", 1, "splashImage.entryEffect", 0, 16),
    Field("splash_exit_effect", "退场效果", "int", 1, "splashImage.exitEffect", 0, 16),
    Field("splash_entry_ms", "入场毫秒", "int", 1200, "splashImage.entryMs", 0, 600000),
    Field("splash_hold_ms", "停留毫秒", "int", 1800, "splashImage.holdMs", 0, 600000),
    Field("splash_exit_ms", "退场毫秒", "int", 1500, "splashImage.exitMs", 0, 600000),
    Field("splash_duration_ms", "总时长毫秒", "int", 0, "splashImage.durationMs", 0, 600000),
    Field("splash_position", "显示位置", "int", 1, "splashImage.position", 0, 10),
    Field("splash_interaction", "交互模式", "int", 0, "splashImage.interactionMode", 0, 10),
    Field("siglus_enable", "启用 Siglus 密钥提取", "bool", False, "siglusKeyExtract.enable"),
    Field("siglus_gameexe", "Gameexe.dat 路径", "text", "Gameexe.dat", "siglusKeyExtract.gameexePath"),
    Field("siglus_output", "密钥输出路径", "text", "siglus_key.txt", "siglusKeyExtract.keyOutputPath"),
    Field("siglus_message", "完成后弹窗", "bool", True, "siglusKeyExtract.showMessageBox"),
    Field("siglus_debug", "Siglus 调试模式", "bool", False, "siglusKeyExtract.debugMode"),
    Field("alice_enable", "启用 Alice System3.x", "bool", False, "aliceSystem3x.enable"),
    Field("alice_log", "Alice 日志", "bool", False, "aliceSystem3x.enableLog"),
    Field("alice_exists", "Hook 存在性检查", "bool", False, "aliceSystem3x.hookExistsCheck"),
    Field("alice_max_size", "最大文件大小", "int", 268435456, "aliceSystem3x.maxFileSize", 1, 2147483647),
    Field("rio_enable", "启用 RioShiina", "bool", False, "rioShiina.enable"),
    Field("rio_mode", "Rio 模式", "int", 0, "rioShiina.mode", 0, 2),
    Field("rio_extract_dir", "解包输出目录", "text", "rio_extract", "rioShiina.extractOutputDir"),
    Field("rio_skip_invalid", "跳过非法文件名", "bool", True, "rioShiina.skipInvalidFileName"),
    Field("rio_log", "Rio 日志", "bool", False, "rioShiina.enableLog"),
    Field("rio_process_reg", "处理注册表资源", "bool", True, "rioShiina.processReg"),
    Field("rio_process_dvd", "处理 DVD 资源", "bool", False, "rioShiina.processDvd"),
    Field("rio_spec_dvd_size", "指定 DVD 文件大小", "uint64text", "0", "rioShiina.specDvdFileSize"),
    Field("engine_cache_med", "清理 MED 缓存", "bool", False, "engineCache.med"),
    Field("engine_cache_majiro", "清理 MAJIRO 缓存", "bool", False, "engineCache.majiro"),
    Field("krkr_patch_enable", "启用 KrkrPatch", "bool", False, "enginePatches.enableKrkrPatch"),
    Field("krkr_patch_verbose", "KrkrPatch 详细日志", "bool", False, "enginePatches.krkrPatchVerboseLog"),
    Field("krkr_bootstrap_bypass", "绕过 Krkr Bootstrap", "bool", False, "enginePatches.krkrBootstrapBypass"),
    Field("krkr_cxdec_bridge", "启用 Krkr Cxdec Bridge", "bool", False, "enginePatches.enableKrkrCxdecBridge"),
    Field("waffle_patch_enable", "启用 Waffle Patch", "bool", False, "enginePatches.enableWafflePatch"),
    Field("binary_enable", "启用二进制补丁", "bool", False, "binaryPatch.enable"),
    Field("binary_log", "二进制补丁日志", "bool", False, "binaryPatch.enableLog"),
    Field("binary_verify_old", "校验旧字节", "bool", True, "binaryPatch.verifyOldBytes"),
    Field("binary_fail_missing", "模块缺失则失败", "bool", False, "binaryPatch.failOnMissingModule"),
    Field("binary_fail_write", "写入失败则失败", "bool", False, "binaryPatch.failOnWriteError"),
    Field("binary_prefer_pak", "优先从 CustomPak 读取", "bool", False, "binaryPatch.preferCustomPak"),
    Field("binary_hwbp_enable", "启用硬件断点触发", "bool", False, "binaryPatch.enableHwbp"),
    Field("binary_hwbp_module", "HWBP 模块名", "text", "", "binaryPatch.hwbpModule"),
    Field("binary_hwbp_rva", "HWBP RVA", "hexint", "0", "binaryPatch.hwbpRva"),
    Field("launcher_target", "目标 EXE", "text", "game.exe", "launcher.targetExe"),
    Field("launcher_debug", "启动器调试模式", "bool", False, "launcher.debugMode"),
]

for hook_name in FONT_HOOKS:
    FIELDS.append(Field(f"font_{hook_name}", hook_name, "bool", FONT_HOOK_DEFAULTS[hook_name], f"font.{hook_name}"))

for hook_name in TEXT_HOOKS:
    FIELDS.append(Field(f"text_{hook_name}", hook_name, "bool", TEXT_HOOK_DEFAULTS[hook_name], f"textReplace.{hook_name}"))


TABLES: list[TableSpec] = [
    TableSpec("font_skip_fonts", "跳过字体", ("原始字体名",), (), "font.skipFonts", "wstring_list"),
    TableSpec("font_redirect_rules", "字体重定向规则", ("原始字体名", "目标字体/文件", "目标内部名覆盖"), (), "font.redirectRules", "font_redirect"),
    TableSpec("text_rules", "文本替换规则", ("原始文本/通配符", "替换文本"), (), "textReplace.rules", "pair_list"),
    TableSpec("title_rules", "窗口标题替换规则", ("原标题/通配符", "新标题"), (), "windowTitle.rules", "pair_list"),
    TableSpec("file_patch_folders", "补丁目录", ("目录",), (("patch",),), "filePatch.patchFolders", "wstring_list"),
    TableSpec("file_custom_paks", "自定义封包", ("封包文件",), (("patch.cpk",),), "filePatch.customPakFiles", "wstring_list"),
    TableSpec("file_spoof_files", "欺骗文件", ("文件路径",), (), "fileSpoof.spoofFiles", "wstring_list"),
    TableSpec("file_spoof_dirs", "欺骗目录", ("目录路径",), (), "fileSpoof.spoofDirectories", "wstring_list"),
    TableSpec("dir_redirect_rules", "目录重定向规则", ("原目录", "目标目录"), (), "directoryRedirect.rules", "pair_list"),
    TableSpec("registry_files", "虚拟注册表文件", ("REG 文件",), (), "registry.files", "wstring_list"),
    TableSpec("registry_bootstrap_rules", "真实注册表引导规则", ("根", "Key", "ValueName", "类型", "数据"), (), "registryBootstrap.rules", "registry_rules"),
    TableSpec("alice_patch_folders", "Alice 补丁目录", ("目录",), (("patch",),), "aliceSystem3x.patchFolders", "wstring_list"),
    TableSpec("rio_patch_names", "Rio 补丁名", ("补丁名",), (("unencrypted",),), "rioShiina.patchNames", "wstring_list"),
    TableSpec("rio_archives", "Rio 待解包归档", ("归档名",), (), "rioShiina.archivesToExtract", "wstring_list"),
    TableSpec("krkr_patch_names", "KrkrPatch 名称", ("补丁名",), (("unencrypted",),), "enginePatches.krkrPatchNames", "wstring_list"),
    TableSpec("binary_patch_files", "二进制补丁文件", ("1337 文件",), (), "binaryPatch.patchFiles", "wstring_list"),
    TableSpec("launcher_target_dlls", "启动器注入 DLL", ("DLL 名称",), (("CialloHook.dll",),), "launcher.targetDllNames", "wstring_list"),
]


FIELD_BY_KEY = {field.key: field for field in FIELDS}
TABLE_BY_KEY = {table.key: table for table in TABLES}


FIELD_HELP: dict[str, str] = {
    "load_mode": "proxy 适合 winmm/version 代理加载；loader 适合启动器注入。",
    "debug_enable": "开启后会输出 CialloHook 调试日志。",
    "debug_file": "写入日志文件，便于事后排查。",
    "debug_console": "启动控制台实时显示日志，部分游戏可能影响前台体验。",
    "startup_attach_mode": "immediate 立即安装 Hook；delay 延迟；entrypoint 尽量等入口点附近再安装。",
    "startup_delay_ms": "AttachMode=delay 时使用，单位毫秒。",
    "startup_wait_gui": "等待主窗口出现后再继续部分初始化。",
    "startup_window_gate": "需要窗口相关 Hook 配合，用于等待/识别启动窗口。",
    "font_charset": "常见值：0x86 简中，0x88 繁中，0x80 日文，0x00 不强制。",
    "font_name": "可填系统字体名或字体文件路径；留空则不做全局字体替换。",
    "font_name_override": "字体文件内部名识别不准时手动覆盖。",
    "font_charset_spoof": "只在原始字符集命中 SpoofFromCharset 时替换。",
    "font_spoof_from": "被替换的原始字符集。",
    "font_spoof_to": "替换后的目标字符集。",
    "font_unlock": "字体枚举时不过滤字体/字符集，可看到更多字体。",
    "font_verbose": "记录每次字体 API 命中，日志量较大。",
    "font_cnjp_enable": "读取映射文件，在字形查询/宽度/绘制阶段做字形映射。",
    "font_cnjp_verbose": "输出日繁/异体字映射命中详情。",
    "font_cnjp_json": "映射 JSON 路径，支持补丁目录和 CustomPak 查找。",
    "font_cnjp_read_encoding": "常见值：932 日文，936 简中，950 繁中，65001 UTF-8。",
    "font_height": "负数表示字符高度，0 表示不固定。",
    "font_width": "0 表示沿用原始宽度；大于 0 会强制固定宽度。",
    "font_weight": "0 不改，400 正常，700 粗体。",
    "font_scale": "字体缩放倍数；FontHeight 不为 0 时通常无效。",
    "font_spacing_scale": "只调整字符间距，不直接拉伸字形。",
    "font_glyph_aspect": "字形宽高比，>1 更宽，<1 更窄。",
    "font_glyph_offset_x": "字形左右平移，左负右正。",
    "font_glyph_offset_y": "字形上下平移，上正下负。",
    "font_metrics_left": "调整左侧度量留白。",
    "font_metrics_right": "调整右侧度量留白。",
    "font_metrics_top": "调整顶部度量空间。",
    "font_metrics_bottom": "调整底部度量空间。",
    "text_encoding": "通用编码；0 通常表示按读/写编码分别处理。",
    "text_read_encoding": "从游戏 API 参数读取文本时使用的代码页。",
    "text_write_encoding": "替换文本写回游戏 API 时使用的代码页。",
    "text_verbose": "输出文本替换命中详情，日志量可能很大。",
    "title_mode": "控制标题替换策略，通常保持默认即可。",
    "title_encoding": "标题通用编码；0 表示按读/写编码处理。",
    "title_read_encoding": "读取窗口标题时使用的代码页。",
    "title_write_encoding": "写回窗口标题时使用的代码页。",
    "title_verbose": "输出标题替换命中详情。",
    "capture_enable": "通过窗口显示亲和性减少截图/录屏捕获。",
    "capture_mode": "exclude 隐藏窗口内容；monitor 为兼容回退模式。",
    "capture_fallback": "exclude 失败时尝试 monitor。",
    "capture_existing": "启动后对已有窗口也应用保护。",
    "capture_tool_windows": "是否保护工具窗口。",
    "capture_owned_windows": "是否保护 owned 子窗口。",
    "capture_verbose": "输出防截图应用过程日志。",
    "file_patch_enable": "启用补丁目录文件覆盖。",
    "file_patch_log": "输出文件补丁命中日志。",
    "file_patch_debug": "输出更详细的文件补丁排查信息。",
    "file_custom_pak_enable": "启用 cpk/xp3/lpk 自定义封包读取。",
    "file_vfs_mode": "CustomPak 文件读取模式，默认 1 通常即可。",
    "file_spoof_enable": "让指定文件/目录在存在性检查中表现为存在。",
    "file_spoof_log": "输出欺骗命中日志。",
    "dir_redirect_enable": "把访问某个目录的请求转到另一个目录。",
    "dir_redirect_log": "输出目录重定向命中日志。",
    "registry_enable": "启用进程内虚拟注册表，不写真实注册表。",
    "registry_log": "输出虚拟注册表命中日志。",
    "registry_bootstrap_enable": "启动时写入真实注册表，适合必须读系统注册表的游戏。",
    "registry_bootstrap_cleanup": "退出时尝试清理引导写入的注册表项。",
    "registry_bootstrap_log": "输出真实注册表引导日志。",
    "codepage_enable": "重定向 MultiByte/WideChar 转换 API 的代码页。",
    "codepage_from": "需要被替换的源代码页。",
    "codepage_to": "替换后的目标代码页。",
    "codepage_hook_mbtowc": "Hook MultiByteToWideChar。",
    "codepage_hook_wctomb": "Hook WideCharToMultiByte。",
    "locale_enable": "启用 LE/LEP 转区辅助和语言环境 API Hook。",
    "locale_acp": "常见日文游戏为 932。",
    "locale_oem": "通常与 AnsiCodePage 保持一致。",
    "locale_id": "日文常用 0x411。",
    "locale_charset": "日文常用 128。",
    "locale_hook_ui": "是否 Hook UI 语言相关 API。",
    "locale_timezone": "Windows 时区名，例如 Tokyo Standard Time。",
    "startup_msg_enable": "启动时显示声明/确认弹窗。",
    "startup_msg_style": "控制声明弹窗样式。",
    "startup_msg_title": "声明窗口标题。",
    "startup_msg_author": "补丁作者展示文本。",
    "startup_msg_text": "声明正文，多行内容会写入 ini。",
    "splash_enable": "启动时显示图片或 WebM 动画。",
    "splash_file": "支持补丁目录、CustomPak 和游戏根目录查找。",
    "splash_width": "启动图窗口宽度。",
    "splash_height": "启动图窗口高度。",
    "splash_entry_effect": "入场动画编号。",
    "splash_exit_effect": "退场动画编号。",
    "splash_entry_ms": "入场动画时长，单位毫秒。",
    "splash_hold_ms": "中间停留时长，单位毫秒。",
    "splash_exit_ms": "退场动画时长，单位毫秒。",
    "splash_duration_ms": "总时长；0 表示按入场/停留/退场计算。",
    "splash_position": "启动图显示位置编号。",
    "splash_interaction": "启动图交互模式。",
    "siglus_enable": "启用 SiglusEngine 密钥提取 Hook。",
    "siglus_gameexe": "Gameexe.dat 路径。",
    "siglus_output": "提取到的 key 输出路径。",
    "siglus_message": "提取完成后显示弹窗。",
    "siglus_debug": "输出 Siglus 调试日志。",
    "alice_enable": "启用 Alice System3.x 松散文件覆盖。",
    "alice_log": "输出 Alice 资源命中日志。",
    "alice_exists": "Hook 文件存在性检查。",
    "alice_max_size": "允许覆盖读取的最大文件大小。",
    "rio_enable": "启用 RioShiina 资源覆盖或 WARC 解包支持。",
    "rio_mode": "0 关闭，1 资源覆盖，2 解包指定 WARC。",
    "rio_extract_dir": "Mode=2 时的输出目录。",
    "rio_skip_invalid": "跳过非法文件名，避免解包中断。",
    "rio_log": "输出 RioShiina 处理日志。",
    "rio_process_reg": "处理封包中的注册表资源。",
    "rio_process_dvd": "处理 DVD 资源。",
    "rio_spec_dvd_size": "手动指定 DVD 文件大小，0 表示自动。",
    "engine_cache_med": "清理/绕过 MED 字体缓存。",
    "engine_cache_majiro": "清理/绕过 MAJIRO 字体缓存。",
    "waffle_patch_enable": "启用 Waffle 文本相关兼容补丁。",
    "krkr_patch_enable": "启用 KRKR 补丁链处理。",
    "krkr_patch_verbose": "输出 KRKR 文件流详细日志。",
    "krkr_bootstrap_bypass": "尝试绕过 krkrz Bootstrap 完整性检查。",
    "krkr_cxdec_bridge": "让 cxdec 场景命中补丁目录/xp3/CustomPak。",
    "binary_enable": "启用 .1337 二进制补丁。",
    "binary_log": "输出二进制补丁日志。",
    "binary_verify_old": "写入前校验旧字节，降低误补风险。",
    "binary_fail_missing": "模块缺失时视为失败。",
    "binary_fail_write": "写入失败时视为失败。",
    "binary_prefer_pak": "同名本地文件和 CustomPak 条目都存在时优先 CustomPak。",
    "binary_hwbp_enable": "启用硬件断点触发补丁。",
    "binary_hwbp_module": "硬件断点目标模块名。",
    "binary_hwbp_rva": "硬件断点目标 RVA。",
    "launcher_target": "启动器要拉起的游戏 EXE。",
    "launcher_debug": "启用启动器调试输出。",
}

TABLE_HELP: dict[str, str] = {
    "font_skip_fonts": "命中这些原始字体名时不做字体 Hook。",
    "font_redirect_rules": "按原始字体名定向替换到指定字体。",
    "text_rules": "按文本或通配符替换。",
    "title_rules": "按窗口标题或通配符替换。",
    "file_patch_folders": "编号越后优先级越高。",
    "file_custom_paks": "支持 cpk/xp3/lpk，编号越后优先级越高。",
    "file_spoof_files": "让指定文件表现为存在。",
    "file_spoof_dirs": "让指定目录表现为存在。",
    "dir_redirect_rules": "把左侧原目录访问重定向到右侧目标目录。",
    "registry_files": "加载 .reg 作为进程内虚拟注册表。",
    "registry_bootstrap_rules": "启动时写真实注册表。",
    "alice_patch_folders": "Alice 资源覆盖目录。",
    "rio_patch_names": "RioShiina 资源覆盖补丁名。",
    "rio_archives": "Mode=2 时待解包 WARC，多个值会用 | 写入 ini。",
    "krkr_patch_names": "补丁名可对应目录或同名 xp3，后配置优先。",
    "binary_patch_files": ".1337 补丁文件路径，支持补丁目录和 CustomPak。",
    "launcher_target_dlls": "启动器注入的 DLL 名称列表。",
}

SECTION_HELP: dict[str, str] = {
    "字体 API": "这些开关控制字体创建、枚举、度量、资源加载、DWrite 和 GDI+ 相关 API 是否安装 Hook。排查兼容问题时可以逐项关闭。",
    "文本 API": "这些开关控制文本绘制、测量、窗口控件、菜单、对话框和 UI 文本相关 API 是否安装 Hook。排查兼容问题时可以逐项关闭。",
}


def help_for_field(field: Field) -> str:
    if field.key in FIELD_HELP:
        return FIELD_HELP[field.key]
    if field.key.startswith("font_hook"):
        return ""
    if field.key.startswith("text_hook"):
        return ""
    return f"默认值: {field.default}。"


def help_for_table(table: TableSpec) -> str:
    return TABLE_HELP.get(table.key, f"")


def make_default_state() -> dict[str, Any]:
    return {
        "profile": "Default",
        "source": "ini",
        "ini_override": "",
        "platform": PLATFORM_ALL,
        "configuration": "Release",
        "target": "all",
        "theme": THEME_SYSTEM,
        "show_help": True,
        "features": {feature.key: True for feature in FEATURES},
        "detail": {field.key: field.default for field in FIELDS},
        "tables": {table.key: [list(row) for row in table.default] for table in TABLES},
    }


def system_prefers_dark() -> bool:
    if sys.platform.startswith("win"):
        try:
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
            ) as key:
                return int(winreg.QueryValueEx(key, "AppsUseLightTheme")[0]) == 0
        except Exception:
            pass
    app = QApplication.instance()
    palette = app.palette() if app is not None else QPalette()
    return palette.color(QPalette.ColorRole.Window).lightness() < 128


def normalize_theme(value: str) -> str:
    return value if value in {THEME_SYSTEM, THEME_LIGHT, THEME_DARK} else THEME_SYSTEM


def build_platforms(platform: str) -> list[str]:
    return ["x86", "x64"] if platform in {PLATFORM_ALL, LEGACY_PLATFORM_ALL} else [platform]


def normalize_feature_dependencies(features: dict[str, bool]) -> dict[str, bool]:
    normalized = dict(features)
    changed = True
    while changed:
        changed = False
        for child_feature, parent_features in FEATURE_DEPENDENCIES.items():
            if not normalized.get(child_feature, False):
                continue
            for parent_feature in parent_features:
                if not normalized.get(parent_feature, False):
                    normalized[parent_feature] = True
                    changed = True
    return normalized


def merge_dict(base: dict[str, Any], extra: dict[str, Any]) -> None:
    for key, value in extra.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            merge_dict(base[key], value)
        else:
            base[key] = value


def read_defines() -> dict[str, str]:
    values: dict[str, str] = {}
    if not OPTIONS_PATH.exists():
        return values
    pattern = re.compile(r"^\s*#define\s+([A-Za-z0-9_]+)\s+(.+?)\s*$")
    for line in OPTIONS_PATH.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            values[match.group(1)] = match.group(2)
    return values


def from_cpp_wide_literal(value: str | None, default: str = "") -> str:
    if value is None:
        return default
    text = value.strip()
    if not text or text == "nullptr":
        return ""
    if text.startswith('L"') and text.endswith('"'):
        text = text[2:-1]
    elif text.startswith('"') and text.endswith('"'):
        text = text[1:-1]
    return (
        text.replace(r"\\", "\\")
        .replace(r"\"", '"')
        .replace(r"\n", "\n")
        .replace(r"\r", "\r")
        .replace(r"\t", "\t")
    )


def encode_value(value: Any) -> str:
    text = str(value)
    return text.replace("\\", r"\\").replace("\r", r"\r").replace("\n", r"\n").replace("\t", r"\t")


def decode_value(value: str) -> str:
    result: list[str] = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch == "\\" and i + 1 < len(value):
            nxt = value[i + 1]
            if nxt == "n":
                result.append("\n")
                i += 2
                continue
            if nxt == "r":
                result.append("\r")
                i += 2
                continue
            if nxt == "t":
                result.append("\t")
                i += 2
                continue
            if nxt == "\\":
                result.append("\\")
                i += 2
                continue
        result.append(ch)
        i += 1
    return "".join(result)


def encode_row(row: list[str]) -> str:
    return "\t".join(encode_value(cell) for cell in row)


def decode_row(value: str, width: int) -> list[str]:
    cells = [decode_value(cell) for cell in value.split("\t")]
    while len(cells) < width:
        cells.append("")
    return cells[:width]


def parse_bool_text(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on", "启用", "是"}


def coerce_field_value(field: Field, value: str) -> Any:
    if field.kind == "bool":
        return parse_bool_text(value)
    if field.kind == "int":
        return parse_int(value, int(field.default))
    if field.kind == "float":
        try:
            return float(value)
        except Exception:
            return float(field.default)
    return decode_value(value)


def load_state() -> dict[str, Any]:
    state = make_default_state()
    defines = read_defines()
    if defines:
        state["profile"] = from_cpp_wide_literal(defines.get("CIALLOHOOK_BUILD_PROFILE_NAME"), "Default")
        state["source"] = "builtin" if defines.get("CIALLOHOOK_CONFIG_SOURCE_BUILTIN", "0") == "1" else "ini"
        state["ini_override"] = from_cpp_wide_literal(defines.get("CIALLOHOOK_CONFIG_INI_OVERRIDE"), "")
        for feature in FEATURES:
            if feature.key in defines:
                state["features"][feature.key] = defines[feature.key].strip() != "0"

    if STATE_PATH.exists():
        parser = configparser.ConfigParser()
        parser.optionxform = str
        parser.read(STATE_PATH, encoding="utf-8")
        if parser.has_section("构建"):
            for key in ["profile", "source", "ini_override", "platform", "configuration", "target", "theme"]:
                if parser.has_option("构建", key):
                    state[key] = decode_value(parser.get("构建", key))
            if parser.has_option("构建", "show_help"):
                state["show_help"] = parser.getboolean("构建", "show_help")
        if parser.has_section("功能"):
            for feature in FEATURES:
                if parser.has_option("功能", feature.key):
                    state["features"][feature.key] = parser.getboolean("功能", feature.key)
        if parser.has_section("细节"):
            for field in FIELDS:
                if parser.has_option("细节", field.key):
                    state["detail"][field.key] = coerce_field_value(field, parser.get("细节", field.key))
        for table in TABLES:
            section = f"表格:{table.key}"
            if parser.has_section(section):
                rows: list[list[str]] = []
                for option in sorted(parser.options(section)):
                    if option.startswith("row_"):
                        rows.append(decode_row(parser.get(section, option), len(table.columns)))
                state["tables"][table.key] = rows
    if state["platform"] == LEGACY_PLATFORM_ALL:
        state["platform"] = PLATFORM_ALL
    state["theme"] = normalize_theme(str(state.get("theme", THEME_SYSTEM)))
    return state


def save_state_ini(state: dict[str, Any]) -> None:
    parser = configparser.ConfigParser()
    parser.optionxform = str
    parser["构建"] = {
        "profile": encode_value(state["profile"]),
        "source": encode_value(state["source"]),
        "ini_override": encode_value(state["ini_override"]),
        "platform": encode_value(state["platform"]),
        "configuration": encode_value(state["configuration"]),
        "target": encode_value(state["target"]),
        "theme": encode_value(state["theme"]),
        "show_help": "true" if bool(state.get("show_help", True)) else "false",
    }
    parser["功能"] = {key: "true" if value else "false" for key, value in state["features"].items()}
    parser["细节"] = {key: encode_value(value) for key, value in state["detail"].items()}
    for table in TABLES:
        section = f"表格:{table.key}"
        parser[section] = {}
        for index, row in enumerate(state["tables"].get(table.key, [])):
            parser[section][f"row_{index:04d}"] = encode_row([str(cell) for cell in row])
    with STATE_PATH.open("w", encoding="utf-8") as file:
        parser.write(file)


def cpp_w(value: str, allow_null: bool = False) -> str:
    if allow_null and not str(value).strip():
        return "nullptr"
    escaped = (
        str(value)
        .replace("\\", r"\\")
        .replace('"', r"\"")
        .replace("\r", r"\r")
        .replace("\n", r"\n")
        .replace("\t", r"\t")
    )
    return f'L"{escaped}"'


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def parse_int(value: str | int | float, default: int = 0) -> int:
    try:
        if isinstance(value, bool):
            return 1 if value else 0
        if isinstance(value, int):
            return value
        if isinstance(value, float):
            return int(value)
        text = str(value).strip()
        return int(text, 0) if text else default
    except Exception:
        return default


def cpp_u32(value: str | int, default: int = 0) -> str:
    return str(parse_int(value, default))


def cpp_u64(value: str | int, default: int = 0) -> str:
    return f"{max(0, parse_int(value, default))}ULL"


def cpp_float(value: str | float, default: float = 1.0) -> str:
    try:
        parsed = float(value)
    except Exception:
        parsed = default
    return f"{parsed:.3f}f"


def clean_rows(rows: list[list[str]]) -> list[list[str]]:
    result: list[list[str]] = []
    for row in rows:
        values = [str(cell).strip() for cell in row]
        if any(values):
            result.append(values)
    return result


def cpp_w_list_rows(rows: list[list[str]]) -> str:
    values = [row[0] for row in clean_rows(rows) if row and row[0]]
    if not values:
        return "{}"
    return "{ " + ", ".join(cpp_w(value) for value in values) + " }"


def cpp_pair_rows(rows: list[list[str]]) -> str:
    pairs = [row for row in clean_rows(rows) if len(row) >= 2 and (row[0] or row[1])]
    if not pairs:
        return "{}"
    return "{ " + ", ".join(f"std::make_pair({cpp_w(row[0])}, {cpp_w(row[1])})" for row in pairs) + " }"


def cpp_font_redirect_rows(rows: list[list[str]]) -> str:
    values = [row for row in clean_rows(rows) if len(row) >= 2 and (row[0] or row[1] or (len(row) > 2 and row[2]))]
    if not values:
        return "{}"
    return "{ " + ", ".join(
        f"FontRedirectRule{{ {cpp_w(row[0])}, {cpp_w(row[1])}, {cpp_w(row[2] if len(row) > 2 else '')} }}"
        for row in values
    ) + " }"


def cpp_registry_rows(rows: list[list[str]]) -> str:
    values = [row for row in clean_rows(rows) if len(row) >= 5 and any(row[:5])]
    if not values:
        return "{}"
    return "{ " + ", ".join(
        "RegistryBootstrapRule{ "
        + ", ".join(cpp_w(row[index] if index < len(row) else "") for index in range(5))
        + " }"
        for row in values
    ) + " }"


def table_cpp_value(table: TableSpec, rows: list[list[str]]) -> str:
    if table.cpp_kind == "wstring_list":
        return cpp_w_list_rows(rows)
    if table.cpp_kind == "pair_list":
        return cpp_pair_rows(rows)
    if table.cpp_kind == "font_redirect":
        return cpp_font_redirect_rows(rows)
    if table.cpp_kind == "registry_rules":
        return cpp_registry_rows(rows)
    return "{}"


def detail_cpp_value(field: Field, detail: dict[str, Any]) -> str:
    value = detail[field.key]
    if field.kind in {"text", "choice", "multiline"}:
        return cpp_w(str(value))
    if field.kind == "bool":
        return cpp_bool(bool(value))
    if field.kind in {"int", "hexint"}:
        return cpp_u32(value, parse_int(field.default))
    if field.kind == "uint64text":
        return cpp_u64(value, parse_int(field.default))
    if field.kind == "float":
        return cpp_float(value, float(field.default))
    return cpp_w(str(value))


def write_options(state: dict[str, Any], feature_values: dict[str, bool]) -> None:
    lines = [
        "#pragma once",
        "",
        "// Generated by tools/build_gui.py.",
        "// Disabled features are not applied at runtime; Release /OPT:REF can discard more code.",
        "",
        f"#define CIALLOHOOK_BUILD_PROFILE_NAME {cpp_w(state['profile'])}",
        "",
        "// 0 = read ini files, 1 = use ApplyBuiltInConfig / ApplyBuiltInLauncherConfig.",
        f"#define CIALLOHOOK_CONFIG_SOURCE_BUILTIN {1 if state['source'] == 'builtin' else 0}",
        "",
        '// nullptr = normal lookup. Use a wide literal such as L".\\\\MyConfig.ini" to force an ini path.',
        f"#define CIALLOHOOK_CONFIG_INI_OVERRIDE {cpp_w(state['ini_override'], allow_null=True)}",
        "",
    ]
    for feature in FEATURES:
        lines.append(f"#define {feature.key} {1 if feature_values.get(feature.key, True) else 0}")
    OPTIONS_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_props(feature_values: dict[str, bool]) -> None:
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">',
        "  <!-- Generated by tools/build_gui.py. Values are used by MSBuild item conditions. -->",
        "  <PropertyGroup>",
    ]
    for feature_key, prop_name in SOURCE_ITEM_PROPS.items():
        lines.append(f"    <{prop_name}>{'true' if feature_values.get(feature_key, True) else 'false'}</{prop_name}>")
    lines.extend(["  </PropertyGroup>", "</Project>"])
    PROPS_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_overrides(detail: dict[str, Any], tables: dict[str, list[list[str]]], features: dict[str, bool]) -> None:
    app_lines: list[str] = []
    launcher_lines: list[str] = []
    has_feature = lambda key: feature_enabled(features, key)

    for field in FIELDS:
        if field.path.startswith("launcher."):
            continue
        app_lines.append(f"\t\tsettings.{field.path} = {detail_cpp_value(field, detail)};")
    for table in TABLES:
        if table.path.startswith("launcher."):
            continue
        app_lines.append(f"\t\tsettings.{table.path} = {table_cpp_value(table, tables.get(table.key, []))};")

    launcher_regular_fields = [
        field for field in FIELDS
        if field.path.startswith("launcher.") and "localeEmulatorBlock" not in field.path
    ]
    for field in launcher_regular_fields:
        attr = field.path.removeprefix("launcher.")
        launcher_lines.append(f"\t\tconfig.{attr} = {detail_cpp_value(field, detail)};")

    launcher_lines.extend(
        [
            "\t\tconfig.targetDllNames = " + table_cpp_value(TABLE_BY_KEY["launcher_target_dlls"], tables.get("launcher_target_dlls", [])) + ";",
            "\t\tconfig.targetDllCount = static_cast<uint32_t>(config.targetDllNames.size());",
            f"\t\tconfig.logToFile = {cpp_bool(bool(detail['debug_file']))};",
            f"\t\tconfig.logToConsole = {cpp_bool(bool(detail['debug_console']))};",
            f"\t\tconfig.filePatchEnable = {cpp_bool(has_feature('CIALLOHOOK_FEATURE_FILE_PATCH') and bool(detail['file_patch_enable']))};",
            "\t\tconfig.patchFolders = " + table_cpp_value(TABLE_BY_KEY["file_patch_folders"], tables.get("file_patch_folders", [])) + ";",
            f"\t\tconfig.customPakEnable = {cpp_bool(has_feature('CIALLOHOOK_FEATURE_FILE_PATCH') and has_feature('CIALLOHOOK_FEATURE_CUSTOM_PAK') and bool(detail['file_custom_pak_enable']))};",
            "\t\tconfig.customPakFiles = " + table_cpp_value(TABLE_BY_KEY["file_custom_paks"], tables.get("file_custom_paks", [])) + ";",
            f"\t\tconfig.startupMessage.enable = {cpp_bool(has_feature('CIALLOHOOK_FEATURE_STARTUP_MESSAGE') and bool(detail['startup_msg_enable']))};",
            f"\t\tconfig.startupMessage.title = {cpp_w(str(detail['startup_msg_title']))};",
            f"\t\tconfig.startupMessage.body = {cpp_w(str(detail['startup_msg_text']))};",
            f"\t\tconfig.enableLocaleEmulator = {cpp_bool(has_feature('CIALLOHOOK_FEATURE_LOCALE_EMULATOR') and bool(detail['locale_enable']))};",
            "\t\tconfig.localeEmulatorBlock = {};",
            f"\t\tconfig.localeEmulatorBlock.AnsiCodePage = {cpp_u32(detail['locale_acp'], 932)};",
            f"\t\tconfig.localeEmulatorBlock.OemCodePage = {cpp_u32(detail['locale_oem'], 932)};",
            f"\t\tconfig.localeEmulatorBlock.LocaleID = {cpp_u32(detail['locale_id'], 0x411)};",
            f"\t\tconfig.localeEmulatorBlock.DefaultCharset = {cpp_u32(detail['locale_charset'], 128)};",
            f"\t\tconfig.localeEmulatorBlock.HookUILanguageAPI = {cpp_u32(detail['locale_hook_ui'], 0)};",
            "\t\tmemset(config.localeEmulatorBlock.DefaultFaceName, 0, sizeof(config.localeEmulatorBlock.DefaultFaceName));",
            f"\t\twcsncpy_s(reinterpret_cast<wchar_t*>(config.localeEmulatorBlock.Timezone.StandardName), 32, {cpp_w(str(detail['locale_timezone']))}, _TRUNCATE);",
            f"\t\twcsncpy_s(reinterpret_cast<wchar_t*>(config.localeEmulatorBlock.Timezone.DaylightName), 32, {cpp_w(str(detail['locale_timezone']))}, _TRUNCATE);",
        ]
    )

    content = f"""#pragma once

// Generated by tools/build_gui.py.
// This file overrides all GUI-controlled built-in settings after
// ApplyBuiltInConfig and ApplyBuiltInLauncherConfig have filled their defaults.

#include <cstring>
#include <cwchar>
#include <utility>

namespace CialloHook
{{
\tinline void ApplyGuiBuiltInConfigOverrides(AppSettings& settings)
\t{{
{chr(10).join(app_lines)}
\t}}

\tinline void ApplyGuiBuiltInLauncherConfigOverrides(CialloLauncher::LauncherConfig& config)
\t{{
{chr(10).join(launcher_lines)}
\t}}
}}
"""
    OVERRIDES_PATH.write_text(content, encoding="utf-8")


def ini_bool(value: Any) -> str:
    return "true" if bool(value) else "false"


def ini_text(value: Any, escape_lines: bool = False) -> str:
    text = str(value)
    if escape_lines:
        text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", r"\n")
    return text


def ini_int(value: Any, default: int = 0) -> str:
    return str(parse_int(value, default))


def ini_value_for_field(detail: dict[str, Any], key: str) -> str:
    field = FIELD_BY_KEY[key]
    value = detail[key]
    if field.kind == "bool":
        return ini_bool(value)
    if field.kind == "hexint":
        return str(value).strip() or str(field.default)
    if field.kind in {"int", "uint64text"}:
        return ini_int(value, parse_int(field.default))
    if field.kind == "float":
        return f"{float(value):.3f}".rstrip("0").rstrip(".")
    return ini_text(value, field.kind == "multiline")


def rows_for(tables: dict[str, list[list[str]]], key: str) -> list[list[str]]:
    return clean_rows(tables.get(key, []))


def append_section(lines: list[str], name: str) -> None:
    if lines and lines[-1] != "":
        lines.append("")
    lines.append(f"[{name}]")


def append_kv(lines: list[str], key: str, value: Any) -> None:
    lines.append(f"{key} = {value}")


def append_list(lines: list[str], rows: list[list[str]], count_key: str, item_prefix: str) -> None:
    values = [row[0] for row in rows if row and row[0].strip()]
    append_kv(lines, count_key, len(values))
    for index, value in enumerate(values):
        append_kv(lines, f"{item_prefix}{index}", value)


def append_pair_rules(lines: list[str], rows: list[list[str]], count_key: str, left_prefix: str, right_prefix: str) -> None:
    pairs = [row for row in rows if len(row) >= 2 and (row[0].strip() or row[1].strip())]
    append_kv(lines, count_key, len(pairs))
    for index, row in enumerate(pairs):
        append_kv(lines, f"{left_prefix}{index}", row[0])
        append_kv(lines, f"{right_prefix}{index}", row[1])


def append_font_redirect(lines: list[str], rows: list[list[str]]) -> None:
    rules = [row for row in rows if len(row) >= 2 and any(cell.strip() for cell in row)]
    append_kv(lines, "RedirectFontCount", len(rules))
    for index, row in enumerate(rules):
        append_kv(lines, f"RedirectFromFont_{index}", row[0])
        append_kv(lines, f"RedirectToFont_{index}", row[1])
        if len(row) > 2 and row[2].strip():
            append_kv(lines, f"RedirectToFontName_{index}", row[2])


def append_registry_bootstrap(lines: list[str], rows: list[list[str]]) -> None:
    rules = [row for row in rows if len(row) >= 5 and any(cell.strip() for cell in row[:5])]
    append_kv(lines, "RuleCount", len(rules))
    for index, row in enumerate(rules):
        append_kv(lines, f"Root_{index}", row[0] or "HKCU")
        append_kv(lines, f"Key_{index}", row[1])
        append_kv(lines, f"ValueName_{index}", row[2])
        append_kv(lines, f"Type_{index}", row[3] or "SZ")
        append_kv(lines, f"Data_{index}", row[4])


def append_pipe_list(lines: list[str], key: str, rows: list[list[str]]) -> None:
    values = [row[0] for row in rows if row and row[0].strip()]
    append_kv(lines, key, "|".join(values))


def output_dir_for_platform(state: dict[str, Any], platform: str) -> Path:
    return ROOT / "out" / "bin" / platform / state["configuration"]


def output_dir_for_state(state: dict[str, Any]) -> Path:
    return output_dir_for_platform(state, build_platforms(state["platform"])[0])


def output_ini_paths_for_platform(state: dict[str, Any], platform: str) -> dict[str, Path]:
    output_dir = output_dir_for_platform(state, platform)
    return {
        "CialloHook.ini": output_dir / "CialloHook.ini",
        "winmm.ini": output_dir / "winmm.ini",
        "version.ini": output_dir / "version.ini",
        "CialloLauncher.ini": output_dir / "CialloLauncher.ini",
    }


def output_ini_paths(state: dict[str, Any]) -> dict[str, Path]:
    return output_ini_paths_for_platform(state, build_platforms(state["platform"])[0])


def feature_enabled(features: dict[str, bool], key: str) -> bool:
    return bool(features.get(key, True))


def build_ciallohook_ini_text(detail: dict[str, Any], tables: dict[str, list[list[str]]], features: dict[str, bool]) -> str:
    lines: list[str] = [
        "# Generated by tools/build_gui.py",
        "# UTF-8",
        "",
    ]
    has_feature = lambda key: feature_enabled(features, key)

    if has_feature("CIALLOHOOK_FEATURE_FONT"):
        append_section(lines, "CialloHook")
        for ini_key, detail_key in [
            ("Charset", "font_charset"),
            ("Font", "font_name"),
            ("FontName", "font_name_override"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_list(lines, rows_for(tables, "font_skip_fonts"), "SkipFontCount", "SkipFontName_")
        append_font_redirect(lines, rows_for(tables, "font_redirect_rules"))
        for ini_key, detail_key in [
            ("EnableCharsetSpoof", "font_charset_spoof"),
            ("SpoofFromCharset", "font_spoof_from"),
            ("SpoofToCharset", "font_spoof_to"),
            ("EnableCnJpMap", "font_cnjp_enable"),
            ("CnJpMapVerboseLog", "font_cnjp_verbose"),
            ("CnJpMapJson", "font_cnjp_json"),
            ("CnJpMapReadEncoding", "font_cnjp_read_encoding"),
            ("UnlockFontSelection", "font_unlock"),
            ("FontHookVerboseLog", "font_verbose"),
            ("FontHeight", "font_height"),
            ("FontWidth", "font_width"),
            ("FontScale", "font_scale"),
            ("FontSpacingScale", "font_spacing_scale"),
            ("FontWeight", "font_weight"),
            ("GlyphAspectRatio", "font_glyph_aspect"),
            ("GlyphOffsetX", "font_glyph_offset_x"),
            ("GlyphOffsetY", "font_glyph_offset_y"),
            ("MetricsOffsetLeft", "font_metrics_left"),
            ("MetricsOffsetRight", "font_metrics_right"),
            ("MetricsOffsetTop", "font_metrics_top"),
            ("MetricsOffsetBottom", "font_metrics_bottom"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        for ini_key in [
            "HookGroupCreate",
            "HookGroupEnumerate",
            "HookGroupMetrics",
            "HookGroupResource",
            "HookGroupModern",
            "HookGroupLateLoad",
        ]:
            append_kv(lines, ini_key, "true")
        for hook_name in FONT_HOOKS:
            append_kv(lines, hook_name[0].upper() + hook_name[1:], ini_bool(detail[f"font_{hook_name}"]))

    if has_feature("CIALLOHOOK_FEATURE_CODEPAGE"):
        append_section(lines, "CodePage")
        for ini_key, detail_key in [
            ("Enable", "codepage_enable"),
            ("FromCodePage", "codepage_from"),
            ("ToCodePage", "codepage_to"),
            ("HookMultiByteToWideChar", "codepage_hook_mbtowc"),
            ("HookWideCharToMultiByte", "codepage_hook_wctomb"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_TEXT"):
        append_section(lines, "TextReplace")
        for ini_key, detail_key in [
            ("Encoding", "text_encoding"),
            ("ReadEncoding", "text_read_encoding"),
            ("WriteEncoding", "text_write_encoding"),
            ("EnableVerboseLog", "text_verbose"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_pair_rules(lines, rows_for(tables, "text_rules"), "ReplaceCount", "Original_", "Replacement_")
        for hook_name in TEXT_HOOKS:
            append_kv(lines, hook_name[0].upper() + hook_name[1:], ini_bool(detail[f"text_{hook_name}"]))

    if has_feature("CIALLOHOOK_FEATURE_WINDOW_TITLE"):
        append_section(lines, "WindowTitle")
        for ini_key, detail_key in [
            ("TitleMode", "title_mode"),
            ("Encoding", "title_encoding"),
            ("ReadEncoding", "title_read_encoding"),
            ("WriteEncoding", "title_write_encoding"),
            ("EnableVerboseLog", "title_verbose"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_pair_rules(lines, rows_for(tables, "title_rules"), "ReplaceCount", "Original_", "New_")

    if has_feature("CIALLOHOOK_FEATURE_SCREEN_CAPTURE_PROTECTION"):
        append_section(lines, "ScreenCaptureProtection")
        for ini_key, detail_key in [
            ("Enable", "capture_enable"),
            ("Mode", "capture_mode"),
            ("FallbackToMonitor", "capture_fallback"),
            ("ApplyExistingWindows", "capture_existing"),
            ("ProtectToolWindows", "capture_tool_windows"),
            ("ProtectOwnedWindows", "capture_owned_windows"),
            ("EnableVerboseLog", "capture_verbose"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_FILE_PATCH"):
        append_section(lines, "FilePatch")
        for ini_key, detail_key in [
            ("Enable", "file_patch_enable"),
            ("EnableLog", "file_patch_log"),
            ("DebugMode", "file_patch_debug"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        if has_feature("CIALLOHOOK_FEATURE_CUSTOM_PAK"):
            append_kv(lines, "CustomPakEnable", ini_value_for_field(detail, "file_custom_pak_enable"))
            append_kv(lines, "VFSMode", ini_value_for_field(detail, "file_vfs_mode"))
        append_list(lines, rows_for(tables, "file_patch_folders"), "PatchFolderCount", "PatchFolderName_")
        if has_feature("CIALLOHOOK_FEATURE_CUSTOM_PAK"):
            append_list(lines, rows_for(tables, "file_custom_paks"), "CustomPakCount", "CustomPakName_")

        append_section(lines, "FileSpoof")
        append_kv(lines, "Enable", ini_value_for_field(detail, "file_spoof_enable"))
        append_kv(lines, "EnableLog", ini_value_for_field(detail, "file_spoof_log"))
        append_list(lines, rows_for(tables, "file_spoof_files"), "SpoofFileCount", "SpoofFileName_")
        append_list(lines, rows_for(tables, "file_spoof_dirs"), "SpoofDirectoryCount", "SpoofDirectoryName_")

        append_section(lines, "DirectoryRedirect")
        append_kv(lines, "Enable", ini_value_for_field(detail, "dir_redirect_enable"))
        append_kv(lines, "EnableLog", ini_value_for_field(detail, "dir_redirect_log"))
        redirect_rows = rows_for(tables, "dir_redirect_rules")
        append_kv(lines, "RuleCount", len(redirect_rows))
        for index, row in enumerate(redirect_rows):
            append_kv(lines, f"Rule_{index}", f"{row[0]}:{row[1] if len(row) > 1 else ''}")

    if has_feature("CIALLOHOOK_FEATURE_REGISTRY"):
        append_section(lines, "Registry")
        append_kv(lines, "Enable", ini_value_for_field(detail, "registry_enable"))
        append_kv(lines, "EnableLog", ini_value_for_field(detail, "registry_log"))
        append_list(lines, rows_for(tables, "registry_files"), "FileCount", "FileName_")

    if has_feature("CIALLOHOOK_FEATURE_REGISTRY_BOOTSTRAP"):
        append_section(lines, "RegistryBootstrap")
        append_kv(lines, "Enable", ini_value_for_field(detail, "registry_bootstrap_enable"))
        append_kv(lines, "CleanupOnExit", ini_value_for_field(detail, "registry_bootstrap_cleanup"))
        append_kv(lines, "EnableLog", ini_value_for_field(detail, "registry_bootstrap_log"))
        append_registry_bootstrap(lines, rows_for(tables, "registry_bootstrap_rules"))

    if has_feature("CIALLOHOOK_FEATURE_BINARY_PATCH"):
        append_section(lines, "BinaryPatch")
        for ini_key, detail_key in [
            ("Enable", "binary_enable"),
            ("EnableLog", "binary_log"),
            ("VerifyOldBytes", "binary_verify_old"),
            ("FailOnMissingModule", "binary_fail_missing"),
            ("FailOnWriteError", "binary_fail_write"),
            ("PreferCustomPak", "binary_prefer_pak"),
            ("EnableHWBP", "binary_hwbp_enable"),
            ("HWBPModule", "binary_hwbp_module"),
            ("HWBPRVA", "binary_hwbp_rva"),
        ]:
            if ini_key == "PreferCustomPak" and not has_feature("CIALLOHOOK_FEATURE_CUSTOM_PAK"):
                continue
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_list(lines, rows_for(tables, "binary_patch_files"), "PatchFileCount", "PatchFileName_")

    if has_feature("CIALLOHOOK_FEATURE_SIGLUS_KEY_EXTRACT"):
        append_section(lines, "SiglusKeyExtract")
        for ini_key, detail_key in [
            ("Enable", "siglus_enable"),
            ("GameexePath", "siglus_gameexe"),
            ("KeyOutputPath", "siglus_output"),
            ("ShowMessageBox", "siglus_message"),
            ("DebugMode", "siglus_debug"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_ALICE_SYSTEM3X"):
        append_section(lines, "AliceSystem3x")
        for ini_key, detail_key in [
            ("Enable", "alice_enable"),
            ("EnableLog", "alice_log"),
            ("HookExistsCheck", "alice_exists"),
            ("MaxFileSize", "alice_max_size"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_list(lines, rows_for(tables, "alice_patch_folders"), "PatchFolderCount", "PatchFolderName_")

    if has_feature("CIALLOHOOK_FEATURE_RIO_SHIINA"):
        append_section(lines, "RioShiina")
        for ini_key, detail_key in [
            ("Enable", "rio_enable"),
            ("Mode", "rio_mode"),
            ("ExtractOutputDir", "rio_extract_dir"),
            ("SkipInvalidFileName", "rio_skip_invalid"),
            ("ProcessReg", "rio_process_reg"),
            ("ProcessDvd", "rio_process_dvd"),
            ("SpecDvdFileSize", "rio_spec_dvd_size"),
            ("EnableLog", "rio_log"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_list(lines, rows_for(tables, "rio_patch_names"), "PatchCount", "PatchName_")
        append_pipe_list(lines, "ArchivesToExtract", rows_for(tables, "rio_archives"))

    if has_feature("CIALLOHOOK_FEATURE_ENGINE_CACHE"):
        append_section(lines, "GLOBAL")
        for ini_key, detail_key in [
            ("MED", "engine_cache_med"),
            ("MAJIRO", "engine_cache_majiro"),
            ("EnableWafflePatch", "waffle_patch_enable"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_KRKR_PATCH"):
        append_section(lines, "Krkr")
        for ini_key, detail_key in [
            ("EnableKrkrPatch", "krkr_patch_enable"),
            ("KrkrPatchVerboseLog", "krkr_patch_verbose"),
            ("KrkrBootstrapBypass", "krkr_bootstrap_bypass"),
            ("EnableKrkrCxdecBridge", "krkr_cxdec_bridge"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))
        append_list(lines, rows_for(tables, "krkr_patch_names"), "KrkrPatchCount", "KrkrPatchName_")

    if has_feature("CIALLOHOOK_FEATURE_LOCALE_EMULATOR"):
        append_section(lines, "LocaleEmulator")
        for ini_key, detail_key in [
            ("Enable", "locale_enable"),
            ("AnsiCodePage", "locale_acp"),
            ("OemCodePage", "locale_oem"),
            ("LocaleID", "locale_id"),
            ("DefaultCharset", "locale_charset"),
            ("HookUILanguageAPI", "locale_hook_ui"),
            ("Timezone", "locale_timezone"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_STARTUP_MESSAGE"):
        append_section(lines, "StartupMessage")
        for ini_key, detail_key in [
            ("Enable", "startup_msg_enable"),
            ("Style", "startup_msg_style"),
            ("Title", "startup_msg_title"),
            ("Author", "startup_msg_author"),
            ("Text", "startup_msg_text"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    if has_feature("CIALLOHOOK_FEATURE_SPLASH_IMAGE"):
        append_section(lines, "SplashImage")
        for ini_key, detail_key in [
            ("Enable", "splash_enable"),
            ("ImageFile", "splash_file"),
            ("Width", "splash_width"),
            ("Height", "splash_height"),
            ("EntryEffect", "splash_entry_effect"),
            ("ExitEffect", "splash_exit_effect"),
            ("EntryMs", "splash_entry_ms"),
            ("HoldMs", "splash_hold_ms"),
            ("ExitMs", "splash_exit_ms"),
            ("DurationMs", "splash_duration_ms"),
            ("Position", "splash_position"),
            ("InteractionMode", "splash_interaction"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    append_section(lines, "LoadMode")
    append_kv(lines, "Mode", ini_value_for_field(detail, "load_mode"))

    append_section(lines, "CialloLauncher")
    append_kv(lines, "DebugMode", ini_value_for_field(detail, "launcher_debug"))
    append_kv(lines, "TargetEXE", ini_value_for_field(detail, "launcher_target"))
    append_list(lines, rows_for(tables, "launcher_target_dlls"), "TargetDLLCount", "TargetDLLName_")

    append_section(lines, "StartupTiming")
    for ini_key, detail_key in [
        ("AttachMode", "startup_attach_mode"),
        ("DelayMs", "startup_delay_ms"),
        ("WaitForGuiReady", "startup_wait_gui"),
        ("EnableStartupWindowGate", "startup_window_gate"),
    ]:
        append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    append_section(lines, "Debug")
    for ini_key, detail_key in [
        ("Enable", "debug_enable"),
        ("LogToFile", "debug_file"),
        ("LogToConsole", "debug_console"),
    ]:
        append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    return "\n".join(lines) + "\n"


def build_launcher_ini_text(detail: dict[str, Any], tables: dict[str, list[list[str]]], features: dict[str, bool]) -> str:
    lines: list[str] = ["# Generated by tools/build_gui.py", "# UTF-8", ""]
    has_feature = lambda key: feature_enabled(features, key)

    append_section(lines, "CialloLauncher")
    append_kv(lines, "DebugMode", ini_value_for_field(detail, "launcher_debug"))
    append_kv(lines, "TargetEXE", ini_value_for_field(detail, "launcher_target"))
    append_list(lines, rows_for(tables, "launcher_target_dlls"), "TargetDLLCount", "TargetDLLName_")

    append_section(lines, "Debug")
    append_kv(lines, "LogToFile", ini_value_for_field(detail, "debug_file"))
    append_kv(lines, "LogToConsole", ini_value_for_field(detail, "debug_console"))

    if has_feature("CIALLOHOOK_FEATURE_FILE_PATCH"):
        append_section(lines, "FilePatch")
        append_kv(lines, "Enable", ini_value_for_field(detail, "file_patch_enable"))
        append_list(lines, rows_for(tables, "file_patch_folders"), "PatchFolderCount", "PatchFolderName_")
        if has_feature("CIALLOHOOK_FEATURE_CUSTOM_PAK"):
            append_kv(lines, "CustomPakEnable", ini_value_for_field(detail, "file_custom_pak_enable"))
            append_list(lines, rows_for(tables, "file_custom_paks"), "CustomPakCount", "CustomPakName_")

    if has_feature("CIALLOHOOK_FEATURE_STARTUP_MESSAGE"):
        append_section(lines, "StartupMessage")
        append_kv(lines, "Enable", ini_value_for_field(detail, "startup_msg_enable"))
        append_kv(lines, "Style", ini_value_for_field(detail, "startup_msg_style"))
        append_kv(lines, "Title", ini_value_for_field(detail, "startup_msg_title"))
        append_kv(lines, "Author", ini_value_for_field(detail, "startup_msg_author"))
        append_kv(lines, "Text", ini_value_for_field(detail, "startup_msg_text"))

    if has_feature("CIALLOHOOK_FEATURE_LOCALE_EMULATOR"):
        append_section(lines, "LocaleEmulator")
        for ini_key, detail_key in [
            ("Enable", "locale_enable"),
            ("AnsiCodePage", "locale_acp"),
            ("OemCodePage", "locale_oem"),
            ("LocaleID", "locale_id"),
            ("DefaultCharset", "locale_charset"),
            ("HookUILanguageAPI", "locale_hook_ui"),
            ("Timezone", "locale_timezone"),
        ]:
            append_kv(lines, ini_key, ini_value_for_field(detail, detail_key))

    return "\n".join(lines) + "\n"


def parse_ini_values(text: str) -> dict[str, dict[str, str]]:
    values: dict[str, dict[str, str]] = {}
    current_section = ""
    section_pattern = re.compile(r"^\s*\[([^\]]+)\]\s*$")
    value_pattern = re.compile(r"^\s*([^#;\[\]=][^=]*?)\s*=\s*(.*)$")
    for line in text.splitlines():
        section_match = section_pattern.match(line)
        if section_match:
            current_section = section_match.group(1).strip()
            values.setdefault(current_section, {})
            continue
        value_match = value_pattern.match(line)
        if value_match and current_section:
            values.setdefault(current_section, {})[value_match.group(1).strip()] = value_match.group(2)
    return values


def merge_ini_preserving_comments(base_text: str, desired_text: str) -> str:
    desired = parse_ini_values(desired_text)
    if not base_text.strip():
        return desired_text

    lines = base_text.splitlines()
    output: list[str] = []
    current_section = ""
    skip_current_section = False
    skipped_prefix: list[str] = []
    seen: dict[str, set[str]] = {section: set() for section in desired}
    template_keys: dict[str, set[str]] = {}
    repeatable_prefixes: dict[str, set[str]] = {}
    section_pattern = re.compile(r"^\s*\[([^\]]+)\]\s*$")
    value_pattern = re.compile(r"^(\s*)([^#;\[\]=][^=]*?)(\s*)=(.*)$")
    commented_value_pattern = re.compile(r"^(\s*[#;]\s*)([^#;\[\]=][^=]*?)(\s*)=(.*)$")
    repeatable_key_pattern = re.compile(r"^(.+_)\d+$")

    scan_section = ""
    for line in lines:
        section_match = section_pattern.match(line)
        if section_match:
            scan_section = section_match.group(1).strip()
            template_keys.setdefault(scan_section, set())
            repeatable_prefixes.setdefault(scan_section, set())
            continue
        key_match = value_pattern.match(line) or commented_value_pattern.match(line)
        if key_match and scan_section:
            key = key_match.group(2).strip()
            template_keys.setdefault(scan_section, set()).add(key)
            repeatable_match = repeatable_key_pattern.match(key)
            if repeatable_match:
                repeatable_prefixes.setdefault(scan_section, set()).add(repeatable_match.group(1))

    def pop_section_heading_prefix() -> list[str]:
        prefix: list[str] = []
        while output:
            stripped = output[-1].strip()
            if stripped == "" or stripped.startswith("#") or stripped.startswith(";"):
                prefix.append(output.pop())
                continue
            break
        prefix.reverse()
        return prefix

    def heading_prefix_from_skipped(lines_to_scan: list[str]) -> list[str]:
        heading_index = -1
        for index, candidate in enumerate(lines_to_scan):
            stripped = candidate.strip()
            if (stripped.startswith("#") or stripped.startswith(";")) and "===" in stripped:
                heading_index = index
        if heading_index < 0:
            return []
        if heading_index > 0 and lines_to_scan[heading_index - 1].strip() == "":
            heading_index -= 1
        return lines_to_scan[heading_index:]

    def flush_missing(section: str) -> None:
        if not section or section not in desired:
            return
        prefixes = repeatable_prefixes.get(section, set())
        for key, value in desired[section].items():
            if key not in seen[section]:
                repeatable_match = repeatable_key_pattern.match(key)
                if repeatable_match and repeatable_match.group(1) in prefixes:
                    output.append(f"{key} = {value}")
                    seen[section].add(key)

    for line in lines:
        section_match = section_pattern.match(line)
        if section_match:
            next_prefix = heading_prefix_from_skipped(skipped_prefix) if skip_current_section else pop_section_heading_prefix()
            skipped_prefix = []
            flush_missing(current_section)
            current_section = section_match.group(1).strip()
            skip_current_section = current_section not in desired
            if skip_current_section:
                continue
            output.extend(next_prefix)
            output.append(line)
            continue

        if skip_current_section:
            stripped = line.strip()
            if stripped == "" or stripped.startswith("#") or stripped.startswith(";"):
                skipped_prefix.append(line)
            else:
                skipped_prefix = []
            continue

        commented_value_match = commented_value_pattern.match(line)
        if commented_value_match and current_section in desired:
            key = commented_value_match.group(2).strip()
            output.append(line)
            if key in desired[current_section] and key not in seen[current_section]:
                output.append(f"{key} = {desired[current_section][key]}")
                seen[current_section].add(key)
            continue

        value_match = value_pattern.match(line)
        if value_match and current_section in desired:
            indent, key, spacing = value_match.group(1), value_match.group(2).strip(), value_match.group(3)
            if key in desired[current_section]:
                output.append(f"{indent}{key}{spacing}= {desired[current_section][key]}")
                seen[current_section].add(key)
            continue
        output.append(line)

    flush_missing(current_section)

    existing_sections = set()
    for line in output:
        section_match = section_pattern.match(line)
        if section_match:
            existing_sections.add(section_match.group(1).strip())

    for section, section_values in desired.items():
        if section in existing_sections:
            continue
        if output and output[-1] != "":
            output.append("")
        output.append(f"[{section}]")
        for key, value in section_values.items():
            output.append(f"{key} = {value}")

    return "\n".join(output).rstrip() + "\n"


def read_ini_base(output_path: Path, template_path: Path) -> str:
    if template_path.exists():
        return template_path.read_text(encoding="utf-8-sig")
    if output_path.exists():
        return output_path.read_text(encoding="utf-8-sig")
    return ""


def write_ini_files(state: dict[str, Any]) -> list[Path]:
    hook_text = build_ciallohook_ini_text(state["detail"], state["tables"], state["features"])
    launcher_text = build_launcher_ini_text(state["detail"], state["tables"], state["features"])
    written_paths: list[Path] = []
    for platform in build_platforms(state["platform"]):
        paths = output_ini_paths_for_platform(state, platform)
        for path in paths.values():
            path.parent.mkdir(parents=True, exist_ok=True)
        hook_ini_names = ["CialloHook.ini"]
        if feature_enabled(state["features"], "CIALLOHOOK_FEATURE_PROXY_EXPORTS"):
            hook_ini_names.extend(["winmm.ini", "version.ini"])
        else:
            for name in ["winmm.ini", "version.ini"]:
                if paths[name].exists():
                    paths[name].unlink()
        for name in hook_ini_names:
            base = read_ini_base(paths[name], ROOT / "src" / "CialloHook" / "config" / "CialloHook.ini")
            paths[name].write_text(merge_ini_preserving_comments(base, hook_text), encoding="utf-8")
            written_paths.append(paths[name])
        launcher_base = read_ini_base(paths["CialloLauncher.ini"], ROOT / "src" / "CialloLauncher" / "CialloLauncher.ini")
        paths["CialloLauncher.ini"].write_text(merge_ini_preserving_comments(launcher_base, launcher_text), encoding="utf-8")
        written_paths.append(paths["CialloLauncher.ini"])
    return written_paths


def card(title: str) -> tuple[QFrame, QVBoxLayout]:
    frame = QFrame()
    frame.setObjectName("card")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(18, 16, 18, 16)
    layout.setSpacing(12)
    label = QLabel(title)
    label.setObjectName("sectionTitle")
    layout.addWidget(label)
    return frame, layout


class TableEditor(QWidget):
    def __init__(self, spec: TableSpec, rows: list[list[str]]) -> None:
        super().__init__()
        self.spec = spec
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)

        self.table = QTableWidget(0, len(spec.columns))
        self.table.setHorizontalHeaderLabels(spec.columns)
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.setMinimumHeight(130)
        layout.addWidget(self.table)

        buttons = QHBoxLayout()
        add_btn = QPushButton("添加")
        remove_btn = QPushButton("删除选中")
        clear_btn = QPushButton("清空")
        buttons.addWidget(add_btn)
        buttons.addWidget(remove_btn)
        buttons.addWidget(clear_btn)
        buttons.addStretch(1)
        layout.addLayout(buttons)

        add_btn.clicked.connect(self.add_empty_row)
        remove_btn.clicked.connect(self.remove_selected_rows)
        clear_btn.clicked.connect(lambda: self.table.setRowCount(0))
        for row in rows:
            self.add_row(row)

    def set_rows(self, rows: list[list[str]]) -> None:
        self.table.setRowCount(0)
        for row in rows:
            self.add_row(row)

    def add_empty_row(self) -> None:
        self.add_row([""] * len(self.spec.columns))

    def add_row(self, values: list[str]) -> None:
        row_index = self.table.rowCount()
        self.table.insertRow(row_index)
        for column, value in enumerate(values[: len(self.spec.columns)]):
            self.table.setItem(row_index, column, QTableWidgetItem(str(value)))
        for column in range(len(values), len(self.spec.columns)):
            self.table.setItem(row_index, column, QTableWidgetItem(""))

    def remove_selected_rows(self) -> None:
        rows = sorted({index.row() for index in self.table.selectedIndexes()}, reverse=True)
        for row in rows:
            self.table.removeRow(row)

    def value(self) -> list[list[str]]:
        rows: list[list[str]] = []
        for row in range(self.table.rowCount()):
            values: list[str] = []
            for column in range(self.table.columnCount()):
                item = self.table.item(row, column)
                values.append(item.text() if item else "")
            if any(cell.strip() for cell in values):
                rows.append(values)
        return rows


class BuildGui(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.state = load_state()
        self.process: QProcess | None = None
        self.current_process_kind = ""
        self.pending_build_platforms: list[str] = []
        self.current_build_platform = ""
        self.updating_feature_dependencies = False
        self.feature_warning_label: QLabel | None = None
        self.checkboxes: dict[str, QCheckBox] = {}
        self.detail_widgets: dict[str, object] = {}
        self.table_widgets: dict[str, TableEditor] = {}
        self.help_labels: list[QLabel] = []

        self.setWindowTitle("CialloHook 编译配置器")
        self.resize(1280, 860)
        self.setMinimumSize(1080, 720)
        self.setAcceptDrops(True)

        root = QWidget()
        self.setCentralWidget(root)
        root_layout = QVBoxLayout(root)
        root_layout.setContentsMargins(18, 18, 18, 18)
        root_layout.setSpacing(14)

        title = QLabel("CialloHook 编译配置器")
        title.setObjectName("title")
        root_layout.addWidget(title)

        subtitle = QLabel("按功能裁剪 DLL，选择 ini 或内置配置，并用表单完整配置内置参数。保存后生成 build_options.h、build_options.props 和 gui_config_overrides.h。")
        subtitle.setObjectName("subtitle")
        subtitle.setWordWrap(True)
        root_layout.addWidget(subtitle)

        body = QHBoxLayout()
        body.setSpacing(14)
        root_layout.addLayout(body, 1)

        left_frame, left_layout = card("构建")
        left_frame.setFixedWidth(360)
        body.addWidget(left_frame)

        self.profile = QLineEdit(self.state["profile"])
        self.source = WheelPassthroughComboBox()
        self.source.addItems(["读取 ini", "使用内置配置"])
        self.source.setCurrentText("使用内置配置" if self.state["source"] == "builtin" else "读取 ini")
        self.ini_override = QLineEdit(self.state["ini_override"])
        self.ini_override.setPlaceholderText("留空：按 version.ini / winmm.ini / CialloHook.ini 查找")
        self.platform = WheelPassthroughComboBox()
        self.platform.addItems([PLATFORM_ALL, "x86", "x64"])
        self.platform.setCurrentText(self.state["platform"])
        self.configuration = WheelPassthroughComboBox()
        self.configuration.addItems(["Release", "Debug"])
        self.configuration.setCurrentText(self.state["configuration"])
        self.target = WheelPassthroughComboBox()
        self.target.addItems(["all", "ciallohook", "ciallolauncher", "runtime", "LitePAK_tool", "CialloWebM"])
        self.target.setCurrentText(self.state["target"])
        self.theme = WheelPassthroughComboBox()
        self.theme.addItems([THEME_SYSTEM, THEME_LIGHT, THEME_DARK])
        self.theme.setCurrentText(normalize_theme(str(self.state.get("theme", THEME_SYSTEM))))
        self.show_help = QCheckBox("显示")
        self.show_help.setChecked(bool(self.state.get("show_help", True)))
        self.show_help.setToolTip("显示或隐藏详细设置里的灰色说明文本；字段悬停提示仍保留。")

        build_grid = QGridLayout()
        build_grid.setHorizontalSpacing(10)
        build_grid.setVerticalSpacing(10)
        self.add_row(build_grid, 0, "配置名", self.profile)
        self.add_row(build_grid, 1, "配置来源", self.source)
        self.add_row(build_grid, 2, "ini 覆盖路径", self.ini_override)
        self.add_row(build_grid, 3, "平台", self.platform)
        self.add_row(build_grid, 4, "配置", self.configuration)
        self.add_row(build_grid, 5, "目标", self.target)
        self.add_row(build_grid, 6, "界面主题", self.theme)
        self.add_row(build_grid, 7, "帮助文本", self.show_help)
        left_layout.addLayout(build_grid)

        preset_group = QGroupBox("预设")
        preset_layout = QVBoxLayout(preset_group)
        self.enable_all_btn = QPushButton("启用全部功能")
        self.minimal_btn = QPushButton("最小字体/文本")
        self.reduce_risk_btn = QPushButton("关闭高误报风险项")
        self.restore_default_btn = QPushButton("恢复默认配置")
        preset_layout.addWidget(self.enable_all_btn)
        preset_layout.addWidget(self.minimal_btn)
        preset_layout.addWidget(self.reduce_risk_btn)
        preset_layout.addWidget(self.restore_default_btn)
        left_layout.addWidget(preset_group)
        left_layout.addStretch(1)

        action_row = QHBoxLayout()
        self.save_btn = QPushButton("保存")
        self.build_btn = QPushButton("保存并编译")
        self.build_btn.setObjectName("primary")
        action_row.addWidget(self.save_btn)
        action_row.addWidget(self.build_btn)
        left_layout.addLayout(action_row)

        self.tabs = QTabWidget()
        body.addWidget(self.tabs, 1)
        self.make_feature_tab()
        self.make_detail_tab()
        self.make_pack_tool_tab()
        self.make_log_tab()
        for cb in self.checkboxes.values():
            cb.toggled.connect(self.handle_feature_toggle)
        self.enforce_initial_feature_dependencies()
        self.refresh_detail_visibility()
        self.update_feature_warnings()

        self.enable_all_btn.clicked.connect(self.enable_all)
        self.minimal_btn.clicked.connect(self.apply_minimal)
        self.reduce_risk_btn.clicked.connect(self.reduce_risk)
        self.restore_default_btn.clicked.connect(self.restore_defaults)
        self.save_btn.clicked.connect(self.save_all)
        self.build_btn.clicked.connect(self.save_and_build)
        self.theme.currentTextChanged.connect(self.apply_style)
        self.show_help.toggled.connect(self.set_help_visible)

        self.apply_style()
        self.set_help_visible(self.show_help.isChecked())

    def make_feature_tab(self) -> None:
        page, layout = self.scroll_page()
        frame, feature_layout = card("功能裁剪")
        hint = QLabel("关闭功能后会写入 build_options.h；二进制补丁、Alice System3.x、RioShiina 还会同步写入 build_options.props，让 .vcxproj 直接跳过对应 .cpp。")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        feature_layout.addWidget(hint)
        self.feature_warning_label = QLabel()
        self.feature_warning_label.setObjectName("featureWarnings")
        self.feature_warning_label.setWordWrap(True)
        self.feature_warning_label.hide()
        feature_layout.addWidget(self.feature_warning_label)
        feature_grid = QGridLayout()
        feature_grid.setHorizontalSpacing(18)
        feature_grid.setVerticalSpacing(14)
        for index, feature in enumerate(FEATURES):
            item = QWidget()
            item.setObjectName("featureItem")
            item_layout = QVBoxLayout(item)
            item_layout.setContentsMargins(0, 0, 0, 0)
            item_layout.setSpacing(4)
            cb = QCheckBox(feature.label)
            cb.setChecked(bool(self.state["features"].get(feature.key, True)))
            cb.setToolTip(feature.description)
            desc = QLabel(feature.description)
            desc.setObjectName("hint")
            desc.setWordWrap(True)
            item_layout.addWidget(cb)
            item_layout.addWidget(desc)
            item_layout.addStretch(1)
            feature_grid.addWidget(item, index // 2, index % 2)
            self.checkboxes[feature.key] = cb
        feature_layout.addLayout(feature_grid)
        layout.addWidget(frame)
        layout.addStretch(1)
        self.tabs.addTab(page, "功能裁剪")

    def make_detail_tab(self) -> None:
        groups: list[tuple[str, list[str], list[str], tuple[str, ...]]] = [
            ("基础", ["load_mode", "debug_enable", "debug_file", "debug_console", "startup_attach_mode", "startup_delay_ms", "startup_wait_gui", "startup_window_gate"], [], ()),
            ("字体", ["font_charset", "font_name", "font_name_override", "font_charset_spoof", "font_spoof_from", "font_spoof_to", "font_unlock", "font_verbose", "font_cnjp_enable", "font_cnjp_verbose", "font_cnjp_json", "font_cnjp_read_encoding", "font_height", "font_width", "font_weight", "font_scale", "font_spacing_scale", "font_glyph_aspect", "font_glyph_offset_x", "font_glyph_offset_y", "font_metrics_left", "font_metrics_right", "font_metrics_top", "font_metrics_bottom"], ["font_skip_fonts", "font_redirect_rules"], ("CIALLOHOOK_FEATURE_FONT",)),
            ("字体 API", [f"font_{name}" for name in FONT_HOOKS], [], ("CIALLOHOOK_FEATURE_FONT",)),
            ("文本", ["text_encoding", "text_read_encoding", "text_write_encoding", "text_verbose"], ["text_rules"], ("CIALLOHOOK_FEATURE_TEXT",)),
            ("文本 API", [f"text_{name}" for name in TEXT_HOOKS], [], ("CIALLOHOOK_FEATURE_TEXT",)),
            ("窗口标题", ["title_mode", "title_encoding", "title_read_encoding", "title_write_encoding", "title_verbose"], ["title_rules"], ("CIALLOHOOK_FEATURE_WINDOW_TITLE",)),
            ("防截图", ["capture_enable", "capture_mode", "capture_fallback", "capture_existing", "capture_tool_windows", "capture_owned_windows", "capture_verbose"], [], ("CIALLOHOOK_FEATURE_SCREEN_CAPTURE_PROTECTION",)),
            ("文件补丁", ["file_patch_enable", "file_patch_log", "file_patch_debug"], ["file_patch_folders"], ("CIALLOHOOK_FEATURE_FILE_PATCH",)),
            ("CustomPak", ["file_custom_pak_enable", "file_vfs_mode"], ["file_custom_paks"], ("CIALLOHOOK_FEATURE_FILE_PATCH", "CIALLOHOOK_FEATURE_CUSTOM_PAK")),
            ("文件欺骗/重定向", ["file_spoof_enable", "file_spoof_log", "dir_redirect_enable", "dir_redirect_log"], ["file_spoof_files", "file_spoof_dirs", "dir_redirect_rules"], ("CIALLOHOOK_FEATURE_FILE_PATCH",)),
            ("虚拟注册表", ["registry_enable", "registry_log"], ["registry_files"], ("CIALLOHOOK_FEATURE_REGISTRY",)),
            ("注册表引导", ["registry_bootstrap_enable", "registry_bootstrap_cleanup", "registry_bootstrap_log"], ["registry_bootstrap_rules"], ("CIALLOHOOK_FEATURE_REGISTRY_BOOTSTRAP",)),
            ("代码页", ["codepage_enable", "codepage_from", "codepage_to", "codepage_hook_mbtowc", "codepage_hook_wctomb"], [], ("CIALLOHOOK_FEATURE_CODEPAGE",)),
            ("Locale Emulator", ["locale_enable", "locale_acp", "locale_oem", "locale_id", "locale_charset", "locale_hook_ui", "locale_timezone"], [], ("CIALLOHOOK_FEATURE_LOCALE_EMULATOR",)),
            ("启动声明", ["startup_msg_enable", "startup_msg_style", "startup_msg_title", "startup_msg_author", "startup_msg_text"], [], ("CIALLOHOOK_FEATURE_STARTUP_MESSAGE",)),
            ("启动图/WebM", ["splash_enable", "splash_file", "splash_width", "splash_height", "splash_entry_effect", "splash_exit_effect", "splash_entry_ms", "splash_hold_ms", "splash_exit_ms", "splash_duration_ms", "splash_position", "splash_interaction"], [], ("CIALLOHOOK_FEATURE_SPLASH_IMAGE",)),
            ("Siglus", ["siglus_enable", "siglus_gameexe", "siglus_output", "siglus_message", "siglus_debug"], [], ("CIALLOHOOK_FEATURE_SIGLUS_KEY_EXTRACT",)),
            ("Alice System3.x", ["alice_enable", "alice_log", "alice_exists", "alice_max_size"], ["alice_patch_folders"], ("CIALLOHOOK_FEATURE_ALICE_SYSTEM3X",)),
            ("RioShiina", ["rio_enable", "rio_mode", "rio_extract_dir", "rio_skip_invalid", "rio_log", "rio_process_reg", "rio_process_dvd", "rio_spec_dvd_size"], ["rio_patch_names", "rio_archives"], ("CIALLOHOOK_FEATURE_RIO_SHIINA",)),
            ("引擎缓存/Waffle", ["engine_cache_med", "engine_cache_majiro", "waffle_patch_enable"], [], ("CIALLOHOOK_FEATURE_ENGINE_CACHE",)),
            ("Krkr", ["krkr_patch_enable", "krkr_patch_verbose", "krkr_bootstrap_bypass", "krkr_cxdec_bridge"], ["krkr_patch_names"], ("CIALLOHOOK_FEATURE_KRKR_PATCH",)),
            ("二进制补丁", ["binary_enable", "binary_log", "binary_verify_old", "binary_fail_missing", "binary_fail_write", "binary_prefer_pak", "binary_hwbp_enable", "binary_hwbp_module", "binary_hwbp_rva"], ["binary_patch_files"], ("CIALLOHOOK_FEATURE_BINARY_PATCH",)),
            ("启动器", ["launcher_target", "launcher_debug"], ["launcher_target_dlls"], ()),
        ]
        page = QWidget()
        page_layout = QHBoxLayout(page)
        page_layout.setContentsMargins(16, 16, 16, 16)
        page_layout.setSpacing(14)

        self.detail_nav = QListWidget()
        self.detail_nav.setObjectName("detailNav")
        self.detail_nav.setFixedWidth(168)
        self.detail_nav.setSpacing(4)
        self.detail_nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        page_layout.addWidget(self.detail_nav)

        self.detail_stack = QStackedWidget()
        page_layout.addWidget(self.detail_stack, 1)

        self.detail_pages: list[tuple[int, tuple[str, ...]]] = []
        for title, field_keys, table_keys, required_features in groups:
            group_page, layout = self.scroll_page()
            self.add_form_section(layout, title, field_keys, table_keys)
            layout.addStretch(1)
            page_index = self.detail_stack.addWidget(group_page)
            self.detail_nav.addItem(title)
            self.detail_pages.append((page_index, required_features))
        self.detail_nav.currentRowChanged.connect(self.detail_stack.setCurrentIndex)
        self.detail_nav.setCurrentRow(0)
        self.refresh_detail_visibility()
        self.tabs.addTab(page, "详细设置")

    def make_pack_tool_tab(self) -> None:
        page, layout = self.scroll_page()
        frame, tool_layout = card("自定义解封包")
        hint = QLabel("CPK 复用 CialloPAK_tool.py；LPK 优先使用已编译 LitePAK_tool.exe，找不到时回退 Python 脚本；XP3 使用普通无加密 XP3 工具。")
        hint.setObjectName("hint")
        hint.setWordWrap(True)
        tool_layout.addWidget(hint)

        self.pak_format = WheelPassthroughComboBox()
        self.pak_format.addItems(["cpk", "lpk", "xp3"])
        self.pak_operation = WheelPassthroughComboBox()
        self.pak_operation.addItems(["封包", "解包"])

        self.pak_input_dir = DragDropLineEdit("input_dir")
        self.pak_input_dir.setPlaceholderText("封包时使用：选择要打包的目录")
        self.pak_input_dir_btn = QPushButton("选择")
        self.pak_file = DragDropLineEdit("pak_file")
        self.pak_file.setPlaceholderText("封包输出或解包输入，例如 patch.cpk / patch.lpk")
        self.pak_file_btn = QPushButton("选择")
        self.pak_manifest = DragDropLineEdit("manifest")
        self.pak_manifest.setPlaceholderText("可留空；封包时自动生成，解包时留空则按 hash 导出")
        self.pak_manifest_btn = QPushButton("选择")
        self.pak_output_dir = DragDropLineEdit("output_dir")
        self.pak_output_dir.setPlaceholderText("解包时使用：选择输出目录")
        self.pak_output_dir_btn = QPushButton("选择")

        self.pak_compression = WheelPassthroughComboBox()
        self.pak_compression.addItems(list(PACK_COMPRESSION_OPTIONS["cpk"]))
        self.pak_workers = WheelPassthroughSpinBox()
        self.pak_workers.setRange(0, 64)
        self.pak_workers.setValue(0)
        self.pak_parallel = WheelPassthroughComboBox()
        self.pak_parallel.addItems(["thread", "process"])
        self.pak_dedup = QCheckBox("启用")
        self.pak_dedup.setChecked(True)

        grid = QGridLayout()
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(10)
        self.add_row(grid, 0, "格式", self.pak_format)
        self.add_row(grid, 1, "操作", self.pak_operation)
        self.add_row(grid, 2, "输入目录", self.path_picker(self.pak_input_dir, self.pak_input_dir_btn))
        self.add_row(grid, 3, "封包文件", self.path_picker(self.pak_file, self.pak_file_btn))
        self.add_row(grid, 4, "Manifest", self.path_picker(self.pak_manifest, self.pak_manifest_btn))
        self.add_row(grid, 5, "输出目录", self.path_picker(self.pak_output_dir, self.pak_output_dir_btn))
        self.add_row(grid, 6, "压缩方式", self.pak_compression)
        self.add_row(grid, 7, "Workers", self.pak_workers)
        self.add_row(grid, 8, "并行模式", self.pak_parallel)
        self.add_row(grid, 9, "去重复", self.pak_dedup)
        tool_layout.addLayout(grid)

        action_row = QHBoxLayout()
        self.pack_tool_start_btn = QPushButton("开始执行")
        self.pack_tool_start_btn.setObjectName("primary")
        action_row.addWidget(self.pack_tool_start_btn)
        action_row.addStretch(1)
        tool_layout.addLayout(action_row)

        layout.addWidget(frame)
        layout.addStretch(1)
        self.tabs.addTab(page, "自定义解封包")

        self.pak_input_dir_btn.clicked.connect(self.browse_pack_input_dir)
        self.pak_file_btn.clicked.connect(self.browse_pack_file)
        self.pak_manifest_btn.clicked.connect(self.browse_pack_manifest)
        self.pak_output_dir_btn.clicked.connect(self.browse_pack_output_dir)
        for line in [self.pak_input_dir, self.pak_file, self.pak_manifest, self.pak_output_dir]:
            line.pathsDropped.connect(self.handle_pack_tool_drop)
        self.pack_tool_start_btn.clicked.connect(self.start_pack_tool_process)
        self.pak_format.currentTextChanged.connect(self.update_pack_tool_state)
        self.pak_format.currentTextChanged.connect(self.refresh_pack_autofill_for_format)
        self.pak_operation.currentTextChanged.connect(self.update_pack_tool_state)
        self.update_pack_tool_state()

    def path_picker(self, line: QLineEdit, button: QPushButton) -> QWidget:
        widget = QWidget()
        widget.setObjectName("inlinePicker")
        layout = QHBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        layout.addWidget(line, 1)
        layout.addWidget(button)
        return widget

    def update_pack_tool_state(self, *_args: Any) -> None:
        if not hasattr(self, "pak_operation"):
            return
        self.update_pack_compression_options()
        is_pack = self.pak_operation.currentText() == "封包"
        is_xp3 = self.pak_format.currentText() == "xp3"
        self.pak_input_dir.setEnabled(is_pack)
        self.pak_input_dir_btn.setEnabled(is_pack)
        self.pak_output_dir.setEnabled(not is_pack)
        self.pak_output_dir_btn.setEnabled(not is_pack)
        self.pak_manifest.setEnabled(not is_xp3)
        self.pak_manifest_btn.setEnabled(not is_xp3)
        self.pak_compression.setEnabled(is_pack)
        self.pak_workers.setEnabled(not is_xp3)
        self.pak_parallel.setEnabled(not is_xp3)
        self.pak_dedup.setEnabled(is_pack and not is_xp3)

    def update_pack_compression_options(self) -> None:
        fmt = self.pak_format.currentText() if hasattr(self, "pak_format") else "cpk"
        options = PACK_COMPRESSION_OPTIONS.get(fmt, PACK_COMPRESSION_OPTIONS["cpk"])
        current = self.pak_compression.currentText()
        existing = [self.pak_compression.itemText(index) for index in range(self.pak_compression.count())]
        if existing == list(options):
            return
        self.pak_compression.blockSignals(True)
        try:
            self.pak_compression.clear()
            self.pak_compression.addItems(list(options))
            self.pak_compression.setCurrentText(current if current in options else options[0])
        finally:
            self.pak_compression.blockSignals(False)

    def dragEnterEvent(self, event) -> None:  # type: ignore[override]
        if hasattr(self, "pak_format") and event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        super().dragEnterEvent(event)

    def dragMoveEvent(self, event) -> None:  # type: ignore[override]
        if hasattr(self, "pak_format") and event.mimeData().hasUrls():
            event.acceptProposedAction()
            return
        super().dragMoveEvent(event)

    def dropEvent(self, event) -> None:  # type: ignore[override]
        paths = [url.toLocalFile() for url in event.mimeData().urls() if url.isLocalFile()]
        paths = [path for path in paths if path]
        if paths:
            self.handle_pack_tool_drop(paths, "")
            event.acceptProposedAction()
            return
        super().dropEvent(event)

    def archive_format_for_path(self, path: Path) -> str:
        suffix = path.suffix.lower().lstrip(".")
        return suffix if suffix in PACK_ARCHIVE_FORMATS else ""

    def set_pack_format_safely(self, fmt: str) -> None:
        if fmt in PACK_ARCHIVE_FORMATS:
            self.pak_format.setCurrentText(fmt)

    def pack_format_for_output(self) -> str:
        fmt = self.pak_format.currentText()
        return fmt if fmt in PACK_ARCHIVE_FORMATS else "cpk"

    def manifest_candidates_for_pak(self, pak_path: Path) -> list[Path]:
        return [
            pak_path.with_name(f"{pak_path.stem}_manifest.txt"),
            pak_path.with_suffix(".manifest"),
            Path(str(pak_path) + ".manifest"),
            Path(str(pak_path) + ".txt"),
            pak_path.with_suffix(".txt"),
        ]

    def find_existing_manifest_for_pak(self, pak_path: Path) -> Path | None:
        for candidate in self.manifest_candidates_for_pak(pak_path):
            if candidate.is_file():
                return candidate
        return None

    def autofill_from_input_dir(self, input_dir: Path) -> None:
        fmt = self.pack_format_for_output()
        self.set_pack_format_safely(fmt)
        self.pak_operation.setCurrentText("封包")
        self.pak_input_dir.setText(str(input_dir))
        pak_path = input_dir.parent / f"{input_dir.name}.{fmt}"
        self.pak_file.setText(str(pak_path))
        if fmt == "xp3":
            self.pak_manifest.clear()
        else:
            self.pak_manifest.setText(str(pak_path.with_name(f"{pak_path.stem}_manifest.txt")))
        self.update_pack_tool_state()
        self.log(f"已根据目录自动填写封包输出: {pak_path}")

    def autofill_from_pak_file(self, pak_path: Path, manifest_path: Path | None = None) -> None:
        fmt = self.archive_format_for_path(pak_path)
        if fmt:
            self.set_pack_format_safely(fmt)
        self.pak_operation.setCurrentText("解包")
        self.pak_file.setText(str(pak_path))
        self.pak_output_dir.setText(str(pak_path.with_name(f"{pak_path.stem}_unpacked")))
        manifest = manifest_path or self.find_existing_manifest_for_pak(pak_path)
        self.pak_manifest.setText(str(manifest) if manifest else "")
        self.update_pack_tool_state()
        self.log(f"已根据封包自动填写解包输出: {self.pak_output_dir.text()}")

    def refresh_pack_autofill_for_format(self) -> None:
        if not hasattr(self, "pak_operation") or self.pak_operation.currentText() != "封包":
            return
        input_text = self.pak_input_dir.text().strip()
        if not input_text:
            return
        input_dir = Path(input_text)
        if input_dir.is_dir() and self.pak_format.currentText() in PACK_ARCHIVE_FORMATS:
            pak_path = input_dir.parent / f"{input_dir.name}.{self.pak_format.currentText()}"
            self.pak_file.setText(str(pak_path))
            if self.pak_format.currentText() == "xp3":
                self.pak_manifest.clear()
            else:
                self.pak_manifest.setText(str(pak_path.with_name(f"{pak_path.stem}_manifest.txt")))

    def handle_pack_tool_drop(self, paths: list[str], role: str) -> None:
        path_objects = [Path(path) for path in paths if path]
        if not path_objects:
            return
        manifest_path = next((path for path in path_objects if path.is_file() and path.suffix.lower() in {".txt", ".manifest"}), None)
        archive_path = next((path for path in path_objects if path.is_file() and self.archive_format_for_path(path)), None)
        directory_path = next((path for path in path_objects if path.is_dir()), None)

        if role == "manifest" and manifest_path:
            self.pak_manifest.setText(str(manifest_path))
            return
        if role == "output_dir" and directory_path:
            self.pak_output_dir.setText(str(directory_path))
            return
        if role == "input_dir" and directory_path:
            self.autofill_from_input_dir(directory_path)
            return
        if role == "pak_file" and archive_path:
            self.autofill_from_pak_file(archive_path, manifest_path)
            return

        if archive_path:
            self.autofill_from_pak_file(archive_path, manifest_path)
            return
        if directory_path:
            self.autofill_from_input_dir(directory_path)
            return
        if manifest_path:
            self.pak_manifest.setText(str(manifest_path))

    def browse_pack_input_dir(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "选择输入目录", self.pak_input_dir.text() or str(ROOT))
        if path:
            self.autofill_from_input_dir(Path(path))

    def browse_pack_file(self) -> None:
        fmt = self.pak_format.currentText()
        suffix = f".{fmt}"
        filters = f"{fmt.upper()} 文件 (*{suffix});;所有文件 (*)"
        current = self.pak_file.text() or str(ROOT / f"patch{suffix}")
        if self.pak_operation.currentText() == "封包":
            path, _ = QFileDialog.getSaveFileName(self, "选择封包输出文件", current, filters)
        else:
            path, _ = QFileDialog.getOpenFileName(self, "选择封包文件", current, filters)
        if path:
            self.pak_file.setText(path)
            pak_path = Path(path)
            if self.pak_operation.currentText() == "解包":
                self.autofill_from_pak_file(pak_path)
            elif self.pak_operation.currentText() == "封包" and not self.pak_manifest.text().strip():
                self.pak_manifest.setText(str(pak_path.with_name(f"{pak_path.stem}_manifest.txt")))

    def browse_pack_manifest(self) -> None:
        current = self.pak_manifest.text() or str(self.default_manifest_for_pak())
        filters = "Manifest / 文本文件 (*.txt *.manifest);;所有文件 (*)"
        if self.pak_operation.currentText() == "封包":
            path, _ = QFileDialog.getSaveFileName(self, "选择 Manifest 输出文件", current, filters)
        else:
            path, _ = QFileDialog.getOpenFileName(self, "选择 Manifest 文件", current, filters)
        if path:
            self.pak_manifest.setText(path)

    def browse_pack_output_dir(self) -> None:
        path = QFileDialog.getExistingDirectory(self, "选择解包输出目录", self.pak_output_dir.text() or str(ROOT))
        if path:
            self.pak_output_dir.setText(path)

    def default_manifest_for_pak(self) -> Path:
        pak_text = self.pak_file.text().strip() if hasattr(self, "pak_file") else ""
        if pak_text:
            pak_path = Path(pak_text)
            return pak_path.with_name(f"{pak_path.stem}_manifest.txt")
        return ROOT / "patch_manifest.txt"

    def find_litepak_exe(self) -> Path | None:
        configuration = self.configuration.currentText()
        selected_platform = self.platform.currentText()
        platforms = ["x64", "x86"] if selected_platform in {PLATFORM_ALL, LEGACY_PLATFORM_ALL} else [selected_platform]
        candidates: list[Path] = []
        for platform in platforms:
            candidates.append(ROOT / "out" / "bin" / platform / configuration / "LitePAK_tool.exe")
        for platform in ["x64", "x86"]:
            for config in [configuration, "Release", "Debug"]:
                candidate = ROOT / "out" / "bin" / platform / config / "LitePAK_tool.exe"
                if candidate not in candidates:
                    candidates.append(candidate)
        for candidate in candidates:
            if candidate.exists():
                return candidate
        return None

    def validate_pack_tool_inputs(self) -> tuple[bool, str]:
        fmt = self.pak_format.currentText()
        is_pack = self.pak_operation.currentText() == "封包"
        pak_text = self.pak_file.text().strip()
        if not pak_text:
            return False, "请先填写封包文件路径。"
        pak_path = Path(pak_text)
        if pak_path.suffix.lower() != f".{fmt}":
            self.log(f"提示: 当前格式是 {fmt}，但封包扩展名是 {pak_path.suffix or '(无扩展名)'}。")
        if is_pack:
            input_text = self.pak_input_dir.text().strip()
            input_dir = Path(input_text)
            if not input_text or not input_dir.is_dir():
                return False, "封包前需要选择存在的输入目录。"
            if fmt != "xp3" and not self.pak_manifest.text().strip():
                self.pak_manifest.setText(str(self.default_manifest_for_pak()))
        else:
            if not pak_path.is_file():
                return False, "解包前需要选择存在的封包文件。"
            if not self.pak_output_dir.text().strip():
                return False, "解包前需要填写输出目录。"
            manifest_text = self.pak_manifest.text().strip()
            if fmt != "xp3" and manifest_text and not Path(manifest_text).is_file():
                return False, "Manifest 已填写，但文件不存在。"
        return True, ""

    def build_pack_tool_command(self) -> tuple[str, list[str], str]:
        fmt = self.pak_format.currentText()
        is_pack = self.pak_operation.currentText() == "封包"
        pak_path = Path(self.pak_file.text().strip())
        manifest_text = self.pak_manifest.text().strip()
        workers = str(self.pak_workers.value())
        compression = self.pak_compression.currentText()
        parallel = self.pak_parallel.currentText()

        if fmt == "xp3":
            args = [str(XP3_TOOL_PATH), "pack" if is_pack else "unpack"]
            if is_pack:
                args.extend(["--input", self.pak_input_dir.text().strip(), "--pak", str(pak_path), "--compression", compression])
            else:
                args.extend(["--pak", str(pak_path), "--output", self.pak_output_dir.text().strip()])
            return sys.executable or "python", args, "XP3_tool.py"

        if fmt == "cpk":
            args = [str(CIALLOPAK_TOOL_PATH), "pack" if is_pack else "unpack"]
            if is_pack:
                args.extend(["--input", self.pak_input_dir.text().strip(), "--pak", str(pak_path), "--manifest", manifest_text])
                if self.pak_dedup.isChecked():
                    args.append("--dedup")
                args.extend(["--compression", compression, "--workers", workers, "--parallel", parallel])
            else:
                args.extend(["--pak", str(pak_path), "--output", self.pak_output_dir.text().strip(), "--workers", workers, "--parallel", parallel])
                if manifest_text:
                    args.extend(["--manifest", manifest_text])
            return sys.executable or "python", args, "CialloPAK_tool.py"

        litepak_exe = self.find_litepak_exe()
        if litepak_exe is not None:
            args = ["pack" if is_pack else "unpack"]
            if is_pack:
                args.extend(["--input", self.pak_input_dir.text().strip(), "--pak", str(pak_path), "--manifest", manifest_text])
                if self.pak_dedup.isChecked():
                    args.append("--dedup")
                args.extend(["--compression", compression, "--workers", workers])
            else:
                args.extend(["--pak", str(pak_path), "--output", self.pak_output_dir.text().strip(), "--workers", workers])
                if manifest_text:
                    args.extend(["--manifest", manifest_text])
            return str(litepak_exe), args, f"LitePAK_tool.exe ({litepak_exe})"

        args = [str(LITEPAK_TOOL_PATH), "pack" if is_pack else "unpack"]
        if is_pack:
            args.extend(["--input", self.pak_input_dir.text().strip(), "--pak", str(pak_path), "--manifest", manifest_text])
            if self.pak_dedup.isChecked():
                args.append("--dedup")
            args.extend(["--compression", compression, "--workers", workers, "--parallel", parallel])
        else:
            args.extend(["--pak", str(pak_path), "--output", self.pak_output_dir.text().strip(), "--workers", workers, "--parallel", parallel])
            if manifest_text:
                args.extend(["--manifest", manifest_text])
        return sys.executable or "python", args, "LitePAK_tool.py"

    def format_command_for_log(self, program: str, args: list[str]) -> str:
        parts = [program, *args]
        return " ".join(f'"{part}"' if any(ch.isspace() for ch in part) else part for part in parts)

    def start_pack_tool_process(self) -> None:
        if self.process is not None:
            QMessageBox.information(self, "正在执行", "当前已有编译或解封包任务在运行。")
            return
        ok, error = self.validate_pack_tool_inputs()
        if not ok:
            QMessageBox.warning(self, "参数不完整", error)
            return
        program, args, tool_desc = self.build_pack_tool_command()
        self.process = QProcess(self)
        self.current_process_kind = "pak_tool"
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProgram(program)
        self.process.setArguments(args)
        env = QProcessEnvironment.systemEnvironment()
        env.insert("PYTHONIOENCODING", "utf-8")
        env.insert("PYTHONUTF8", "1")
        self.process.setProcessEnvironment(env)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.finished.connect(self.pack_tool_finished)
        self.set_busy(True)
        self.tabs.setCurrentIndex(self.tabs.count() - 1)
        self.log(f"开始执行自定义解封包: {tool_desc}")
        self.log(self.format_command_for_log(program, args))
        self.process.start()
        if not self.process.waitForStarted(5000):
            self.set_busy(False)
            self.process = None
            self.current_process_kind = ""
            QMessageBox.critical(self, "启动失败", f"无法启动工具: {program}")

    def pack_tool_finished(self, exit_code: int, _exit_status: object = None) -> None:
        self.log(f"自定义解封包结束，退出码 {exit_code}。")
        self.process = None
        self.current_process_kind = ""
        self.set_busy(False)

    def refresh_detail_visibility(self) -> None:
        if not hasattr(self, "detail_pages"):
            return
        first_visible = -1
        for item_index, (page_index, required_features) in enumerate(self.detail_pages):
            visible = all(self.checkboxes[key].isChecked() for key in required_features)
            item = self.detail_nav.item(item_index)
            item.setHidden(not visible)
            if visible and first_visible < 0:
                first_visible = item_index
        current_item = self.detail_nav.currentItem()
        if first_visible >= 0 and (current_item is None or current_item.isHidden()):
            self.detail_nav.setCurrentRow(first_visible)
        elif self.detail_nav.currentRow() >= 0:
            self.detail_stack.setCurrentIndex(self.detail_pages[self.detail_nav.currentRow()][0])

    def add_form_section(self, parent: QVBoxLayout, title: str, field_keys: list[str], table_keys: list[str]) -> None:
        if field_keys:
            form_frame, form_layout = card(title)
            section_help = SECTION_HELP.get(title, "")
            if section_help:
                section_help_label = QLabel(section_help)
                section_help_label.setObjectName("fieldHelp")
                section_help_label.setWordWrap(True)
                self.register_help_label(section_help_label)
                form_layout.addWidget(section_help_label)
            grid = QGridLayout()
            grid.setHorizontalSpacing(12)
            grid.setVerticalSpacing(10)
            row = 0
            for key in field_keys:
                field = FIELD_BY_KEY[key]
                widget = self.create_field_widget(field, self.state["detail"].get(key, field.default))
                self.detail_widgets[key] = widget
                self.connect_warning_widget(key, widget)
                help_text = help_for_field(field)
                if help_text:
                    widget.setToolTip(help_text)
                    row_widget = self.with_help(widget, help_text)
                else:
                    widget.setToolTip(section_help)
                    row_widget = widget
                self.add_row(grid, row, field.label, row_widget)
                row += 1
            form_layout.addLayout(grid)
            parent.addWidget(form_frame)
        for key in table_keys:
            table = TABLE_BY_KEY[key]
            table_frame, table_layout = card(table.label)
            table_help_text = help_for_table(table)
            if table_help_text:
                table_help = QLabel(table_help_text)
                table_help.setObjectName("fieldHelp")
                table_help.setWordWrap(True)
                self.register_help_label(table_help)
                table_layout.addWidget(table_help)
            editor = TableEditor(table, self.state["tables"].get(key, []))
            self.table_widgets[key] = editor
            table_layout.addWidget(editor)
            parent.addWidget(table_frame)

    def with_help(self, widget: QWidget, help_text: str) -> QWidget:
        container = QWidget()
        container.setObjectName("fieldWithHelp")
        layout = QVBoxLayout(container)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(4)
        layout.addWidget(widget)
        help_label = QLabel(help_text)
        help_label.setObjectName("fieldHelp")
        help_label.setWordWrap(True)
        help_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.register_help_label(help_label)
        layout.addWidget(help_label)
        return container

    def register_help_label(self, label: QLabel) -> None:
        self.help_labels.append(label)
        if hasattr(self, "show_help"):
            label.setVisible(self.show_help.isChecked())

    def set_help_visible(self, visible: bool) -> None:
        for label in getattr(self, "help_labels", []):
            label.setVisible(visible)

    def make_log_tab(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(16, 16, 16, 16)
        self.output = QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setObjectName("log")
        self.output.setFont(QFont("Consolas", 9))
        layout.addWidget(self.output)
        self.tabs.addTab(page, "编译日志")

    def scroll_page(self) -> tuple[QScrollArea, QVBoxLayout]:
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(14)
        scroll.setWidget(inner)
        return scroll, layout

    def create_field_widget(self, field: Field, value: Any) -> QWidget:
        if field.kind == "bool":
            box = QCheckBox("启用")
            box.setChecked(bool(value))
            return box
        if field.kind == "choice":
            combo = WheelPassthroughComboBox()
            combo.addItems(list(field.options))
            combo.setCurrentText(str(value))
            return combo
        if field.kind == "int":
            spin = WheelPassthroughSpinBox()
            spin.setRange(int(field.minimum if field.minimum is not None else -2147483647), int(field.maximum if field.maximum is not None else 2147483647))
            spin.setValue(parse_int(value, parse_int(field.default)))
            return spin
        if field.kind == "float":
            spin = WheelPassthroughDoubleSpinBox()
            spin.setRange(float(field.minimum if field.minimum is not None else -999999.0), float(field.maximum if field.maximum is not None else 999999.0))
            spin.setSingleStep(0.05)
            spin.setDecimals(3)
            spin.setValue(float(value))
            return spin
        if field.kind == "multiline":
            edit = QPlainTextEdit(str(value))
            edit.setMinimumHeight(90)
            edit.setMaximumHeight(150)
            return edit
        line = QLineEdit(str(value))
        if field.kind == "hexint":
            line.setPlaceholderText("支持十进制或 0x 前缀")
        return line

    def add_row(self, grid: QGridLayout, row: int, label: str, widget: QWidget) -> None:
        lab = QLabel(label)
        lab.setObjectName("fieldLabel")
        lab.setMinimumWidth(150)
        grid.addWidget(lab, row, 0)
        grid.addWidget(widget, row, 1)
        widget.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        if isinstance(widget, QPlainTextEdit) or widget.objectName() == "fieldWithHelp":
            widget.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)

    def connect_warning_widget(self, key: str, widget: QWidget) -> None:
        warning_detail_keys = {
            "binary_prefer_pak",
            "waffle_patch_enable",
            "startup_window_gate",
        }
        if key not in warning_detail_keys:
            return
        if isinstance(widget, QCheckBox):
            widget.toggled.connect(self.update_feature_warnings)

    def widget_value(self, key: str) -> Any:
        widget = self.detail_widgets[key]
        if isinstance(widget, QLineEdit):
            return widget.text()
        if isinstance(widget, QPlainTextEdit):
            return widget.toPlainText()
        if isinstance(widget, QCheckBox):
            return widget.isChecked()
        if isinstance(widget, QComboBox):
            return widget.currentText()
        if isinstance(widget, QSpinBox):
            return widget.value()
        if isinstance(widget, QDoubleSpinBox):
            return widget.value()
        return ""

    def set_widget_value(self, key: str, value: Any) -> None:
        widget = self.detail_widgets[key]
        field = FIELD_BY_KEY[key]
        if isinstance(widget, QLineEdit):
            widget.setText(str(value))
        elif isinstance(widget, QPlainTextEdit):
            widget.setPlainText(str(value))
        elif isinstance(widget, QCheckBox):
            widget.setChecked(bool(value))
        elif isinstance(widget, QComboBox):
            widget.setCurrentText(str(value))
        elif isinstance(widget, QSpinBox):
            widget.setValue(parse_int(value, parse_int(field.default)))
        elif isinstance(widget, QDoubleSpinBox):
            try:
                widget.setValue(float(value))
            except Exception:
                widget.setValue(float(field.default))

    def feature_key_for_checkbox(self, checkbox: QCheckBox) -> str:
        for key, candidate in self.checkboxes.items():
            if candidate is checkbox:
                return key
        return ""

    def set_feature_checked(self, key: str, checked: bool) -> None:
        checkbox = self.checkboxes.get(key)
        if checkbox is not None and checkbox.isChecked() != checked:
            checkbox.setChecked(checked)

    def enable_feature_parents(self, key: str) -> None:
        for parent_key in FEATURE_DEPENDENCIES.get(key, ()):
            self.set_feature_checked(parent_key, True)
            self.enable_feature_parents(parent_key)

    def disable_feature_dependents(self, key: str) -> None:
        for child_key in FEATURE_DEPENDENTS.get(key, ()):
            self.set_feature_checked(child_key, False)
            self.disable_feature_dependents(child_key)

    def enforce_initial_feature_dependencies(self) -> None:
        self.updating_feature_dependencies = True
        try:
            normalized = normalize_feature_dependencies(self.collect_features())
            for key, value in normalized.items():
                self.set_feature_checked(key, value)
        finally:
            self.updating_feature_dependencies = False

    def handle_feature_toggle(self, checked: bool) -> None:
        if not self.updating_feature_dependencies:
            sender = self.sender()
            key = self.feature_key_for_checkbox(sender) if isinstance(sender, QCheckBox) else ""
            self.updating_feature_dependencies = True
            try:
                if key:
                    if checked:
                        self.enable_feature_parents(key)
                    else:
                        self.disable_feature_dependents(key)
            finally:
                self.updating_feature_dependencies = False
        self.refresh_detail_visibility()
        self.update_feature_warnings()

    def current_detail_values_for_warnings(self) -> dict[str, Any]:
        detail = dict(self.state.get("detail", {}))
        for key in self.detail_widgets:
            detail[key] = self.widget_value(key)
        return detail

    def collect_feature_warnings(self) -> list[str]:
        features = self.collect_features()
        detail = self.current_detail_values_for_warnings()
        warnings: list[str] = []
        for rule in FEATURE_WARNINGS:
            feature_key = str(rule["feature"])
            if not features.get(feature_key, False):
                continue
            detail_key = rule.get("detail")
            if detail_key and not bool(detail.get(str(detail_key), False)):
                continue
            missing_any = tuple(rule.get("missing_any", ()))
            missing_all = tuple(rule.get("missing_all", ()))
            missing_any_active = missing_any and any(not features.get(key, False) for key in missing_any)
            missing_all_active = missing_all and all(not features.get(key, False) for key in missing_all)
            if missing_any_active or missing_all_active:
                label = FEATURE_LABELS.get(feature_key, feature_key)
                warnings.append(f"{label}: {rule['message']}")

        window_hook_features = (
            "CIALLOHOOK_FEATURE_WINDOW_TITLE",
            "CIALLOHOOK_FEATURE_SCREEN_CAPTURE_PROTECTION",
            "CIALLOHOOK_FEATURE_STARTUP_MESSAGE",
        )
        if bool(detail.get("startup_window_gate", False)) and not any(features.get(key, False) for key in window_hook_features):
            warnings.append("基础: 启动窗口门控需要窗口标题、启动声明或防截图/录屏任一窗口 Hook；全部关闭后不会生效。")
        return warnings

    def update_feature_warnings(self) -> None:
        if self.feature_warning_label is None:
            return
        warnings = self.collect_feature_warnings()
        if not warnings:
            self.feature_warning_label.hide()
            self.feature_warning_label.clear()
            return
        self.feature_warning_label.setText("配置提示:\n" + "\n".join(f"- {message}" for message in warnings))
        self.feature_warning_label.show()

    def collect_state(self) -> dict[str, Any]:
        features = normalize_feature_dependencies(self.collect_features())
        state = {
            "profile": self.profile.text().strip() or "Default",
            "source": "builtin" if self.source.currentText() == "使用内置配置" else "ini",
            "ini_override": self.ini_override.text(),
            "platform": self.platform.currentText(),
            "configuration": self.configuration.currentText(),
            "target": self.target.currentText(),
            "theme": normalize_theme(self.theme.currentText()),
            "show_help": self.show_help.isChecked(),
            "features": features,
            "detail": {},
            "tables": {},
        }
        for field in FIELDS:
            state["detail"][field.key] = self.widget_value(field.key)
        for key, editor in self.table_widgets.items():
            state["tables"][key] = editor.value()
        return state

    def collect_features(self) -> dict[str, bool]:
        return {key: cb.isChecked() for key, cb in self.checkboxes.items()}

    def save_all(self) -> None:
        try:
            self.state = self.collect_state()
            write_options(self.state, self.state["features"])
            write_props(self.state["features"])
            write_overrides(self.state["detail"], self.state["tables"], self.state["features"])
            written_ini_paths = write_ini_files(self.state)
            save_state_ini(self.state)
            self.log(f"已保存: {OPTIONS_PATH}")
            self.log(f"已保存: {PROPS_PATH}")
            self.log(f"已保存: {OVERRIDES_PATH}")
            for ini_path in written_ini_paths:
                self.log(f"已保存输出 ini: {ini_path}")
            self.log(f"已保存界面状态: {STATE_PATH}")
        except Exception as exc:
            QMessageBox.critical(self, "保存失败", str(exc))

    def save_and_build(self) -> None:
        self.save_all()
        if self.process is not None:
            QMessageBox.information(self, "正在执行", "当前已有编译或解封包任务在运行。")
            return
        selected_platform = self.platform.currentText()
        if self.target.currentText() == "CialloWebM" and selected_platform in {PLATFORM_ALL, LEGACY_PLATFORM_ALL}:
            self.pending_build_platforms = ["both"]
        else:
            self.pending_build_platforms = build_platforms(selected_platform)
        self.set_busy(True)
        self.tabs.setCurrentIndex(self.tabs.count() - 1)
        self.start_next_build()

    def start_next_build(self) -> None:
        if not self.pending_build_platforms:
            self.set_busy(False)
            self.current_build_platform = ""
            self.log("全部编译完成。")
            return
        platform = self.pending_build_platforms.pop(0)
        self.current_build_platform = platform
        self.current_process_kind = "build"
        target = self.target.currentText()
        script = WEBM_BUILD_SCRIPT if target == "CialloWebM" else BUILD_SCRIPT
        script_platform = platform if platform != "all" else "both"
        args = [
            "-NoLogo",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(script),
            "-Platform",
            script_platform,
            "-Configuration",
            self.configuration.currentText(),
        ]
        if target != "CialloWebM":
            args.extend(["-Target", target])
        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProgram("powershell.exe")
        self.process.setArguments(args)
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.finished.connect(self.build_finished)
        self.log(f"开始编译 {platform} {self.configuration.currentText()}...")
        self.process.start()
        if not self.process.waitForStarted(5000):
            self.set_busy(False)
            self.process = None
            self.pending_build_platforms = []
            self.current_build_platform = ""
            self.current_process_kind = ""
            QMessageBox.critical(self, "编译失败", "无法启动 powershell.exe")

    def read_output(self) -> None:
        if self.process is None:
            return
        text = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        if text:
            self.output.moveCursor(self.output.textCursor().MoveOperation.End)
            self.output.insertPlainText(text)
            self.output.moveCursor(self.output.textCursor().MoveOperation.End)

    def build_finished(self, exit_code: int, _exit_status: object = None) -> None:
        platform = self.current_build_platform or self.platform.currentText()
        self.log(f"{platform} 编译结束，退出码 {exit_code}。")
        self.process = None
        if exit_code == 0 and self.pending_build_platforms:
            self.start_next_build()
            return
        if exit_code == 0 and self.state:
            try:
                for ini_path in write_ini_files(self.state):
                    self.log(f"已刷新输出 ini: {ini_path}")
            except Exception as exc:
                self.log(f"刷新输出 ini 失败: {exc}")
        elif exit_code != 0:
            self.pending_build_platforms = []
        self.set_busy(False)
        self.current_build_platform = ""
        self.current_process_kind = ""

    def set_busy(self, busy: bool) -> None:
        for button in [self.save_btn, self.build_btn, self.enable_all_btn, self.minimal_btn, self.reduce_risk_btn, self.restore_default_btn]:
            button.setEnabled(not busy)
        if hasattr(self, "pack_tool_start_btn"):
            self.pack_tool_start_btn.setEnabled(not busy)

    def log(self, text: str) -> None:
        if hasattr(self, "output"):
            self.output.appendPlainText(text)

    def enable_all(self) -> None:
        for cb in self.checkboxes.values():
            cb.setChecked(True)

    def apply_minimal(self) -> None:
        for feature in FEATURES:
            self.checkboxes[feature.key].setChecked(feature.minimal)
        self.checkboxes["CIALLOHOOK_FEATURE_PROXY_EXPORTS"].setChecked(True)

    def restore_defaults(self) -> None:
        defaults = make_default_state()
        self.profile.setText(defaults["profile"])
        self.source.setCurrentText("使用内置配置" if defaults["source"] == "builtin" else "读取 ini")
        self.ini_override.setText(defaults["ini_override"])
        self.platform.setCurrentText(defaults["platform"])
        self.configuration.setCurrentText(defaults["configuration"])
        self.target.setCurrentText(defaults["target"])
        self.theme.setCurrentText(defaults["theme"])
        self.show_help.setChecked(bool(defaults["show_help"]))
        for feature in FEATURES:
            self.checkboxes[feature.key].setChecked(bool(defaults["features"][feature.key]))
        for field in FIELDS:
            self.set_widget_value(field.key, defaults["detail"][field.key])
        for table in TABLES:
            self.table_widgets[table.key].set_rows(defaults["tables"][table.key])
        self.state = defaults
        self.refresh_detail_visibility()
        self.log("已恢复界面默认配置，点击保存后写入文件。")

    def reduce_risk(self) -> None:
        high_risk = {
            "CIALLOHOOK_FEATURE_LOCALE_EMULATOR",
            "CIALLOHOOK_FEATURE_BINARY_PATCH",
            "CIALLOHOOK_FEATURE_REGISTRY_BOOTSTRAP",
            "CIALLOHOOK_FEATURE_SCREEN_CAPTURE_PROTECTION",
            "CIALLOHOOK_FEATURE_SIGLUS_KEY_EXTRACT",
            "CIALLOHOOK_FEATURE_RIO_SHIINA",
            "CIALLOHOOK_FEATURE_CUSTOM_PAK",
            "CIALLOHOOK_FEATURE_CODECRYPT_PATCH",
            "CIALLOHOOK_FEATURE_KRKR_PATCH",
            "CIALLOHOOK_FEATURE_PROXY_EXPORTS",
        }
        for key in high_risk:
            self.checkboxes[key].setChecked(False)

    def apply_style(self, *_args: Any) -> None:
        selected_theme = normalize_theme(self.theme.currentText()) if hasattr(self, "theme") else THEME_SYSTEM
        dark = selected_theme == THEME_DARK or (selected_theme == THEME_SYSTEM and system_prefers_dark())
        colors = {
            "bg": "#0f141d" if dark else "#f5f7fb",
            "panel": "#151c29" if dark else "#ffffff",
            "panel_alt": "#111827" if dark else "#f8fafd",
            "control": "#0f1724" if dark else "#ffffff",
            "control_hover": "#253044" if dark else "#e4ebf6",
            "button": "#1d2738" if dark else "#eef2f8",
            "border": "#2f3b4f" if dark else "#dde3ee",
            "border_strong": "#41506a" if dark else "#cbd5e3",
            "header": "#1c2636" if dark else "#eef2f8",
            "text": "#e7edf6" if dark else "#172033",
            "title": "#f4f7fb" if dark else "#0d1628",
            "muted": "#9aa7bb" if dark else "#5d687b",
            "nav_text": "#c5cfdd" if dark else "#425067",
            "accent": "#6ea8fe" if dark else "#2f6fed",
            "accent_text": "#07111f" if dark else "#ffffff",
            "warning_bg": "#3a2f12" if dark else "#fff8e6",
            "warning_border": "#8a6d20" if dark else "#f0d58c",
            "warning_text": "#f8df8a" if dark else "#664d03",
            "log_bg": "#090e17" if dark else "#0f1724",
            "log_text": "#d9e3f0",
        }
        self.setStyleSheet(
            f"""
            QWidget {{
                background: {colors['bg']};
                color: {colors['text']};
                font-family: "Segoe UI", "Microsoft YaHei UI";
                font-size: 10pt;
            }}
            QLabel#title {{
                font-size: 22pt;
                font-weight: 700;
                color: {colors['title']};
                background: transparent;
            }}
            QLabel#subtitle, QLabel#hint, QLabel#fieldHelp {{
                color: {colors['muted']};
                background: transparent;
            }}
            QLabel#fieldHelp {{
                font-size: 9pt;
                line-height: 1.25;
            }}
            QWidget#fieldWithHelp, QWidget#inlinePicker {{
                background: transparent;
            }}
            QWidget#featureItem {{
                background: transparent;
            }}
            QLabel#featureWarnings {{
                background: {colors['warning_bg']};
                border: 1px solid {colors['warning_border']};
                border-radius: 6px;
                color: {colors['warning_text']};
                padding: 8px 10px;
            }}
            QFrame#card {{
                background: {colors['panel']};
                border: 1px solid {colors['border']};
                border-radius: 8px;
            }}
            QLabel#sectionTitle {{
                font-size: 12pt;
                font-weight: 700;
                background: transparent;
            }}
            QLabel#fieldLabel {{
                color: {colors['muted']};
                background: transparent;
            }}
            QLineEdit, QComboBox, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QTableWidget {{
                background: {colors['control']};
                border: 1px solid {colors['border_strong']};
                border-radius: 6px;
                padding: 6px 8px;
                color: {colors['text']};
                selection-background-color: {colors['accent']};
            }}
            QComboBox QAbstractItemView {{
                background: {colors['panel']};
                border: 1px solid {colors['border_strong']};
                color: {colors['text']};
                selection-background-color: {colors['accent']};
            }}
            QTableWidget::item:selected {{
                background: {colors['accent']};
                color: {colors['accent_text']};
            }}
            QHeaderView::section {{
                background: {colors['header']};
                border: 0;
                border-right: 1px solid {colors['border']};
                border-bottom: 1px solid {colors['border']};
                padding: 6px;
                font-weight: 600;
                color: {colors['text']};
            }}
            QCheckBox {{
                background: transparent;
                spacing: 8px;
                min-height: 24px;
            }}
            QPushButton {{
                background: {colors['button']};
                border: 1px solid {colors['border_strong']};
                border-radius: 6px;
                padding: 8px 12px;
                color: {colors['text']};
            }}
            QPushButton:hover {{
                background: {colors['control_hover']};
            }}
            QPushButton#primary {{
                background: {colors['accent']};
                border-color: {colors['accent']};
                color: {colors['accent_text']};
                font-weight: 600;
            }}
            QGroupBox {{
                background: transparent;
                border: 1px solid {colors['border']};
                border-radius: 8px;
                margin-top: 12px;
                padding: 12px 10px 10px 10px;
                color: {colors['muted']};
            }}
            QTabWidget::pane {{
                border: 1px solid {colors['border']};
                border-radius: 8px;
                background: {colors['panel']};
            }}
            QTabBar::tab {{
                background: {colors['header']};
                border: 1px solid {colors['border']};
                padding: 9px 14px;
                margin-right: 4px;
                border-top-left-radius: 7px;
                border-top-right-radius: 7px;
                color: {colors['text']};
            }}
            QTabBar::tab:selected {{
                background: {colors['panel']};
                color: {colors['accent']};
                font-weight: 600;
            }}
            QListWidget#detailNav {{
                background: {colors['panel_alt']};
                border: 1px solid {colors['border']};
                border-radius: 8px;
                padding: 6px;
                outline: 0;
            }}
            QListWidget#detailNav::item {{
                border-radius: 6px;
                padding: 9px 10px;
                color: {colors['nav_text']};
            }}
            QListWidget#detailNav::item:hover {{
                background: {colors['header']};
            }}
            QListWidget#detailNav::item:selected {{
                background: {colors['accent']};
                color: {colors['accent_text']};
                font-weight: 600;
            }}
            QPlainTextEdit#log {{
                background: {colors['log_bg']};
                color: {colors['log_text']};
                border-color: {colors['log_bg']};
            }}
            QScrollBar:vertical, QScrollBar:horizontal {{
                background: {colors['panel_alt']};
                border: 0;
                width: 12px;
                height: 12px;
            }}
            QScrollBar::handle:vertical, QScrollBar::handle:horizontal {{
                background: {colors['border_strong']};
                border-radius: 6px;
                min-height: 24px;
                min-width: 24px;
            }}
            QScrollBar::add-line, QScrollBar::sub-line {{
                width: 0;
                height: 0;
            }}
            """
        )


def main() -> int:
    app = QApplication(sys.argv)
    window = BuildGui()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())

