/*
 * ciallo_webm_decoder.c — CialloWebM DLL 主体
 *
 * 组装三个模块：
 * 1. WebM 解复用器 → 提取 VP9 压缩帧 + Alpha 帧
 * 2. libvpx → 解码 VP9 帧为 YUV420
 * 3. YUV→BGRA 转换 + Alpha 合成 → 输出预乘 BGRA
 */

/* CIALLO_WEBM_EXPORTS is defined by the project preprocessor settings */
#include "../include/ciallo_webm.h"
#include "webm_demuxer.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ---- libvpx 头文件 ---- */
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"

/* ---- Vorbis 迷你解码器 ---- */
#include "vorbis_mini.h"
#include "opus_audio.h"

#define CIALLO_WEBM_VERSION "1.0.0"

/* ================================================================
 * 解码器内部结构
 * ================================================================ */

typedef struct CialloWebMDecoder
{
    /* 文件数据（内存映射或直接持有） */
    uint8_t*        file_data;      /* 文件数据 */
    size_t          file_size;
    int             owns_data;      /* 是否由我们分配（需释放） */
    HANDLE          file_handle;    /* 文件映射句柄 */
    HANDLE          mapping;        /* 映射对象句柄 */

    /* 解复用器 */
    WebmDemuxer     demuxer;

    /* VP9 解码器（颜色帧） */
    vpx_codec_ctx_t color_codec;
    int             color_codec_init;

    /* VP9 解码器（Alpha 帧，仅在有 Alpha 时使用） */
    vpx_codec_ctx_t alpha_codec;
    int             alpha_codec_init;

    /* 输出帧缓冲（预乘 BGRA） */
    uint8_t*        frame_buf;
    uint32_t        frame_buf_stride;
    uint32_t        frame_buf_size;

    /* 视频信息 */
    uint32_t        width;
    uint32_t        height;
    int             has_alpha;
    int             codec_type; /* CIALLO_WEBM_CODEC_VP8 / VP9 */

    /* 错误信息 */
    char            last_error[256];
} CialloWebMDecoder;

/* ---- 错误设置 ---- */
static void decoder_set_error(CialloWebMDecoder* d, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(d->last_error, sizeof(d->last_error), fmt, args);
    va_end(args);
}

/* ================================================================
 * YUV420 → 预乘 BGRA 转换
 *
 * BT.601 系数（标准 VP9 色彩空间）：
 *   R = Y + 1.402 * (V - 128)
 *   G = Y - 0.344 * (U - 128) - 0.714 * (V - 128)
 *   B = Y + 1.772 * (U - 128)
 *
 * 输出为预乘 Alpha（UpdateLayeredWindow + ULW_ALPHA 所需）：
 *   R' = R * A / 255
 *   G' = G * A / 255
 *   B' = B * A / 255
 * ================================================================ */

