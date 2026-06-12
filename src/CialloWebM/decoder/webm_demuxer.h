/*
 * webm_demuxer.h — WebM 容器解复用器
 *
 * 在 EBML 解析器之上，处理 WebM 特有的：
 * - Segment / Info / Tracks 解析
 * - 视频轨参数提取（VP8/VP9、AlphaMode）
 * - Cluster → Block/SimpleBlock 逐帧读取
 * - BlockAdditions（VP9 Alpha 数据）提取
 */

#ifndef CIALLO_WEBM_DEMUXER_H
#define CIALLO_WEBM_DEMUXER_H

#include "ebml_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 最大支持的轨道数 ---- */
#define WEBM_MAX_TRACKS 8

/* ---- 单个 laced Block 最多包含的帧数 ---- */
#define WEBM_MAX_LACED_FRAMES 256

/* ---- 编解码器 ID ---- */
typedef enum WebmCodecType
{
    WEBM_CODEC_UNKNOWN = 0,
    WEBM_CODEC_VP8     = 8,
    WEBM_CODEC_VP9     = 9,
    WEBM_CODEC_OPUS    = 100,
    WEBM_CODEC_VORBIS  = 101
} WebmCodecType;

/* ---- 轨道信息 ---- */
typedef struct WebmTrackInfo
{
    uint64_t       track_number;
    uint64_t       track_uid;
    int            track_type;       /* EBML_TRACK_TYPE_VIDEO / AUDIO */
    WebmCodecType  codec;
    const uint8_t* codec_private;    /* CodecPrivate 数据指针 */
    size_t         codec_private_len;
    uint64_t       default_duration; /* 纳秒，0 = 可变帧率 */

    /* 视频专用 */
    uint32_t pixel_width;
    uint32_t pixel_height;
    int      alpha_mode;             /* 0 = 无 Alpha, 1 = 有 Alpha */
} WebmTrackInfo;

/* ---- Block 数据包 ---- */
typedef struct WebmPacket
{
    uint64_t       track_number;   /* 所属轨道 */
    const uint8_t* data;           /* 帧数据指针 */
    size_t         data_size;      /* 帧数据大小 */
    int64_t        timestamp_ns;   /* 绝对时间戳（纳秒） */
    int            is_key;         /* 是否关键帧 */

    /* Alpha 附加数据（VP9 Alpha 通道） */
    const uint8_t* alpha_data;     /* BlockAdditional 数据，NULL = 无 Alpha */
    size_t         alpha_size;

    /* 显示时长（来自 BlockDuration，纳秒；0 = 使用 DefaultDuration） */
    int64_t        duration_ns;
} WebmPacket;

/* ---- 解复用器状态 ---- */
typedef struct WebmDemuxer
{
    EbmlStream      stream;           /* 完整的 WebM 数据流 */
    EbmlStream      segment_stream;   /* Segment 内容子流 */

    /* Segment 级信息 */
    uint64_t        timestamp_scale;  /* TimestampScale（纳秒/单位），默认 1000000 */
    double          duration;         /* 总时长（TimestampScale 单位），0 = 未知 */

    /* 轨道 */
    WebmTrackInfo   tracks[WEBM_MAX_TRACKS];
    int             track_count;
    int             video_track_idx;  /* 主视频轨在 tracks[] 中的索引，-1 = 无 */
    int             audio_track_idx;  /* 主音频轨在 tracks[] 中的索引，-1 = 无 */

    /* Cluster 遍历状态 */
    int             initialized;      /* 是否已完成 header 解析 */
    size_t          cluster_start;    /* 当前 Cluster 在 segment_stream 中的起始位置 */
    int64_t         cluster_ts;       /* 当前 Cluster 的时间戳 */
    EbmlStream      cluster_stream;   /* 当前 Cluster 的内容子流 */
    int             in_cluster;       /* 是否正在读取 Cluster */

    /* laced Block 拆分后的待返回帧 */
    WebmPacket      pending_packets[WEBM_MAX_LACED_FRAMES];
    int             pending_count;
    int             pending_index;

    /* 错误信息 */
    char            error_msg[256];
} WebmDemuxer;

/* ================================================================
 * API
 * ================================================================ */

/*
 * webm_demuxer_init — 初始化解复用器，解析 EBML Header + Segment Header + Tracks
 * 返回: 1 成功, 0 失败（检查 demuxer->error_msg）
 */
int webm_demuxer_init(WebmDemuxer* demuxer, const uint8_t* data, size_t size);

/*
 * webm_demuxer_read_packet — 读取下一个视频 Block/SimpleBlock
 * 只返回视频轨的包（跳过音频和其他轨道）
 * 返回: 1 成功读取, 0 到达末尾
 * 注意: pkt 中的指针指向 demuxer 内部数据，不需要释放
 */
int webm_demuxer_read_packet(WebmDemuxer* demuxer, WebmPacket* pkt);

/*
 * webm_demuxer_rewind — 倒回到第一个 Cluster
 * 返回: 1 成功, 0 失败
 */
int webm_demuxer_rewind(WebmDemuxer* demuxer);

/*
 * webm_demuxer_get_video_track — 获取主视频轨道信息
 * 返回: 轨道信息指针, 或 NULL（无视频轨）
 */
const WebmTrackInfo* webm_demuxer_get_video_track(const WebmDemuxer* demuxer);

/*
 * webm_demuxer_get_audio_track — 获取主音频轨道信息
 * 返回: 轨道信息指针, 或 NULL（无音频轨）
 */
const WebmTrackInfo* webm_demuxer_get_audio_track(const WebmDemuxer* demuxer);

/*
 * webm_demuxer_read_next_packet — 读取下一个任意轨道的 Block
 * 返回所有轨道的包（不过滤），用于音频提取
 * 返回: 1 成功读取, 0 到达末尾
 */
int webm_demuxer_read_next_packet(WebmDemuxer* demuxer, WebmPacket* pkt);

#ifdef __cplusplus
}
#endif

#endif /* CIALLO_WEBM_DEMUXER_H */
