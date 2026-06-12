/*
 * vorbis_mini.h — 最小 Vorbis 解码器（WebM 闪屏音频专用）
 *
 * 功能：解码 WebM 中的 Vorbis 音频轨到 PCM
 * 输入：Matroska CodecPrivate + 原始 Vorbis 数据包
 * 输出：交错 16-bit PCM
 *
 * 限制：
 * - 仅支持 Floor type 1（最常见）
 * - 仅支持 Residue type 0/1/2
 * - 不支持 Floor type 0（极少见）
 * - 不支持流式播放（一次性解码全部）
 */

#ifndef VORBIS_MINI_H
#define VORBIS_MINI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VorbisDecoder VorbisDecoder;

/* 从 Matroska/WebM CodecPrivate 初始化解码器
 * codec_private 格式: [0x02] [size1 xiph] [size2 xiph] [hdr1] [hdr2] [hdr3]
 * 返回: 解码器句柄, NULL = 失败 */
VorbisDecoder* vorbis_create(const uint8_t* codec_private, size_t len);

/* 解码一个原始 Vorbis 数据包
 * 成功返回输出的样本数（每通道），失败返回负值
 * *out_pcm 指向内部缓冲区，下次调用后失效 */
int vorbis_decode_packet(VorbisDecoder* dec,
    const uint8_t* packet, size_t packet_len,
    float** out_pcm);

int vorbis_get_channels(const VorbisDecoder* dec);
int vorbis_get_sample_rate(const VorbisDecoder* dec);

void vorbis_destroy(VorbisDecoder* dec);

#ifdef __cplusplus
}
#endif

#endif /* VORBIS_MINI_H */
