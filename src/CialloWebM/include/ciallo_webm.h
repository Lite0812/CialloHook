/*
 * ciallo_webm.h — CialloWebM 透明 WebM 解码器公共 API
 *
 * 用途：解码 VP8/VP9 WebM 文件，支持 Alpha 透明通道。
 *       输出预乘 BGRA 像素，可直接用于 UpdateLayeredWindow。
 *
 * 使用方式（DLL）：
 *   CIALLO_WEBM_HANDLE h = CialloWebM_Open(L"splash.webm");
 *   CialloWebMInfo info;
 *   CialloWebM_GetInfo(h, &info);
 *   CialloWebMFrame frame;
 *   while (CialloWebM_ReadFrame(h, &frame) == CIALLO_WEBM_OK) {
 *       // frame.pixels = BGRA 预乘数据, frame.stride = 每行字节数
 *       Sleep(frame.duration_ms);
 *   }
 *   CialloWebM_Close(h);
 */

#ifndef CIALLO_WEBM_H
#define CIALLO_WEBM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 导出宏 ---- */
#ifdef CIALLO_WEBM_EXPORTS
  #define CIALLO_WEBM_API __declspec(dllexport)
#else
  #define CIALLO_WEBM_API __declspec(dllimport)
#endif

/* ---- 返回码 ---- */
#define CIALLO_WEBM_OK              0    /* 成功 */
#define CIALLO_WEBM_EOF             1    /* 已读完所有帧 */
#define CIALLO_WEBM_ERR_OPEN       -1    /* 无法打开文件 */
#define CIALLO_WEBM_ERR_FORMAT     -2    /* 非 WebM/EBML 格式 */
#define CIALLO_WEBM_ERR_NO_VIDEO   -3    /* 找不到视频轨 */
#define CIALLO_WEBM_ERR_CODEC      -4    /* 不支持的编码（非 VP8/VP9） */
#define CIALLO_WEBM_ERR_DECODE     -5    /* 帧解码失败 */
#define CIALLO_WEBM_ERR_PARAM      -6    /* 参数无效 */
#define CIALLO_WEBM_ERR_ALLOC      -7    /* 内存分配失败 */

/* ---- 编码类型 ---- */
#define CIALLO_WEBM_CODEC_VP8       8
#define CIALLO_WEBM_CODEC_VP9       9

/* ---- 不透明句柄 ---- */
typedef struct CialloWebMDecoder* CIALLO_WEBM_HANDLE;

/* ---- 视频信息 ---- */
typedef struct CialloWebMInfo
{
    uint32_t width;            /* 视频宽度（像素） */
    uint32_t height;           /* 视频高度（像素） */
    int      codec;            /* CIALLO_WEBM_CODEC_VP8 或 VP9 */
    int      has_alpha;        /* 是否包含 Alpha 通道 */
    double   duration_sec;     /* 总时长（秒），0 表示未知 */
    double   fps;              /* 帧率（来自 DefaultDuration），0 表示可变 */
    uint32_t frame_count_hint; /* 帧数估计值，0 表示未知 */
} CialloWebMInfo;

/* ---- 单帧数据 ---- */
typedef struct CialloWebMFrame
{
    const uint8_t* pixels;     /* 预乘 BGRA 数据，行序为自上而下 */
    uint32_t       stride;     /* 每行字节数（= width * 4，可能含对齐） */
    uint32_t       width;
    uint32_t       height;
    uint32_t       duration_ms;/* 本帧显示时长（毫秒） */
    double         timestamp;  /* 本帧时间戳（秒） */
    int            is_key;     /* 是否为关键帧 */
} CialloWebMFrame;

/* ================================================================
 * 核心 API
 * ================================================================ */

/*
 * CialloWebM_Open — 从文件路径打开 WebM
 * 返回：句柄（成功）或 NULL（失败）
 */
CIALLO_WEBM_API CIALLO_WEBM_HANDLE CialloWebM_Open(const wchar_t* file_path);

/*
 * CialloWebM_OpenMemory — 从内存打开 WebM
 * data 指向的内存在解码器关闭前必须保持有效
 * 返回：句柄（成功）或 NULL（失败）
 */
CIALLO_WEBM_API CIALLO_WEBM_HANDLE CialloWebM_OpenMemory(
    const uint8_t* data, size_t size);

/*
 * CialloWebM_GetInfo — 获取视频基本信息
 * 返回：CIALLO_WEBM_OK 或错误码
 */
CIALLO_WEBM_API int CialloWebM_GetInfo(
    CIALLO_WEBM_HANDLE handle, CialloWebMInfo* out_info);

/*
 * CialloWebM_ReadFrame — 读取并解码下一帧
 *
 * 成功时 out_frame->pixels 指向内部缓冲区（下次调用后失效）。
 * 像素格式：预乘 BGRA（B 在低位），兼容 Win32 BLENDFUNCTION + AC_SRC_ALPHA。
 *
 * 返回：
 *   CIALLO_WEBM_OK   — 成功读取一帧
 *   CIALLO_WEBM_EOF  — 视频结束
 *   CIALLO_WEBM_ERR_DECODE — 解码失败（跳过损坏帧）
 */
CIALLO_WEBM_API int CialloWebM_ReadFrame(
    CIALLO_WEBM_HANDLE handle, CialloWebMFrame* out_frame);

/*
 * CialloWebM_Rewind — 倒回到第一帧（用于循环播放）
 * 返回：CIALLO_WEBM_OK 或错误码
 */
CIALLO_WEBM_API int CialloWebM_Rewind(CIALLO_WEBM_HANDLE handle);

/*
 * CialloWebM_Close — 关闭解码器，释放所有资源
 * handle 可为 NULL（安全调用）
 */
CIALLO_WEBM_API void CialloWebM_Close(CIALLO_WEBM_HANDLE handle);

/*
 * CialloWebM_HasAudio — 查询 WebM 是否包含音频轨
 * 返回: 1 = 有音频轨, 0 = 无音频轨或句柄无效
 */
CIALLO_WEBM_API int CialloWebM_HasAudio(CIALLO_WEBM_HANDLE handle);

/*
 * CialloWebM_ExtractAudioWav — 解码音频轨并写入 WAV 文件
 *
 * 将 WebM 中的 Opus/Vorbis 音频解码为 16-bit PCM，输出标准 WAV 文件。
 * 输出的 WAV 可直接用 MCI 播放（兼容 Win7）。
 * Opus 解码器随 ciallo_webm.dll 静态编译，不依赖系统 Opus 组件。
 *
 * wav_path: 输出 WAV 文件路径
 * 返回: CIALLO_WEBM_OK 成功, 或错误码
 *        CIALLO_WEBM_ERR_NO_VIDEO (-3) 表示无音频轨
 *        CIALLO_WEBM_ERR_CODEC (-4) 表示音频编码不支持（非 Vorbis）
 */
CIALLO_WEBM_API int CialloWebM_ExtractAudioWav(
    CIALLO_WEBM_HANDLE handle, const wchar_t* wav_path);

/* ================================================================
 * 辅助 API
 * ================================================================ */

/*
 * CialloWebM_GetLastError — 获取最后一次错误的文字描述
 * 返回静态字符串，不需要释放
 */
CIALLO_WEBM_API const char* CialloWebM_GetLastError(CIALLO_WEBM_HANDLE handle);

/*
 * CialloWebM_GetVersion — 获取库版本字符串
 */
CIALLO_WEBM_API const char* CialloWebM_GetVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* CIALLO_WEBM_H */
