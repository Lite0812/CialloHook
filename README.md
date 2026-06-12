# CialloHook

一个面向 Windows 系统 Galgame 游戏的通用 Hook 工具。

它主要用于：

- 替换字体、修复乱码、调整字形和字距
- 文本替换、窗口标题替换、启动提示、启动图片动画
- 代码页伪装、Locale Emulator 转区
- 挂载补丁目录、补丁包、目录重定向、文件伪装、虚拟注册表
- 兼容 kirikiri / Waffle / MED / MAJIRO / Siglus / RioShiina 等引擎场景

## 功能特性

- **字体 Hook**：支持系统字体、外部字体文件、字符集伪装、定向字体重定向、跳过指定字体
- **字形微调**：支持字体缩放、字距、字重、宽高比、字形偏移、度量补偿等精细调整
- **文本 Hook**：覆盖 `TextOut` / `DrawText` / `ExtTextOut` 及 UI 控件、菜单、对话框等完整文本链路
- **窗口处理**：支持标题替换和启动时原生提示框
- **启动图片**：支持 GDI+ 图片弹窗，6 种入场/退场动画，可通过补丁目录或封包加载
- **资源挂载**：支持补丁目录、补丁包（cpk/xp3/lpk）、自定义 PAK / VFS、链式归档名
- **环境伪装**：支持代码页伪装、Locale Emulator 转区、UI 语言伪装、时区伪装
- **双加载模式**：支持 `proxy` 代理模式和 `loader` 启动器模式
- **启动控制**：支持延迟挂载、入口点挂载、GUI 就绪等待、窗口门控
- **注册表引导**：支持虚拟注册表注入和临时真实注册表引导（进程退出自动回滚）
- **RioShiina 引擎**：支持资源覆盖、WARC 解包、注册表/DVD 检测自动处理
- **引擎兼容**：支持 kirikiri xp3 补丁链、krkr bootstrap 绕过、krkr cxdec 桥接、Siglus 密钥提取
- **配置驱动**：绝大部分行为都通过 `ini` 控制，同时支持 `built-in` 内嵌配置
- **日志排障**：支持文件日志、控制台日志和详细调试日志

## 适用场景

- 日文游戏在中文系统下乱码、缺字、字重不对、字宽异常
- 汉化补丁需要替换字体，但不想回封资源
- 想直接在运行时替换文本或窗口标题
- 启动时展示补丁声明、Logo 动画等图片弹窗
- 需要把补丁目录、`xp3`、`cpk` 或其他补丁资源接到游戏读取链
- 需要伪装区域、代码页或时区，绕过部分地区依赖问题
- 需要临时写入注册表项但不想残留系统污染
- RioShiina 引擎游戏需要资源覆盖或 WARC 解包
- SiglusEngine 游戏需要提取 XOR 密钥

## 文件说明

- `version.dll` / `winmm.dll`：代理模式用的主 DLL
- `CialloHook.dll`：核心模块本体
- `CialloHook.ini`：主配置文件
- `version.ini` / `winmm.ini`：给代理名预留的同内容配置
- `src/CialloHook/config/config_source.h`：编译期配置来源开关，可切换 `ini` 或内置硬编码配置
- `src/CialloHook/hooks/rio_shiina_hooks.cpp` / `.h`：RioShiina 引擎资源覆盖与解包支持
- `src/CialloHook/config/CialloHook_Example.ini`：完整带注释配置示例
- `CialloLauncher.exe`：启动器模式入口
- `LoaderDll_x86.dll` / `LoaderDll_x64.dll`、`LocaleEmulatorPlus_x86.dll` / `LocaleEmulatorPlus_x64.dll`：LEP 转区依赖，按目标位数使用
- `LoaderDll.dll` / `LocaleEmulator.dll`：LE 转区依赖，主要用于 x86 兜底
- `subs_cn_jp.json`：日繁到简中映射示例文件
- `CialloPAK_tool.py`：`cpk` 封包/解包脚本版工具
- `CialloPAK_tool.exe`：`cpk` 封包/解包独立可执行版工具
- `LitePAK_tool.py`：`lpk` 封包/解包脚本版工具，支持 LitePAK v6、清单、校验、单文件读取
- `LitePAK_tool.exe`：`lpk` 封包/解包独立可执行版工具

## 快速开始

### 1. 先选加载方式

