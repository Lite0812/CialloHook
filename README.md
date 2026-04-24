# CialloHook

一个面向 Windows 系统 Galgame 游戏的通用 Hook 工具。

它主要用于：

- 替换字体、修复乱码、调整字形和字距
- 文本替换、窗口标题替换、启动提示
- 代码页伪装、Locale Emulator 转区
- 挂载补丁目录、补丁包、目录重定向、文件伪装
- 兼容部分 kirikiri / Waffle / MED / MAJIRO 场景

## 功能特性

- **字体 Hook**：支持系统字体、外部字体文件、字符集伪装、定向字体重定向
- **文本 Hook**：支持 `TextOut` / `DrawText` 等常见绘制链路的文本替换
- **窗口处理**：支持标题替换和启动时原生提示框
- **资源挂载**：支持补丁目录、补丁包、自定义 PAK / VFS
- **环境伪装**：支持代码页伪装和 Locale Emulator 转区
- **双加载模式**：支持 `proxy` 代理模式和 `loader` 启动器模式
- **配置驱动**：绝大部分行为都通过 `ini` 控制
- **日志排障**：支持文件日志和控制台日志

## 适用场景

- 日文游戏在中文系统下乱码、缺字、字重不对、字宽异常
- 汉化补丁需要替换字体，但不想回封资源
- 想直接在运行时替换文本或窗口标题
- 需要把补丁目录、`xp3`、`cpk` 或其他补丁资源接到游戏读取链
- 需要伪装区域、代码页或时区，绕过部分地区依赖问题

## 文件说明

- `version.dll` / `winmm.dll`：代理模式用的主 DLL
- `CialloHook.dll`：核心模块本体
- `CialloHook.ini`：主配置文件
- `version.ini` / `winmm.ini`：给代理名预留的同内容配置
- `CialloLauncher.exe`：启动器模式入口
- `LoaderDll.dll` / `LocaleEmulator.dll`：部分模式和转区功能所需依赖
- `subs_cn_jp.json`：日繁到简中映射示例文件
- `CialloPAK_tool.py`：`cpk` 封包/解包脚本版工具
- `CialloPAK_tool.exe`：`cpk` 封包/解包独立可执行版工具

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

1. 把 `CialloLauncher.exe`、`CialloHook.dll`、配置文件放到游戏目录
2. 修改 `TargetEXE`
3. 运行 `CialloLauncher.exe`

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

### 4. 文本替换

```ini
[TextReplace]
ReplaceCount = 1
ReadEncoding = 932
WriteEncoding = 936
Original_0 = こんにちは
Replacement_0 = 你好
```

### 5. 启用日繁映射

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

- `CnJpMapJson` 默认就是 `subs_cn_jp.json`
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

### 6. 文件补丁目录与封包

`[FilePatch]` 用来让“补丁文件优先于原始文件”。

最常见的两种补丁来源是：

- `PatchFolderName_i`：直接放在目录里的补丁文件
- `CustomPakName_i`：封装成 `cpk` 的补丁包

查找顺序大致是：

- 补丁目录
- 自定义 `cpk` 封包
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
- 想把补丁打成单文件分发时，用 `cpk` 封包
- 想同时保留“基础补丁 + 热修复覆盖”时，两者可以一起开

封包相关说明：

- `CustomPakName_i` 可以写文件名、相对路径或绝对路径
- 编号越大优先级越高，适合叠加 `patch_base.cpk`、`patch_cn.cpk`、`patch_hotfix.cpk`
- `VFSMode = 0` 一般更稳，适合发布默认值
- `VFSMode = 1` 是内存读取模式，适合有特殊性能或兼容需求时再试

如果你打算发一个单文件补丁包，推荐思路是：

1. 先在 `patch\` 目录里调试资源是否生效
2. 确认路径结构正确后再封成 `patch.cpk`
3. 发布时默认开启 `CustomPakEnable = true`

### 7. CialloPAK_tool.py / CialloPAK_tool.exe

如果你准备把补丁目录封成单个 `cpk` 文件，可以直接使用：

- `CialloPAK_tool.py`：适合自己改脚本、命令行批处理
- `CialloPAK_tool.exe`：适合直接双击或拖拽使用，发布时也更方便

两者功能基本一致，主要提供：

- 把一个目录封成 `cpk`
- 把 `cpk` 解包回目录
- 按资源相对路径从 `cpk` 里单独导出某个文件
- 生成 `manifest` 清单，保留 hash 和原始路径对应关系

最简单的用法就是拖拽：

- 把一个文件夹拖到工具上：自动封包，生成同名 `.cpk` 和 `_manifest.txt`
- 把一个 `.cpk` 拖到工具上：按 hash 方式解包
- 把一个 `.cpk` 和对应 `.txt` 清单一起拖到工具上：按原始路径解包

如果你习惯命令行，也可以直接这样用：

```bash
python CialloPAK_tool.py pack --input patch --pak patch.cpk --manifest patch_manifest.txt --dedup --compression auto
python CialloPAK_tool.py unpack --pak patch.cpk --manifest patch_manifest.txt --output patch_unpacked
python CialloPAK_tool.py read --pak patch.cpk --name script/start.ks --output start.ks
```

如果你用的是打包好的可执行版，把前面的 `python CialloPAK_tool.py` 换成：

```bash
CialloPAK_tool.exe pack --input patch --pak patch.cpk --manifest patch_manifest.txt --dedup --compression auto
```

几个实用参数：

- `--dedup`：对重复内容做复用，补丁里重复资源多时更省空间
- `--compression auto`：自动在 `raw` / `zlib` / `zstd` / `lzma` 中选更合适的压缩结果
- `--workers`：指定并行线程/进程数，默认自动按 CPU 决定
- `--parallel thread|process`：切换并行方式，默认 `thread`

注意：

- 如果要使用 `zstd` 压缩或解压，脚本版环境里需要安装 `zstandard`
- 不带 `manifest` 解包时，文件会按 hash 名称导出，不会还原原始路径
- 推荐先确认 `patch\` 目录结构正确，再封成 `cpk` 发布
- 对接 `CialloHook.ini` 时，把生成的 `patch.cpk` 写到 `CustomPakName_i` 即可

### 8. 启用代码页伪装

```ini
[CodePage]
Enable = true
FromCodePage = 932
ToCodePage = 936
```

### 9. 启用 Locale Emulator 转区

```ini
[LocaleEmulator]
Enable = true
CodePage = 932
LocaleID = 0x411
Charset = 128
Timezone = Tokyo Standard Time
```

使用这项功能时，记得同时带上：

- `LoaderDll.dll`
- `LocaleEmulator.dll`

## 主要配置段

`CialloHook.ini` 常用配置段如下：

- `[CialloHook]`：字体、字符集、字形调整、字体映射
- `[TextReplace]`：文本替换
- `[CialloHook]` 中的 `EnableCnJpMap` / `CnJpMapJson`：日繁映射
- `[WindowTitle]`：窗口标题替换
- `[StartupMessage]`：启动提示
- `[CodePage]`：代码页伪装
- `[FilePatch]`：补丁目录、补丁包、自定义 VFS
- `[FileSpoof]`：伪装文件或目录不存在
- `[DirectoryRedirect]`：目录重定向
- `[Registry]`：注册表重定向
- `[GLOBAL]`：引擎兼容项
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
