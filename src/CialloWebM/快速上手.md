# CialloWebM 快速上手

## 制作带透明通道的 WebM 文件

### 方法 1：ffmpeg（推荐）

从带 Alpha 的视频源（如 .mov ProRes 4444 或 .webm）转换：
```
ffmpeg -i input_with_alpha.mov -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0 -b:v 1M output.webm
```

从 PNG 序列帧合成：
```
ffmpeg -framerate 30 -i frames/frame_%04d.png -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0 -b:v 2M splash.webm
```

从 GIF 转换（自动提取 GIF 的透明区域）：
```
ffmpeg -i input.gif -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0 -b:v 500K splash.webm
```

**关键参数说明：**
- `-pix_fmt yuva420p` — 启用 Alpha 通道（不加这个就没透明度）
- `-auto-alt-ref 0` — Alpha 视频必须关闭此选项
- `-b:v` — 视频码率，闪屏动画 500K~2M 足够

### 方法 2：Adobe After Effects
导出时选择 WebM 格式，勾选"包含 Alpha 通道"。

### 方法 3：Blender
渲染输出选择 FFmpeg → WebM → 编码为 VP9 → 颜色模式 RGBA。

## 在 CialloHook 中使用

`CialloHook.ini`:
```ini
[SplashImage]
Enable = true
ImageFile = splash.webm
Width = 800
Height = 600
Position = 1
Opacity = 100
DurationMs = 0
```

将 `splash.webm` 放到补丁目录（patch 文件夹）或游戏根目录。
将 `ciallo_webm.dll` 放到游戏根目录（与 CialloHook DLL 同目录）。

## 文件大小对比

同一个 5 秒 800x600 带透明度的动画：
| 格式            | 体积      | 画质           |
|-----------------|-----------|----------------|
| GIF (256色)     | ~2.5 MB   | 色彩严重失真   |
| PNG 序列帧      | ~15 MB    | 无损           |
| CHV (自定义)    | ~8 MB     | 无损但解码慢   |
| **WebM VP9+α**  | **~300KB**| 接近无损       |

WebM VP9 的体积优势非常明显。