`CialloHook` 支持两种常用模式：

- `proxy`：把 DLL 直接放进游戏目录，随游戏自动加载
- `loader`：先运行启动器，再由启动器拉起游戏并注入 DLL

如果你不确定选哪个，先用 `proxy`。

### 2. Proxy 模式

部署最简单的模式，直接把 `version.dll` / `winmm.dll` 直接放进游戏目录，随游戏自动加载。

配置：

```ini
[LoadMode]
Mode = proxy
```

使用步骤：

1. 确认游戏是 `x86` 还是 `x64`
2. 选择对应位数的发布文件
3. 将 `version.dll`、`CialloHook.ini` 以及需要的附加文件复制到游戏目录
4. 启动游戏测试
5. 如果 `version.dll` 不生效，再改用 `winmm.dll` 试一次，都不行则改用 `loader` 模式

适合：

- 追求“解压即用”
- 不想额外改启动流程
- 只需要单个 Hook 模块

### 3. Loader 模式

更适合需要控制启动流程、注入多个 DLL 或配合转区的场景。

配置：

```ini
[LoadMode]
Mode = loader

[CialloLauncher]
TargetEXE = game.exe
TargetDLLCount = 1
TargetDLLName_0 = CialloHook.dll
```

使用步骤：

1. 把 `CialloLauncher.exe`、需要注入的 DLL、配置文件放到游戏目录
2. 修改 `TargetEXE`
3. 在 `TargetDLLName_i` 中写出完整注入列表；需要 CialloHook 功能时显式写入 `CialloHook.dll`
4. 运行 `CialloLauncher.exe`

适合：

- 游戏不方便使用代理 DLL
- 需要多 DLL 注入
- 需要更明确地控制启动目标

## 最小配置示例

如果你的目标只是“先让中文字体正常显示”，建议从这份最小配置开始：

```ini
[CialloHook]
Font = SimHei
Charset = 0x86
UnlockFontSelection = true

[LoadMode]
Mode = proxy

[Debug]
Enable = true
LogToFile = true
LogToConsole = false
```

这套配置适合先验证：

- Hook 是否成功加载
- 字体是否被替换
- 是否有基础日志输出

## 配置来源切换

现在支持两种配置来源：

- `ini`：默认模式，继续读取游戏目录里的 `CialloHook.ini` / `version.ini` / `winmm.ini`
- `built-in`：把配置直接写进源码，编译后 DLL 和 Loader 都可以不依赖 ini 内容

切换入口在 `src/CialloHook/config/config_source.h`：

```cpp
selection.mode = ConfigSourceMode::IniFile;
```

如果要改成编译进 DLL / Loader：

```cpp
selection.mode = ConfigSourceMode::BuiltIn;
```

然后按模式修改同一个文件里的两个函数：

- `ApplyBuiltInConfig`：CialloHook.dll / proxy 模式 / DLL 内逻辑
- `ApplyBuiltInLauncherConfig`：CialloLauncher.exe / loader 模式启动参数

Loader 模式要注意两点：

- `ApplyBuiltInConfig` 里把 `settings.loadMode.mode` 改成 `L"loader"`
- `ApplyBuiltInLauncherConfig` 里把 `targetExe` 改成目标游戏 EXE

另外，`targetDllNames` / `TargetDLLName_i` 是启动器的完整注入列表；启动器只注入这里写出的 DLL，不会自动追加 `CialloHook.dll`。如果要使用 CialloHook 功能，请显式填写 `CialloHook.dll`；也可以只填写其它 DLL，把 CialloLauncher 当作通用启动器使用。

## 常见用法

### 1. 替换成系统字体

```ini
[CialloHook]
Font = Microsoft YaHei
```

### 2. 使用外部字体文件

```ini
[CialloHook]
Font = .\fonts\SourceHanSansCN-Regular.otf
FontName = Source Han Sans CN Regular
```

说明：

- `Font` 可以写系统字体名，也可以写字体文件路径
- 如果字体文件名和字体内部名称不一致，通常还要补 `FontName`

### 3. 只重定向某个原始字体

```ini
[CialloHook]
RedirectFontCount = 1
RedirectFromFont_0 = MS UI Gothic
RedirectToFont_0 = Microsoft YaHei
```

### 4. 跳过某些原始字体

```ini
[CialloHook]
SkipFontCount = 1
SkipFontName_0 = MS Gothic
```

