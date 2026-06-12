# CialloWebM — WebM 透明视频解码器设计文档

## 一、背景

CialloHook 的闪屏（Splash）模块原先使用 GDI+ 加载 PNG/GIF。
GIF 仅支持 256 色调色板，画质不佳。自定义 CHV 格式体积太大、播放卡顿。

本方案实现一个纯 C 的 **WebM 解码器 DLL**，支持 **VP9 + Alpha 透明通道**，
并附带一个简易 EXE 播放器用于测试。

## 二、关于"从头抄 ffmpeg 写 VP9 解码器"的评估

**强烈不建议自己实现 VP9 解码算法。** 原因：

1. VP9 规范超过 300 页，涉及超分割树、自适应算术编码、多参考帧预测、
   环路滤波、超级块分区等大量算法，ffmpeg 的 `libavcodec/vp9*.c` 总计约 15000 行
2. 性能要求极高——纯 C 无 SIMD 优化的解码器帧率会很低
3. 正确性验证困难，VP9 的 conformance test suite 有数百个测试向量
4. libvpx（Google 官方 VP9 实现）是 BSD 协议，可以直接链接使用

## 三、本方案的架构

```
┌─────────────────────────────────────────────┐
│                ciallo_webm.dll              │
│                                             │
│  ┌──────────┐   ┌──────────┐   ┌─────────┐ │
│  │ EBML     │──→│  WebM    │──→│ libvpx  │ │
│  │ Parser   │   │ Demuxer  │   │ VP9     │ │
│  │ (自写)   │   │ (自写)   │   │ (外部)  │ │
│  └──────────┘   └──────────┘   └─────────┘ │
│                      │                      │
│              ┌───────┴───────┐              │
│              │ YUV→BGRA 转换 │              │
│              │ + Alpha 合成  │              │
│              └───────────────┘              │
└──────────────────┬──────────────────────────┘
                   │ C API
       ┌───────────┴───────────┐
       │  CialloHook Splash    │  (或 ciallo_webm_player.exe)
       │  UpdateLayeredWindow  │
       └───────────────────────┘
```

**自写部分（~1200 行 C）：**
- EBML 变长整数解析器
- WebM 容器解析（Segment → Tracks → Clusters → Blocks）
- VP9 Alpha 通道的 BlockAdditions 提取
- YUV420 → 预乘 BGRA 颜色空间转换

**外部依赖（仅一个）：**
- **libvpx** — Google 官方 VP8/VP9 编解码库，BSD 协议
- 仅需要解码部分（vpxdec），编译后约 200~400KB
- 获取方式见下方"构建指南"

## 四、DLL 公开 API

```c
// 打开 WebM 文件，返回解码器句柄
CIALLO_WEBM_HANDLE CialloWebM_Open(const wchar_t* filePath);

// 从内存打开
CIALLO_WEBM_HANDLE CialloWebM_OpenMemory(const uint8_t* data, size_t size);

// 获取视频信息
int CialloWebM_GetInfo(CIALLO_WEBM_HANDLE h, CialloWebMInfo* info);

// 读取下一帧，输出预乘 BGRA 像素数据（兼容 UpdateLayeredWindow）
int CialloWebM_ReadFrame(CIALLO_WEBM_HANDLE h, CialloWebMFrame* frame);

// 跳回第一帧（用于循环播放）
int CialloWebM_Rewind(CIALLO_WEBM_HANDLE h);

// 关闭并释放资源
void CialloWebM_Close(CIALLO_WEBM_HANDLE h);
```

## 五、VP9 Alpha 通道原理

WebM 中 VP9 透明度的编码方式：
1. 视频轨设置 `AlphaMode = 1`
2. 每个 Block/SimpleBlock 的 **BlockAdditions** 中附加 `BlockAdditional`
   （`BlockAddID = 1`），内容是一个独立的 VP9 码流——仅编码 Alpha 平面
3. 解码时：
   - 主 Block 数据 → libvpx 解码 → YUV420 颜色帧
   - BlockAdditional 数据 → libvpx 解码 → Y 平面 = Alpha 值
   - 合成为 BGRA：RGB 来自颜色帧的 YUV→RGB，A 来自 Alpha 帧的 Y

## 六、与 CialloHook 闪屏模块的集成

现有 `RunSplashAnimation()` 的 GDI+ 管线可以保持不变。
在 `ShowSplashFromEntryPoint()` 中增加 WebM 分支：

```
if 文件扩展名是 .webm:
    CialloWebM_Open → 获取宽高
    创建 WS_EX_LAYERED 窗口（与现有代码相同）
    定时器回调中：CialloWebM_ReadFrame → UpdateLayeredWindow
else:
    走现有 GDI+ PNG/GIF 流程
```

INI 配置只需将 `ImageFile = splash.webm` 即可触发。

## 七、构建指南

### 获取 libvpx

**方法 A — vcpkg（最简单）：**
```
vcpkg install libvpx:x86-windows-static libvpx:x64-windows-static
```

**方法 B — 手动编译：**
1. 克隆 https://chromium.googlesource.com/webm/libvpx
2. 使用 MSYS2 + MSVC 工具链编译：
   ```
   ./configure --target=x86-win32-vs17 --enable-static --disable-examples
   make
   ```
3. 将 `vpxdec.lib` 复制到 `third/libvpx/lib.X86/vpx.lib`
4. 将头文件复制到 `third/libvpx/include/vpx/`

### 编译本项目

在 Visual Studio 中打开 `CialloHook.sln`，新增的两个项目：
- **CialloWebM** — 编译为 `ciallo_webm.dll`
- **CialloWebMPlayer** — 编译为 `ciallo_webm_player.exe`

## 八、文件清单

```
src/CialloWebM/
  include/ciallo_webm.h           公开 API 头文件
  decoder/ebml_parser.h           EBML 格式解析器头文件
  decoder/ebml_parser.c           EBML 格式解析器实现
  decoder/webm_demuxer.h          WebM 容器解复用器头文件
  decoder/webm_demuxer.c          WebM 容器解复用器实现
  decoder/ciallo_webm_decoder.c   DLL 主体：组装解复用 + libvpx 解码
  decoder/ciallo_webm.def         DLL 导出定义
  player/ciallo_webm_player.c     独立 EXE 播放器（透明窗口）
  projects/CialloWebM.vcxproj     DLL 工程文件
  projects/CialloWebMPlayer.vcxproj 播放器工程文件
third/libvpx/
  include/vpx/vpx_decoder.h      libvpx 解码器头（需用户自行放入）
  lib.X86/vpx.lib                 x86 静态库（需用户自行放入）
  lib.X64/vpx.lib                 x64 静态库（需用户自行放入）
```