static inline uint8_t clamp_u8(int v)
{
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static void yuv420_to_premul_bgra(
    const uint8_t* y_plane, int y_stride,
    const uint8_t* u_plane, int u_stride,
    const uint8_t* v_plane, int v_stride,
    const uint8_t* a_plane, int a_stride,  /* NULL = 不透明（A=255） */
    uint8_t* bgra, int bgra_stride,
    int width, int height)
{
    for (int row = 0; row < height; ++row)
    {
        const uint8_t* yp = y_plane + row * y_stride;
        const uint8_t* up = u_plane + (row / 2) * u_stride;
        const uint8_t* vp = v_plane + (row / 2) * v_stride;
        const uint8_t* ap = a_plane ? (a_plane + row * a_stride) : NULL;
        uint8_t* dst = bgra + row * bgra_stride;

        for (int col = 0; col < width; ++col)
        {
            int Y = yp[col];
            int U = up[col / 2] - 128;
            int V = vp[col / 2] - 128;

            /* 定点数运算（精度 >> 10） */
            int R = Y + ((1436 * V) >> 10);
            int G = Y - ((352 * U + 731 * V) >> 10);
            int B = Y + ((1815 * U) >> 10);

            uint8_t r = clamp_u8(R);
            uint8_t g = clamp_u8(G);
            uint8_t b = clamp_u8(B);
            uint8_t a = ap ? ap[col] : 255;

            if (a == 255)
            {
                dst[0] = b;
                dst[1] = g;
                dst[2] = r;
                dst[3] = 255;
            }
            else if (a == 0)
            {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
            }
            else
            {
                /* 预乘 Alpha */
                dst[0] = (uint8_t)((b * a + 127) / 255);
                dst[1] = (uint8_t)((g * a + 127) / 255);
                dst[2] = (uint8_t)((r * a + 127) / 255);
                dst[3] = a;
            }

            dst += 4;
        }
    }
}

/* ================================================================
 * 内部：VP9 解码器初始化
 * ================================================================ */

static int init_vpx_codec(vpx_codec_ctx_t* ctx, int codec_type,
                          CialloWebMDecoder* d)
{
    const vpx_codec_iface_t* iface = NULL;

    if (codec_type == CIALLO_WEBM_CODEC_VP9)
    {
        iface = vpx_codec_vp9_dx();
    }
    else if (codec_type == CIALLO_WEBM_CODEC_VP8)
    {
        iface = vpx_codec_vp8_dx();
    }
    else
    {
        decoder_set_error(d, "Unsupported codec type: %d", codec_type);
        return 0;
    }

    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.threads = 1; /* 闪屏用途，单线程足够 */

    vpx_codec_err_t err = vpx_codec_dec_init(ctx, iface, &cfg, 0);
    if (err != VPX_CODEC_OK)
    {
        decoder_set_error(d, "vpx_codec_dec_init failed: %s",
                          vpx_codec_err_to_string(err));
        return 0;
    }

    return 1;
}

/* ---- 解码一个帧包 ---- */
static vpx_image_t* decode_vpx_frame(vpx_codec_ctx_t* ctx,
                                      const uint8_t* data, size_t size,
                                      CialloWebMDecoder* d)
{
    vpx_codec_err_t err = vpx_codec_decode(ctx, data, (unsigned int)size,
                                            NULL, 0);
    if (err != VPX_CODEC_OK)
    {
        decoder_set_error(d, "vpx_codec_decode failed: %s",
                          vpx_codec_err_to_string(err));
        return NULL;
    }

    vpx_codec_iter_t iter = NULL;
    vpx_image_t* img = vpx_codec_get_frame(ctx, &iter);
    return img;
}

/* ================================================================
 * 文件打开辅助
 * ================================================================ */

static CialloWebMDecoder* alloc_decoder(void)
{
    CialloWebMDecoder* d = (CialloWebMDecoder*)calloc(1, sizeof(*d));
    return d;
}

static int open_common(CialloWebMDecoder* d)
{
    /* 初始化解复用器 */
    if (!webm_demuxer_init(&d->demuxer, d->file_data, d->file_size))
    {
        decoder_set_error(d, "WebM demuxer init failed: %s",
                          d->demuxer.error_msg);
        return 0;
    }

    const WebmTrackInfo* vt = webm_demuxer_get_video_track(&d->demuxer);
    if (!vt)
    {
        decoder_set_error(d, "No video track found");
        return 0;
    }

    d->width = vt->pixel_width;
    d->height = vt->pixel_height;
    d->has_alpha = vt->alpha_mode;
    d->codec_type = (int)vt->codec;

    /* 初始化 VP9 颜色解码器 */
    if (!init_vpx_codec(&d->color_codec, d->codec_type, d))
    {
        return 0;
    }
    d->color_codec_init = 1;

    /* 如果有 Alpha，初始化第二个解码器 */
    if (d->has_alpha)
    {
        if (!init_vpx_codec(&d->alpha_codec, d->codec_type, d))
        {
            return 0;
        }
        d->alpha_codec_init = 1;
    }

    /* 分配帧缓冲 */
    d->frame_buf_stride = d->width * 4;
    d->frame_buf_size = d->frame_buf_stride * d->height;
    d->frame_buf = (uint8_t*)malloc(d->frame_buf_size);
    if (!d->frame_buf)
    {
        decoder_set_error(d, "Frame buffer allocation failed (%u bytes)",
                          d->frame_buf_size);
        return 0;
    }

    return 1;
}

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

CIALLO_WEBM_API CIALLO_WEBM_HANDLE CialloWebM_Open(const wchar_t* file_path)
{
    if (!file_path)
    {
        return NULL;
    }

    CialloWebMDecoder* d = alloc_decoder();
    if (!d) return NULL;

    /* 使用内存映射打开文件（只读，零拷贝） */
    d->file_handle = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ,
                                  NULL, OPEN_EXISTING, 0, NULL);
    if (d->file_handle == INVALID_HANDLE_VALUE)
    {
        decoder_set_error(d, "Cannot open file (GetLastError=%lu)",
                          GetLastError());
        CialloWebM_Close(d);
        return NULL;
    }

    DWORD file_size_high = 0;
    DWORD file_size_low = GetFileSize(d->file_handle, &file_size_high);
    if (file_size_low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    {
        decoder_set_error(d, "GetFileSize failed");
        CialloWebM_Close(d);
        return NULL;
    }
    d->file_size = (size_t)file_size_low | ((size_t)file_size_high << 32);

    d->mapping = CreateFileMappingW(d->file_handle, NULL, PAGE_READONLY,
                                     0, 0, NULL);
    if (!d->mapping)
    {
        decoder_set_error(d, "CreateFileMapping failed");
        CialloWebM_Close(d);
        return NULL;
    }

    d->file_data = (uint8_t*)MapViewOfFile(d->mapping, FILE_MAP_READ,
                                            0, 0, 0);
    if (!d->file_data)
    {
        decoder_set_error(d, "MapViewOfFile failed");
        CialloWebM_Close(d);
        return NULL;
    }

    d->owns_data = 0; /* 内存映射，不需要 free */

    if (!open_common(d))
    {
        CialloWebM_Close(d);
        return NULL;
    }

    return d;
}

CIALLO_WEBM_API CIALLO_WEBM_HANDLE CialloWebM_OpenMemory(
    const uint8_t* data, size_t size)
{
    if (!data || size == 0)
    {
        return NULL;
    }

    CialloWebMDecoder* d = alloc_decoder();
    if (!d) return NULL;

    /* 直接使用调用方的内存（零拷贝），调用方保证生命周期 */
    d->file_data = (uint8_t*)data;
    d->file_size = size;
    d->owns_data = 0;

    if (!open_common(d))
    {
        CialloWebM_Close(d);
        return NULL;
    }

    return d;
}

CIALLO_WEBM_API int CialloWebM_GetInfo(
    CIALLO_WEBM_HANDLE handle, CialloWebMInfo* out_info)
{
    if (!handle || !out_info)
    {
        return CIALLO_WEBM_ERR_PARAM;
    }

    CialloWebMDecoder* d = handle;
    const WebmTrackInfo* vt = webm_demuxer_get_video_track(&d->demuxer);

    memset(out_info, 0, sizeof(*out_info));
    out_info->width = d->width;
    out_info->height = d->height;
    out_info->codec = d->codec_type;
    out_info->has_alpha = d->has_alpha;

    if (d->demuxer.duration > 0.0 && d->demuxer.timestamp_scale > 0)
    {
        out_info->duration_sec = d->demuxer.duration
            * (double)d->demuxer.timestamp_scale / 1e9;
    }

    if (vt && vt->default_duration > 0)
    {
        out_info->fps = 1e9 / (double)vt->default_duration;
    }

    return CIALLO_WEBM_OK;
}

CIALLO_WEBM_API int CialloWebM_ReadFrame(
    CIALLO_WEBM_HANDLE handle, CialloWebMFrame* out_frame)
{
    if (!handle || !out_frame)
    {
        return CIALLO_WEBM_ERR_PARAM;
    }

    CialloWebMDecoder* d = handle;
    WebmPacket pkt;

    if (!webm_demuxer_read_packet(&d->demuxer, &pkt))
    {
        return CIALLO_WEBM_EOF;
    }

    /* 解码颜色帧 */
    vpx_image_t* color_img = decode_vpx_frame(&d->color_codec,
                                               pkt.data, pkt.data_size, d);
    if (!color_img)
    {
        return CIALLO_WEBM_ERR_DECODE;
    }

    /* 确保尺寸匹配 */
    uint32_t fw = color_img->d_w;
    uint32_t fh = color_img->d_h;
    if (fw != d->width || fh != d->height)
    {
        /* 分辨率变化，重新分配缓冲 */
        d->width = fw;
        d->height = fh;
        d->frame_buf_stride = fw * 4;
        d->frame_buf_size = d->frame_buf_stride * fh;
        uint8_t* new_buf = (uint8_t*)realloc(d->frame_buf, d->frame_buf_size);
        if (!new_buf)
        {
            return CIALLO_WEBM_ERR_ALLOC;
        }
        d->frame_buf = new_buf;
    }

    /* 解码 Alpha 帧（如果有） */
    const uint8_t* alpha_y_plane = NULL;
    int alpha_y_stride = 0;

    if (d->has_alpha && pkt.alpha_data && pkt.alpha_size > 0)
    {
        vpx_image_t* alpha_img = decode_vpx_frame(&d->alpha_codec,
                                                    pkt.alpha_data,
                                                    pkt.alpha_size, d);
        if (alpha_img)
        {
            alpha_y_plane = alpha_img->planes[VPX_PLANE_Y];
            alpha_y_stride = alpha_img->stride[VPX_PLANE_Y];
        }
        /* Alpha 解码失败时继续，按不透明处理 */
    }

    /* YUV → 预乘 BGRA */
    yuv420_to_premul_bgra(
        color_img->planes[VPX_PLANE_Y], color_img->stride[VPX_PLANE_Y],
        color_img->planes[VPX_PLANE_U], color_img->stride[VPX_PLANE_U],
        color_img->planes[VPX_PLANE_V], color_img->stride[VPX_PLANE_V],
        alpha_y_plane, alpha_y_stride,
        d->frame_buf, d->frame_buf_stride,
        fw, fh);

    /* 填充输出 */
    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->pixels = d->frame_buf;
    out_frame->stride = d->frame_buf_stride;
    out_frame->width = fw;
    out_frame->height = fh;
    out_frame->is_key = pkt.is_key;
    out_frame->timestamp = (double)pkt.timestamp_ns / 1e9;

    /* 计算帧持续时间 */
    if (pkt.duration_ns > 0)
    {
        out_frame->duration_ms = (uint32_t)(pkt.duration_ns / 1000000);
    }
    else
    {
        const WebmTrackInfo* vt = webm_demuxer_get_video_track(&d->demuxer);
        if (vt && vt->default_duration > 0)
        {
            out_frame->duration_ms = (uint32_t)(vt->default_duration / 1000000);
        }
        else
        {
            out_frame->duration_ms = 33; /* 默认约 30fps */
        }
    }

    return CIALLO_WEBM_OK;
}

CIALLO_WEBM_API int CialloWebM_Rewind(CIALLO_WEBM_HANDLE handle)
{
    if (!handle)
    {
        return CIALLO_WEBM_ERR_PARAM;
    }

    CialloWebMDecoder* d = handle;

    /* 重置 libvpx 解码器状态 */
    if (d->color_codec_init)
    {
        vpx_codec_destroy(&d->color_codec);
        d->color_codec_init = 0;
        if (!init_vpx_codec(&d->color_codec, d->codec_type, d))
        {
            return CIALLO_WEBM_ERR_CODEC;
        }
        d->color_codec_init = 1;
    }

    if (d->alpha_codec_init)
    {
        vpx_codec_destroy(&d->alpha_codec);
        d->alpha_codec_init = 0;
        if (!init_vpx_codec(&d->alpha_codec, d->codec_type, d))
        {
            return CIALLO_WEBM_ERR_CODEC;
        }
        d->alpha_codec_init = 1;
    }

    /* 重置解复用器到第一个 Cluster */
    if (!webm_demuxer_rewind(&d->demuxer))
    {
        return CIALLO_WEBM_ERR_FORMAT;
    }

    return CIALLO_WEBM_OK;
}

CIALLO_WEBM_API void CialloWebM_Close(CIALLO_WEBM_HANDLE handle)
{
    if (!handle) return;

    CialloWebMDecoder* d = handle;

    if (d->color_codec_init)
    {
        vpx_codec_destroy(&d->color_codec);
    }
    if (d->alpha_codec_init)
    {
        vpx_codec_destroy(&d->alpha_codec);
    }

    if (d->frame_buf)
    {
        free(d->frame_buf);
    }

    /* 释放文件数据 */
    if (d->file_data && d->mapping)
    {
        /* 内存映射 */
        UnmapViewOfFile(d->file_data);
    }
    else if (d->file_data && d->owns_data)
    {
        free(d->file_data);
    }

    if (d->mapping) CloseHandle(d->mapping);
    if (d->file_handle && d->file_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(d->file_handle);
    }

    free(d);
}

CIALLO_WEBM_API int CialloWebM_HasAudio(CIALLO_WEBM_HANDLE handle)
{
    if (!handle) return 0;
    return webm_demuxer_get_audio_track(&handle->demuxer) != NULL ? 1 : 0;
}

/* ---- WAV file writing helpers ---- */
static void wav_write_u16(uint8_t* p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wav_write_u32(uint8_t* p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

CIALLO_WEBM_API int CialloWebM_ExtractAudioWav(
    CIALLO_WEBM_HANDLE handle, const wchar_t* wav_path)
{
    if (!handle || !wav_path) return CIALLO_WEBM_ERR_PARAM;

    const WebmTrackInfo* at = webm_demuxer_get_audio_track(&handle->demuxer);
    if (!at) return CIALLO_WEBM_ERR_NO_VIDEO; /* no audio track */

    if (at->codec == WEBM_CODEC_OPUS)
    {
        int rc = ciallo_extract_opus_wav(handle->file_data, handle->file_size,
                                         at, wav_path,
                                         handle->last_error,
                                         sizeof(handle->last_error));
        return rc;
    }

    if (at->codec != WEBM_CODEC_VORBIS)
    {
        decoder_set_error(handle, "Audio codec not supported (only Opus/Vorbis)");
        return CIALLO_WEBM_ERR_CODEC;
    }

    if (!at->codec_private || at->codec_private_len == 0)
    {
        decoder_set_error(handle, "No CodecPrivate for audio track");
        return CIALLO_WEBM_ERR_FORMAT;
    }

    /* Create Vorbis decoder */
    VorbisDecoder* vdec = vorbis_create(at->codec_private, at->codec_private_len);
    if (!vdec)
    {
        decoder_set_error(handle, "Failed to initialize Vorbis decoder");
        return CIALLO_WEBM_ERR_CODEC;
    }

    int channels = vorbis_get_channels(vdec);
    int sample_rate = vorbis_get_sample_rate(vdec);
    if (channels <= 0 || sample_rate <= 0)
    {
        vorbis_destroy(vdec);
        return CIALLO_WEBM_ERR_CODEC;
    }

    /* Create a separate demuxer for audio extraction (to not disturb video state) */
    WebmDemuxer audio_demuxer;
    if (!webm_demuxer_init(&audio_demuxer, handle->file_data, handle->file_size))
    {
        vorbis_destroy(vdec);
        return CIALLO_WEBM_ERR_FORMAT;
    }

    /* Collect decoded PCM */
    int pcm_cap = sample_rate * 30 * channels; /* pre-allocate ~30 sec */
    int16_t* pcm_data = (int16_t*)malloc(pcm_cap * sizeof(int16_t));
    int pcm_total = 0; /* total samples (all channels interleaved) */

    WebmPacket pkt;
    while (webm_demuxer_read_next_packet(&audio_demuxer, &pkt))
    {
        /* Only process audio packets */
        if (pkt.track_number != at->track_number) continue;

        float* decoded = NULL;
        int n_samples = vorbis_decode_packet(vdec, pkt.data, pkt.data_size, &decoded);
        if (n_samples <= 0 || !decoded) continue;

        int needed = pcm_total + n_samples * channels;
        while (needed > pcm_cap)
        {
            pcm_cap *= 2;
            int16_t* tmp = (int16_t*)realloc(pcm_data, pcm_cap * sizeof(int16_t));
            if (!tmp) { free(pcm_data); vorbis_destroy(vdec); return CIALLO_WEBM_ERR_ALLOC; }
            pcm_data = tmp;
        }

        /* Convert float to 16-bit PCM */
        for (int i = 0; i < n_samples * channels; i++)
        {
            float s = decoded[i] * 32767.0f;
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            pcm_data[pcm_total++] = (int16_t)s;
        }
    }

    vorbis_destroy(vdec);

    if (pcm_total == 0)
    {
        free(pcm_data);
        decoder_set_error(handle, "No audio samples decoded");
        return CIALLO_WEBM_ERR_DECODE;
    }

    /* Write WAV file */
    uint32_t data_size = pcm_total * sizeof(int16_t);
    uint32_t file_size = 44 + data_size;

    HANDLE hf = CreateFileW(wav_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hf == INVALID_HANDLE_VALUE)
    {
        free(pcm_data);
        return CIALLO_WEBM_ERR_OPEN;
    }

    /* WAV header (44 bytes) */
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);
    wav_write_u32(hdr + 4, file_size - 8);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wav_write_u32(hdr + 16, 16); /* fmt chunk size */
    wav_write_u16(hdr + 20, 1);  /* PCM format */
    wav_write_u16(hdr + 22, (uint16_t)channels);
    wav_write_u32(hdr + 24, (uint32_t)sample_rate);
    wav_write_u32(hdr + 28, (uint32_t)(sample_rate * channels * 2)); /* byte rate */
    wav_write_u16(hdr + 32, (uint16_t)(channels * 2)); /* block align */
    wav_write_u16(hdr + 34, 16); /* bits per sample */
    memcpy(hdr + 36, "data", 4);
    wav_write_u32(hdr + 40, data_size);

    DWORD written;
    WriteFile(hf, hdr, 44, &written, NULL);
    WriteFile(hf, pcm_data, data_size, &written, NULL);
    CloseHandle(hf);
    free(pcm_data);

    return CIALLO_WEBM_OK;
}

CIALLO_WEBM_API const char* CialloWebM_GetLastError(CIALLO_WEBM_HANDLE handle)
{
    if (!handle) return "Invalid handle";
    return handle->last_error;
}

CIALLO_WEBM_API const char* CialloWebM_GetVersion(void)
{
    return CIALLO_WEBM_VERSION;
}

/* ---- DLL 入口点 ---- */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    (void)hModule;
    (void)lpReserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    }
    return TRUE;
}