### 5. 字形微调

如果字体替换后大小、间距、字重不合适，可以用以下选项精细调整：

```ini
[CialloHook]
FontScale = 1.2          ; 整体缩放，1.0 为不变
FontSpacingScale = 1.1   ; 字间距缩放
FontWeight = 700         ; 字重，0=不改，400=常规，700=粗体
GlyphAspectRatio = 1.08  ; 字形宽高比，>1 更宽，<1 更窄
GlyphOffsetX = 1         ; 字形 X 偏移（像素），正数右移
GlyphOffsetY = -1        ; 字形 Y 偏移（像素），正数上移
MetricsOffsetLeft = 1    ; 左侧度量补偿
MetricsOffsetRight = -1  ; 右侧度量补偿
MetricsOffsetTop = 1     ; 顶部度量补偿
MetricsOffsetBottom = -1 ; 底部度量补偿
```

说明：

- `FontScale` 保留各处相对大小关系，推荐优先使用
- `FontHeight` / `FontWidth` 非 0 时优先于 `FontScale`，适合需要固定大小的场景
- 度量补偿适合微调字间距过紧或过松的情况

### 6. 字符集伪装

当游戏请求特定字符集的字体时，仅对匹配的来源字符集做伪装替换：

```ini
[CialloHook]
EnableCharsetSpoof = true
SpoofFromCharset = 0x80   ; SHIFTJIS_CHARSET（日文）
SpoofToCharset = 0x01     ; DEFAULT_CHARSET
```

与直接设置 `Charset` 的区别：

- `Charset` 非 0：无条件强制覆盖为指定字符集
- `EnableCharsetSpoof`：仅当来源字符集命中时才替换，更精准

### 7. 文本替换

```ini
[TextReplace]
ReplaceCount = 1
ReadEncoding = 932
WriteEncoding = 936
Original_0 = こんにちは
Replacement_0 = 你好
```

### 8. 启用日繁映射

这项功能适合游戏只支持 CP932 编码的场景。

文本本身已经能显示，但繁体字 / 日文字形不好看、缺字，故借用简中字形来显示。

原理可以简单理解为：

- 游戏实际要显示 `頭`
- Hook 改成去取 `头` 的字形和字宽
- 最终屏幕上显示为 `头` 的字形

示例：

```ini
[CialloHook]
EnableCnJpMap = true
CnJpMapJson = subs_cn_jp.json
CnJpMapReadEncoding = 932
```

说明：

- `CnJpMapJson` 默认就是 `subs_cn_jp.json`，读取优先级为补丁目录 → CustomPak → 游戏根目录
- 建议映射文件使用“单字对单字”形式
- 这项功能会在普通 `TextReplace` 规则之后再参与处理
- 如果某条文本已经命中了 `Replacement_i`，那条文本就不会再继续做日繁映射

一个最小映射示例：

```json
{
  "头": "頭",
  "国": "國",
  "学": "學"
}
```

上面这个例子表示：

- 当游戏要渲染 `頭` 时，改取 `头` 的字形
- 当游戏要渲染 `國` 时，改取 `国` 的字形
- 当游戏要渲染 `學` 时，改取 `学` 的字形

### 9. 文件补丁目录与封包

`[FilePatch]` 用来让“补丁文件优先于原始文件”。

最常见的两种补丁来源是：

- `PatchFolderName_i`：直接放在目录里的补丁文件
- `CustomPakName_i`：封装成 `cpk` / `xp3` / `lpk` 的补丁包

查找顺序大致是：

- 补丁目录
- 自定义 `cpk` / `xp3` / `lpk` 封包
- 游戏原始文件

一个常见配置：

```ini
[FilePatch]
Enable = true
PatchFolderCount = 1
PatchFolderName_0 = patch
CustomPakEnable = true
CustomPakCount = 1
CustomPakName_0 = patch.cpk
VFSMode = 0
EnableLog = false
```

这表示：

- 先从 `patch\` 目录找文件
- 目录里没有，再去 `patch.cpk` 里找
- 还没有，才回退到游戏原始资源

适合的发布方式：

- 想让用户直接替换零散文件时，用补丁目录
- 想把补丁打成单文件分发时，用 `cpk` 或 `lpk` 封包
- 想同时保留“基础补丁 + 热修复覆盖”时，两者可以一起开

封包相关说明：

- `CustomPakName_i` 可以写文件名、相对路径或绝对路径
- 支持 `cpk` / `xp3` / `lpk`；`cpk` / `lpk` 如需目录枚举，建议随包放置 manifest（如 `patch_manifest.txt`）
- 编号越大优先级越高，适合叠加 `patch_base.cpk`、`patch_cn.cpk`、`patch_hotfix.cpk`
- `VFSMode = 0` 一般更稳，适合发布默认值
- `VFSMode = 1` 是内存读取模式，适合有特殊性能或兼容需求时再试

如果你打算发一个单文件补丁包，推荐思路是：

1. 先在 `patch\` 目录里调试资源是否生效
2. 确认路径结构正确后再封成 `patch.cpk`
3. 发布时默认开启 `CustomPakEnable = true`

### 10. CialloPAK / LitePAK 封包工具

如果你准备把补丁目录封成单个封包文件，可以使用 `CialloPAK_tool` 或 `LitePAK_tool`：

- `CialloPAK_tool.py` / `CialloPAK_tool.exe`：用于 `cpk` 封包，适合普通补丁分发
- `LitePAK_tool.py` / `LitePAK_tool.exe`：用于 `lpk` 封包，支持 LitePAK v6、manifest、完整校验、单文件读取和可选签名参数

脚本版适合自己改脚本、命令行批处理；exe 版适合直接双击或拖拽使用，发布时也更方便。

两类工具主要提供：

- 把一个目录封成 `cpk` / `lpk`
- 把 `cpk` / `lpk` 解包回目录
- 按资源相对路径从封包里单独导出某个文件
- 生成 `manifest` 清单，保留 hash 和原始路径对应关系

最简单的用法就是拖拽：

- CialloPAK：把一个文件夹拖到工具上会自动封成 `.cpk`，把 `.cpk` 拖到工具上会解包
- LitePAK：把一个文件夹拖到工具上会自动封成 `.lpk` 并生成清单，把 `.lpk` 拖到工具上会按 hash 解包，把 `.lpk` 和对应 `.txt` 清单一起拖到工具上会按原始路径解包

如果你习惯命令行，也可以直接这样用：

```bash
python CialloPAK_tool.py pack --input patch --pak patch.cpk --manifest patch_manifest.txt --dedup --compression auto
python CialloPAK_tool.py unpack --pak patch.cpk --manifest patch_manifest.txt --output patch_unpacked
python CialloPAK_tool.py read --pak patch.cpk --name script/start.ks --output start.ks

python LitePAK_tool.py pack --input patch --pak patch.lpk --manifest patch_manifest.txt --dedup --compression auto
python LitePAK_tool.py unpack --pak patch.lpk --manifest patch_manifest.txt --output patch_unpacked
python LitePAK_tool.py info --pak patch.lpk
python LitePAK_tool.py verify --pak patch.lpk
python LitePAK_tool.py read --pak patch.lpk --name script/start.ks --output start.ks
```

如果你用的是打包好的可执行版，把前面的 `python xxx_tool.py` 换成对应 exe，例如：

```bash
CialloPAK_tool.exe pack --input patch --pak patch.cpk --manifest patch_manifest.txt --dedup --compression auto
LitePAK_tool.exe pack --input patch --pak patch.lpk --manifest patch_manifest.txt --dedup --compression auto
```

几个实用参数：

- `--dedup`：对重复内容做复用，补丁里重复资源多时更省空间
- `--compression auto`：自动在 `raw` / `zlib` / `zstd` / `lzma` 中选更合适的压缩结果
- `--workers`：指定并行线程/进程数，默认自动按 CPU 决定
- `--parallel thread|process`：切换并行方式，默认 `thread`
- LitePAK 的 `--sign-key`：指定 Ed25519 签名/验签私钥文件；不需要签名时可不填
- LitePAK 的 `--cdc-avg-kb` / `--whole-file-threshold-kb`：调整分块和小文件整文件处理策略

注意：

- 如果要使用 `zstd` 压缩或解压，脚本版环境里需要安装 `zstandard`
- LitePAK 脚本版若使用签名相关能力，需要可用的 `cryptography` 或 `PyNaCl`
- 不带 `manifest` 解包时，文件会按 hash 名称导出，不会还原原始路径
- 推荐先确认 `patch\` 目录结构正确，再封成 `cpk` / `lpk` 发布
- 对接 `CialloHook.ini` 时，把生成的 `patch.cpk` / `patch.lpk` 写到 `CustomPakName_i` 即可；CPK/LPK 若场景需要目录枚举请随包放置 manifest。字体、日繁映射 JSON、启动图、虚拟注册表和 `.1337` 均按 PatchFolder → CustomPak → 根目录读取。

### 11. x64dbg .1337 内存补丁

`[BinaryPatch]` 可在游戏启动后读取 x64dbg `.1337` 字节补丁文件，并按模块 RVA 写入 `NEW` 字节，适合把调试器里验证过的字节修改迁移到补丁包中自动应用。

`.1337` 文件示例：

```text
>game.exe
001CDB04:CC->B8
001CDB05:CC->00
>scn.dll
002CDB04:CC->90
```

配置示例：

```ini
[BinaryPatch]
Enable = true
PatchFileCount = 1
PatchFileName_0 = patches\main.1337
EnableLog = true
VerifyOldBytes = false
PreferCustomPak = true
FailOnMissingModule = false
FailOnWriteError = false
EnableHWBP = false
```

说明：

- `>game.exe` 表示主 EXE，实际按主模块基址计算；`>xxx.dll` 表示已加载 DLL
- 地址按“模块基址 + RVA”计算，写入 `NEW` 字节；默认不恢复旧字节
- 补丁文件路径可写相对游戏目录或绝对路径，也可放进已启用的 CustomPak 中
- 读取优先级支持 `PatchFolder → CustomPak → 游戏根目录`，与其他资源补丁一致
- `VerifyOldBytes = true` 时会先校验 `OLD` 字节，不匹配则跳过该连续块并记录警告
- `EnableHWBP = true` 可用硬件断点触发应用补丁，适合需要等到特定代码位置后再写入的场景

### 12. 启用代码页伪装

```ini
[CodePage]
Enable = true
FromCodePage = 932
ToCodePage = 936
```

### 13. 启用 Locale Emulator 转区

```ini
[LocaleEmulator]
Enable = true
AnsiCodePage = 932
OemCodePage = 932
LocaleID = 0x411
DefaultCharset = 128
Timezone = Tokyo Standard Time
```

说明：

- proxy 模式下由 `winmm.dll` / `version.dll` 直接重启转区；loader 模式下由 `CialloLauncher.exe` 处理
- 当前系统 ACP 已经等于目标 `AnsiCodePage` 时会自动跳过重启，避免重复转区
- 依赖加载顺序为 LEP 优先、LE 兜底：
  - LEP：`LoaderDll_x86.dll` / `LoaderDll_x64.dll` + `LocaleEmulatorPlus_x86.dll` / `LocaleEmulatorPlus_x64.dll`
  - LE：`LoaderDll.dll` + `LocaleEmulator.dll`
- 依赖可放在游戏目录 / CialloHook 目录，也可通过 `[FilePatch]` 的补丁目录或 CustomPak 提供；从补丁资源加载时会临时释放并在重启后清理
- 构建脚本会按位数复制依赖：`third\LEP` 下的 DLL 会分别进入 x86/x64 输出目录，`third\LE` 下的 DLL 只复制到 x86 输出目录

### 14. 启动图片弹窗

在游戏启动时弹出一个带动画效果的图片窗口，适合展示补丁声明、Logo、免责说明等。

```ini
[SplashImage]
Enable = true
ImageFile = splash.png
Width = 800
Height = 600
EntryEffect = 1    ; 1=淡入 2=旋转 3=破碎 4=缩放 5=百叶窗 6=像素化
ExitEffect = 1     ; 同上
EntryMs = 1200
HoldMs = 1800
ExitMs = 1500
Position = 1       ; 1=居中 2=左上 3=右上 4=左下 5=右下
InteractionMode = 0 ; 0=无交互 1=可拖动 2=点击消失
```

说明：

- 图片文件会在补丁目录 → CustomPak（cpk/xp3/lpk）→ 游戏根目录中按顺序查找；多项配置时编号越高优先级越高
- 入场和退场效果可以自由搭配（6×6 = 36 种组合）
- `DurationMs` 为 0 时自动按入场+保持+退场三段之和计算
- 找不到图片文件时静默跳过，不影响游戏启动

### 15. RioShiina 引擎支持

针对 RioShiina 引擎（如 `椎名里緒` 系列）的资源覆盖与封包处理。

**Mode 1 — 资源覆盖**：用补丁资源替换游戏原始资源文件：

```ini
[RioShiina]
Enable = true
Mode = 1
PatchCount = 1
PatchName_0 = unencrypted
ProcessReg = true
ProcessDvd = false
EnableLog = false
```

**Mode 2 — WARC 解包**：从指定 WARC 封包中提取资源：

```ini
[RioShiina]
Enable = true
Mode = 2
ArchivesToExtract = data.warc|voice.warc
ExtractOutputDir = rio_extract
SkipInvalidFileName = true
```

说明：

- 当前仅支持 x86 目标
- Mode 1 会复用 `[FilePatch]` 的补丁目录和 custom CPK 作为补丁来源，查找顺序为 `patch\name → patch.cpk\name → 游戏目录\name`
- 多个封包用 `|` 分隔，多个 PatchName 按编号越大优先级越高
- `ProcessReg = true` 自动扫描同目录 ini 里 `椎名里緒*` section 的注册表检测
- `ProcessDvd = true` 自动处理 DVD 检测，`SpecDvdFileSize = 0` 时仅改写盘符到 `W:\`

### 16. 注册表引导

在进程启动时临时写入真实注册表键值，进程退出时自动回滚清理：

```ini
[RegistryBootstrap]
Enable = true
CleanupOnExit = true
EnableLog = false
RuleCount = 2
Root_0 = HKCU
Key_0 = Software\Vendor\Game
ValueName_0 = InstallPath
Type_0 = SZ
Data_0 = .\
Root_1 = HKCU
Key_1 = Software\Vendor\Game
ValueName_1 = Type
Type_1 = DWORD
Data_1 = 0
```

说明：

- 当前支持 `SZ`（字符串）和 `DWORD` 两种类型
- `CleanupOnExit = true` 在进程退出时自动删除本次写入的键值
- 适合免安装游戏需要读取特定注册表项的场景，比虚拟注册表更直接

### 17. 启动时机控制

控制 Hook 安装时机，适合 Hook 过早导致不生效或冲突的场景：

```ini
[StartupTiming]
AttachMode = delay      ; immediate / delay / entrypoint
DelayMs = 1000          ; 仅 AttachMode = delay 时有效
WaitForGuiReady = false
EnableStartupWindowGate = false
```

说明：

- `immediate`：立即挂钩（默认行为）
- `delay`：等待 `DelayMs` 毫秒后再挂钩，建议从 500~2000 试起
- `entrypoint`：在 EXE 入口点首次执行时挂钩
- `WaitForGuiReady`：等待 user32/gdi32 就绪后再挂钩
- `EnableStartupWindowGate`：延迟模式下先隐藏早期窗口，挂完再放行

### 18. krkr 引擎增强

krkirikiri 引擎的额外兼容补丁：

```ini
[GLOBAL]
EnableKrkrPatch = true
KrkrBootstrapBypass = true
EnableKrkrCxdecBridge = true
KrkrPatchName = unencrypted
```

说明：

- `KrkrBootstrapBypass`：应用 FuckBootStrap 风格的 x86 字节特征补丁，绕过启动校验
- `EnableKrkrCxdecBridge`：让 cxdec 场景也能命中现有的补丁目录 / xp3 / custom pak 路径
- 支持链式归档名如 `patch.cpk>inner.xp3`，用于读取 cpk 内嵌 xp3

### 19. Siglus 密钥提取

自动提取 SiglusEngine 的 16 字节 XOR 密钥：

```ini
[SiglusKeyExtract]
Enable = true
GameexePath = Gameexe.dat
KeyOutputPath = siglus_key.txt
ShowMessageBox = true
DebugMode = false
```

说明：

- 仅 SiglusEngine 游戏有效
- 监控 `Gameexe.dat` 读取过程，在解密后的关键位置抓取密钥
- 密钥输出到 `KeyOutputPath` 指定的文件

### 20. Waffle 引擎兼容

```ini
[GLOBAL]
EnableWafflePatch = true
WaffleFixGetTextCrash = true
```

说明：

- 修复部分 Waffle 游戏在 `GetTextExtentPoint32A` 处理制表符时的文本崩溃

## 主要配置段

`CialloHook.ini` 常用配置段如下：

- `[CialloHook]`：字体、字符集、字形调整、字体映射、日繁映射
- `[TextReplace]`：文本替换（覆盖完整 GDI 文本及 UI 控件链路）
- `[WindowTitle]`：窗口标题替换
- `[StartupMessage]`：启动提示
- `[SplashImage]`：启动图片弹窗及动画
- `[StartupTiming]`：Hook 安装时机控制
- `[SiglusKeyExtract]`：SiglusEngine 密钥提取
- `[RioShiina]`：RioShiina 引擎资源覆盖与解包
- `[CodePage]`：代码页伪装
- `[FilePatch]`：补丁目录、补丁包、自定义 VFS
- `[FileSpoof]`：伪装文件或目录不存在
- `[DirectoryRedirect]`：目录重定向
- `[Registry]`：虚拟注册表注入
- `[RegistryBootstrap]`：临时真实注册表引导（退出自动回滚）
- `[GLOBAL]`：引擎兼容项（krkr / Waffle / MED / MAJIRO）
- `[LocaleEmulator]`：转区参数
- `[LoadMode]`：加载方式
- `[CialloLauncher]`：启动器设置
- `[Debug]`：日志输出

完整带注释示例见：

- `src/CialloHook/config/CialloHook_Example.ini`

## 常见问题

### 1. 放进游戏目录后完全没效果

优先检查：

- 位数是否匹配
- 当前游戏是否会加载 `version.dll` 或 `winmm.dll`
- 你使用的模式是否和配置一致
- `ini` 是否与 DLL 放在同目录
- 日志文件是否生成

### 2. 字体变了，但还是乱码

通常说明“字体”问题解决了，但“编码”问题还在。优先尝试：

- `Charset = 0x86`
- `[CodePage]` 从 `932` 到 `936`
- `[LocaleEmulator]` 启用对应区域

### 3. 字体能显示，但繁体 / 日文字形看起来别扭

这时可以优先试：

- `EnableCnJpMap = true`
- `CnJpMapJson = subs_cn_jp.json`

它更适合处理“字形借用”问题，而不是普通文本替换问题。

### 4. 补丁目录生效，但封包补丁不生效

优先检查：

- `CustomPakEnable` 是否开启
- `CustomPakCount` 和 `CustomPakName_i` 是否连续填写
- `patch.cpk` 路径是否正确
- 先把 `VFSMode` 改成 `0` 再测试
- 临时打开 `[FilePatch]` 的 `EnableLog = true` 看实际命中情况

### 5. 外部字体文件不生效

优先检查：

- 路径是否写对
- 字体文件是否真的在游戏目录可访问
- `FontName` 是否填写正确
- 游戏是否后续又创建了别的字体覆盖结果

### 6. 控制台窗口会不会影响游戏体验

会，所以发布时通常建议关掉：

```ini
LogToConsole = false
```

### 7. 想看完整配置说明

直接看：

- `src/CialloHook/config/CialloHook_Example.ini`

## 从源码构建

如果你需要自己编译：

- 环境：Windows + Visual Studio 2022 + PowerShell
- 脚本：`build_x86.bat`、`build_x64.bat`

执行：

```bat
build_x86.bat
build_x64.bat
```

默认输出：

```text
out/bin/x86/Release/
out/bin/x64/Release/
```

## 参考项目与感谢

本项目的实现和整理过程中，参考或使用了下列项目 / 组件：

- `Detours`：用于部分 API Hook、注入与调用链改写相关能力
- `Locale Emulator` / `Locale Emulator Plus`：用于转区相关能力，以及发布包中的 LE / LEP 运行时依赖
- `krkrplugin`：用于部分 kirikiri 相关兼容、补丁流处理和桥接实现
- `miniz`：用于部分压缩 / 解压支持
- `zstd`：用于 `CialloPAK` / 自定义封包链路中的高压缩比支持
- `LZMA SDK`：用于 `LZMA` 相关压缩 / 解压支持
- `Nepgear-main`：为部分 Hook 思路、补丁组织方式和兼容处理提供了参考
- `CELICA_HOOK-master`：为部分字体 / 文本 Hook 与实战补丁方案提供了参考
- `GalPatch-main`：为补丁封装、分发形式和工程整理方式提供了参考
- `RioShiinaTools-master`：为 RioShiina 引擎的 WARC 资源读取与解包方案提供了参考


