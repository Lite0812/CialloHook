/*
 * webm_demuxer.c — WebM 容器解复用器实现
 *
 * 解析流程：
 * 1. 验证 EBML Header（DocType = "webm"）
 * 2. 打开 Segment，解析 Info（TimestampScale, Duration）
 * 3. 解析 Tracks，找到 VP8/VP9 视频轨（含 AlphaMode 检测）
 * 4. 逐 Cluster 读取 SimpleBlock / BlockGroup，提取视频帧数据
 * 5. 对于有 Alpha 的 VP9，从 BlockAdditions 中提取 Alpha 帧数据
 */

#include "webm_demuxer.h"
#include <string.h>
#include <stdio.h>

/* ---- 内部辅助 ---- */

static void set_error(WebmDemuxer* d, const char* msg)
{
    strncpy(d->error_msg, msg, sizeof(d->error_msg) - 1);
    d->error_msg[sizeof(d->error_msg) - 1] = '\0';
}

/* 判断 CodecID 字符串 */
static WebmCodecType parse_codec_id(const char* str, size_t len)
{
    if (len >= 5 && memcmp(str, "V_VP8", 5) == 0 && (len == 5 || str[5] == '\0'))
        return WEBM_CODEC_VP8;
    if (len >= 5 && memcmp(str, "V_VP9", 5) == 0 && (len == 5 || str[5] == '\0'))
        return WEBM_CODEC_VP9;
    if (len >= 6 && memcmp(str, "A_OPUS", 6) == 0)
        return WEBM_CODEC_OPUS;
    if (len >= 8 && memcmp(str, "A_VORBIS", 8) == 0)
        return WEBM_CODEC_VORBIS;
    return WEBM_CODEC_UNKNOWN;
}

/* ---- EBML Header 验证 ---- */

static int parse_ebml_header(WebmDemuxer* d, EbmlStream* s)
{
    EbmlElement header_elem;
    if (!ebml_read_element(s, &header_elem) ||
        header_elem.id != EBML_ID_HEADER)
    {
        set_error(d, "Not an EBML file: missing EBML header");
        return 0;
    }

    EbmlStream hsub;
    ebml_sub_stream(s, &header_elem, &hsub);

    int found_doctype = 0;
    EbmlElement child;
    while (ebml_read_element(&hsub, &child))
    {
        if (child.id == EBML_ID_DOCTYPE && child.size > 0)
        {
            size_t slen = 0;
            const char* doctype = ebml_read_string(&hsub, (size_t)child.size, &slen);
            if (slen >= 4 && memcmp(doctype, "webm", 4) == 0)
            {
                found_doctype = 1;
            }
            else if (slen >= 8 && memcmp(doctype, "matroska", 8) == 0)
            {
                found_doctype = 1; /* Matroska 也兼容 */
            }
        }
        else
        {
            ebml_skip_element(&hsub, &child);
        }
    }

    if (!found_doctype)
    {
        set_error(d, "Not a WebM file: DocType is not 'webm' or 'matroska'");
        return 0;
    }

    /* 跳过整个 EBML Header */
    ebml_skip_element(s, &header_elem);
    return 1;
}

/* ---- Tracks 解析 ---- */

static void parse_video_element(EbmlStream* sub, WebmTrackInfo* track)
{
    EbmlElement child;
    while (ebml_read_element(sub, &child))
    {
        switch (child.id)
        {
        case EBML_ID_PIXELWIDTH:
            track->pixel_width = (uint32_t)ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_PIXELHEIGHT:
            track->pixel_height = (uint32_t)ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_ALPHAMODE:
            track->alpha_mode = (int)ebml_read_uint(sub, (size_t)child.size);
            break;
        default:
            ebml_skip_element(sub, &child);
            break;
        }
    }
}

static int parse_track_entry(WebmDemuxer* d, EbmlStream* sub)
{
    if (d->track_count >= WEBM_MAX_TRACKS)
    {
        return 1; /* 静默忽略超出的轨道 */
    }

    WebmTrackInfo* track = &d->tracks[d->track_count];
    memset(track, 0, sizeof(*track));

    EbmlElement child;
    while (ebml_read_element(sub, &child))
    {
        switch (child.id)
        {
        case EBML_ID_TRACKNUMBER:
            track->track_number = ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_TRACKUID:
            track->track_uid = ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_TRACKTYPE:
            track->track_type = (int)ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_CODECID:
        {
            size_t slen = 0;
            const char* cid = ebml_read_string(sub, (size_t)child.size, &slen);
            track->codec = parse_codec_id(cid, slen);
            break;
        }
        case EBML_ID_CODECPRIVATE:
            track->codec_private = ebml_read_binary(sub, (size_t)child.size);
            track->codec_private_len = (size_t)child.size;
            break;
        case EBML_ID_DEFAULTDURATION:
            track->default_duration = ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_VIDEO:
        {
            EbmlStream vsub;
            ebml_sub_stream(sub, &child, &vsub);
            parse_video_element(&vsub, track);
            ebml_skip_element(sub, &child);
            break;
        }
        default:
            ebml_skip_element(sub, &child);
            break;
        }
    }

    d->track_count++;
    return 1;
}

static int parse_tracks(WebmDemuxer* d, EbmlStream* sub)
{
    EbmlElement child;
    while (ebml_read_element(sub, &child))
    {
        if (child.id == EBML_ID_TRACKENTRY)
        {
            EbmlStream tsub;
            ebml_sub_stream(sub, &child, &tsub);
            parse_track_entry(d, &tsub);
            ebml_skip_element(sub, &child);
        }
        else
        {
            ebml_skip_element(sub, &child);
        }
    }

    /* 找主视频轨 */
    d->video_track_idx = -1;
    d->audio_track_idx = -1;
    for (int i = 0; i < d->track_count; ++i)
    {
        if (d->video_track_idx < 0 &&
            d->tracks[i].track_type == EBML_TRACK_TYPE_VIDEO &&
            (d->tracks[i].codec == WEBM_CODEC_VP8 ||
             d->tracks[i].codec == WEBM_CODEC_VP9))
        {
            d->video_track_idx = i;
        }
        if (d->audio_track_idx < 0 &&
            d->tracks[i].track_type == EBML_TRACK_TYPE_AUDIO &&
            (d->tracks[i].codec == WEBM_CODEC_OPUS ||
             d->tracks[i].codec == WEBM_CODEC_VORBIS))
        {
            d->audio_track_idx = i;
        }
    }

    if (d->video_track_idx < 0)
    {
        set_error(d, "No VP8/VP9 video track found in WebM file");
        return 0;
    }

    return 1;
}

/* ---- Info 解析 ---- */

static void parse_info(WebmDemuxer* d, EbmlStream* sub)
{
    EbmlElement child;
    while (ebml_read_element(sub, &child))
    {
        switch (child.id)
        {
        case EBML_ID_TIMESTAMPSCALE:
            d->timestamp_scale = ebml_read_uint(sub, (size_t)child.size);
            break;
        case EBML_ID_DURATION:
            d->duration = ebml_read_float(sub, (size_t)child.size);
            break;
        default:
            ebml_skip_element(sub, &child);
            break;
        }
    }
}

/* ---- Segment 头部解析（Info + Tracks） ---- */

static int parse_segment_headers(WebmDemuxer* d)
{
    int found_tracks = 0;
    EbmlStream* seg = &d->segment_stream;

    /* 扫描 Segment 子元素，直到找到 Tracks 并遇到第一个 Cluster */
    while (ebml_stream_remaining(seg) > 0)
    {
        size_t save_pos = seg->pos;
        EbmlElement child;
        if (!ebml_read_element(seg, &child))
        {
            break;
        }

        switch (child.id)
        {
        case EBML_ID_INFO:
        {
            EbmlStream isub;
            ebml_sub_stream(seg, &child, &isub);
            parse_info(d, &isub);
            ebml_skip_element(seg, &child);
            break;
        }
        case EBML_ID_TRACKS:
        {
            EbmlStream tsub;
            ebml_sub_stream(seg, &child, &tsub);
            if (!parse_tracks(d, &tsub))
            {
                return 0;
            }
            found_tracks = 1;
            ebml_skip_element(seg, &child);
            break;
        }
        case EBML_ID_CLUSTER:
            /* 遇到第一个 Cluster，停止头部解析 */
            /* 回退到 Cluster 开始位置，留给 read_packet 处理 */
            seg->pos = save_pos;
            d->cluster_start = save_pos;
            goto headers_done;
        default:
            ebml_skip_element(seg, &child);
            break;
        }
    }

headers_done:
    if (!found_tracks)
    {
        set_error(d, "Tracks element not found before first Cluster");
        return 0;
    }
    return 1;
}

/* ================================================================
 * SimpleBlock / Block 解析
 *
 * SimpleBlock 格式：
 *   [TrackNumber VINT] [Timestamp int16 big-endian] [Flags uint8]
 *   [Lacing header（可选）] [帧数据...]
 *
 * Flags:
 *   bit 7 (0x80) = keyframe
 *   bit 2-1 (0x06) = lacing type (00=none, 01=Xiph, 10=fixed, 11=EBML)
 *   bit 0 (0x01) = discardable
 * ================================================================ */

typedef struct WebmBlockHeader
{
    uint64_t       track_number;
    int16_t        rel_ts;
    uint8_t        flags;
    int            lacing;
    const uint8_t* payload;
    size_t         payload_size;
} WebmBlockHeader;

static void clear_pending_packets(WebmDemuxer* d)
{
    d->pending_count = 0;
    d->pending_index = 0;
}

static int pop_pending_packet(WebmDemuxer* d, WebmPacket* pkt)
{
    if (d->pending_index < d->pending_count)
    {
        *pkt = d->pending_packets[d->pending_index++];
        if (d->pending_index >= d->pending_count)
        {
            clear_pending_packets(d);
        }
        return 1;
    }
    clear_pending_packets(d);
    return 0;
}

static int size_add_overflow(size_t a, size_t b, size_t* out)
{
    if (b > (size_t)-1 - a) return 1;
    *out = a + b;
    return 0;
}

static int parse_block_header(const uint8_t* bdata, size_t bsize,
                              WebmBlockHeader* out)
{
    EbmlStream bs;
    ebml_stream_init(&bs, bdata, bsize);

    int vint_bytes = 0;
    int64_t track_num = ebml_read_vint(&bs, &vint_bytes);
    if (track_num < 0 || ebml_stream_remaining(&bs) < 3)
    {
        return 0;
    }

    out->track_number = (uint64_t)track_num;
    out->rel_ts = (int16_t)((bs.data[bs.pos] << 8) | bs.data[bs.pos + 1]);
    bs.pos += 2;

    out->flags = bs.data[bs.pos++];
    out->lacing = (out->flags >> 1) & 0x03;
    out->payload = bs.data + bs.pos;
    out->payload_size = ebml_stream_remaining(&bs);
    return 1;
}

static int64_t read_ebml_lace_sint(EbmlStream* s, int* ok)
{
    int bytes = 0;
    int64_t raw = ebml_read_vint(s, &bytes);
    if (raw < 0 || bytes <= 0)
    {
        if (ok) *ok = 0;
        return 0;
    }

    int bits = bytes * 7;
    int64_t bias = ((int64_t)1 << (bits - 1)) - 1;
    if (ok) *ok = 1;
    return raw - bias;
}

static int assign_frame_pointers(EbmlStream* ls, const size_t* sizes,
                                 const uint8_t** frame_data,
                                 size_t* frame_size, int frame_count)
{
    const uint8_t* p = ls->data + ls->pos;
    const uint8_t* end = ls->data + ls->size;

    for (int i = 0; i < frame_count; ++i)
    {
        if (sizes[i] > (size_t)(end - p)) return 0;
        frame_data[i] = p;
        frame_size[i] = sizes[i];
        p += sizes[i];
    }
    return p == end;
}

static int parse_xiph_lacing(EbmlStream* ls, const uint8_t** frame_data,
                             size_t* frame_size, int frame_count)
{
    size_t sizes[WEBM_MAX_LACED_FRAMES];
    size_t total_known = 0;
    memset(sizes, 0, sizeof(sizes));

    for (int i = 0; i < frame_count - 1; ++i)
    {
        size_t sz = 0;
        for (;;)
        {
            if (ebml_stream_remaining(ls) < 1) return 0;
            uint8_t v = ls->data[ls->pos++];
            if (size_add_overflow(sz, v, &sz)) return 0;
            if (v != 255) break;
        }
        sizes[i] = sz;
        if (size_add_overflow(total_known, sz, &total_known)) return 0;
    }

    size_t remaining = ebml_stream_remaining(ls);
    if (total_known > remaining) return 0;
    sizes[frame_count - 1] = remaining - total_known;

    return assign_frame_pointers(ls, sizes, frame_data, frame_size, frame_count);
}

static int parse_fixed_lacing(EbmlStream* ls, const uint8_t** frame_data,
                              size_t* frame_size, int frame_count)
{
    size_t remaining = ebml_stream_remaining(ls);
    if (frame_count <= 0 || remaining % (size_t)frame_count != 0) return 0;

    size_t sz = remaining / (size_t)frame_count;
    for (int i = 0; i < frame_count; ++i)
    {
        frame_data[i] = ls->data + ls->pos + (size_t)i * sz;
        frame_size[i] = sz;
    }
    return 1;
}

static int parse_ebml_lacing(EbmlStream* ls, const uint8_t** frame_data,
                             size_t* frame_size, int frame_count)
{
    size_t sizes[WEBM_MAX_LACED_FRAMES];
    size_t total_known = 0;
    memset(sizes, 0, sizeof(sizes));

    int vint_bytes = 0;
    int64_t first_size = ebml_read_vint(ls, &vint_bytes);
    if (first_size < 0) return 0;

    sizes[0] = (size_t)first_size;
    total_known = sizes[0];

    int64_t prev = first_size;
    for (int i = 1; i < frame_count - 1; ++i)
    {
        int ok = 0;
        int64_t delta = read_ebml_lace_sint(ls, &ok);
        if (!ok) return 0;

        int64_t cur = prev + delta;
        if (cur < 0) return 0;

        sizes[i] = (size_t)cur;
        if (size_add_overflow(total_known, sizes[i], &total_known)) return 0;
        prev = cur;
    }

    size_t remaining = ebml_stream_remaining(ls);
    if (total_known > remaining) return 0;
    sizes[frame_count - 1] = remaining - total_known;

    return assign_frame_pointers(ls, sizes, frame_data, frame_size, frame_count);
}

static int parse_block_frames(const WebmBlockHeader* bh,
                              const uint8_t** frame_data,
                              size_t* frame_size, int* frame_count)
{
    if (bh->lacing == 0)
    {
        frame_data[0] = bh->payload;
        frame_size[0] = bh->payload_size;
        *frame_count = 1;
        return 1;
    }

    if (bh->payload_size < 1) return 0;

    EbmlStream ls;
    ebml_stream_init(&ls, bh->payload, bh->payload_size);
    int count = (int)ls.data[ls.pos++] + 1;
    if (count <= 1 || count > WEBM_MAX_LACED_FRAMES) return 0;

    int ok = 0;
    switch (bh->lacing)
    {
    case 1:
        ok = parse_xiph_lacing(&ls, frame_data, frame_size, count);
        break;
    case 2:
        ok = parse_fixed_lacing(&ls, frame_data, frame_size, count);
        break;
    case 3:
        ok = parse_ebml_lacing(&ls, frame_data, frame_size, count);
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) return 0;
    *frame_count = count;
    return 1;
}

static void fill_packet_from_frame(WebmDemuxer* d, WebmPacket* pkt,
                                   const WebmBlockHeader* bh,
                                   const uint8_t* frame_data,
                                   size_t frame_size,
                                   int frame_index,
                                   int frame_count,
                                   const uint8_t* alpha_data,
                                   size_t alpha_size,
                                   int64_t block_duration)
{
    int64_t base_ts = ((int64_t)bh->rel_ts + d->cluster_ts) * (int64_t)d->timestamp_scale;
    int64_t duration = block_duration;

    if (frame_count > 1 && block_duration > 0)
    {
        int64_t per_frame = block_duration / frame_count;
        base_ts += per_frame * frame_index;
        duration = (frame_index == frame_count - 1)
            ? (block_duration - per_frame * (frame_count - 1))
            : per_frame;
    }

    pkt->track_number = bh->track_number;
    pkt->data = frame_data;
    pkt->data_size = frame_size;
    pkt->timestamp_ns = base_ts;
    pkt->is_key = (bh->flags & 0x80) ? 1 : 0;
    pkt->alpha_data = (frame_count == 1) ? alpha_data : NULL;
    pkt->alpha_size = (frame_count == 1) ? alpha_size : 0;
    pkt->duration_ns = duration;
}

static int queue_block_packets(WebmDemuxer* d, const uint8_t* bdata, size_t bsize,
                               int64_t cluster_ts,
                               const uint8_t* alpha_data, size_t alpha_size,
                               int64_t block_duration)
{
    WebmBlockHeader bh;
    const uint8_t* frame_data[WEBM_MAX_LACED_FRAMES];
    size_t frame_size[WEBM_MAX_LACED_FRAMES];
    int frame_count = 0;

    clear_pending_packets(d);

    if (!parse_block_header(bdata, bsize, &bh)) return 0;
    if (!parse_block_frames(&bh, frame_data, frame_size, &frame_count)) return 0;

    for (int i = 0; i < frame_count; ++i)
    {
        memset(&d->pending_packets[i], 0, sizeof(d->pending_packets[i]));
        fill_packet_from_frame(d, &d->pending_packets[i], &bh,
                               frame_data[i], frame_size[i], i, frame_count,
                               alpha_data, alpha_size, block_duration);
    }

    d->pending_count = frame_count;
    d->pending_index = 0;
    (void)cluster_ts; /* cluster_ts is read from d->cluster_ts for consistency */
    return frame_count > 0;
}

/* ---- BlockGroup 解析（包含 BlockAdditions） ---- */

static int queue_block_group_packets(WebmDemuxer* d, EbmlStream* bgsub,
                                     int64_t cluster_ts)
{
    const uint8_t* block_data = NULL;
    size_t block_size = 0;
    const uint8_t* alpha_data = NULL;
    size_t alpha_size = 0;
    int64_t block_duration = 0;

    EbmlElement child;
    while (ebml_read_element(bgsub, &child))
    {
        switch (child.id)
        {
        case EBML_ID_BLOCK:
            block_data = ebml_read_binary(bgsub, (size_t)child.size);
            block_size = (size_t)child.size;
            break;

        case EBML_ID_BLOCKDURATION:
            block_duration = (int64_t)ebml_read_uint(bgsub, (size_t)child.size)
                             * (int64_t)d->timestamp_scale;
            break;

        case EBML_ID_BLOCKADDITIONS:
        {
            /* 解析 BlockAdditions → BlockMore → BlockAdditional */
            EbmlStream add_sub;
            ebml_sub_stream(bgsub, &child, &add_sub);

            EbmlElement more;
            while (ebml_read_element(&add_sub, &more))
            {
                if (more.id == EBML_ID_BLOCKMORE)
                {
                    EbmlStream more_sub;
                    ebml_sub_stream(&add_sub, &more, &more_sub);

                    uint64_t add_id = 1; /* 默认 */
                    EbmlElement mc;
                    while (ebml_read_element(&more_sub, &mc))
                    {
                        if (mc.id == EBML_ID_BLOCKADDID)
                        {
                            add_id = ebml_read_uint(&more_sub, (size_t)mc.size);
                        }
                        else if (mc.id == EBML_ID_BLOCKADDITIONAL)
                        {
                            if (add_id == 1) /* Alpha 数据的 BlockAddID = 1 */
                            {
                                alpha_data = ebml_read_binary(&more_sub,
                                                              (size_t)mc.size);
                                alpha_size = (size_t)mc.size;
                            }
                            else
                            {
                                ebml_skip_element(&more_sub, &mc);
                            }
                        }
                        else
                        {
                            ebml_skip_element(&more_sub, &mc);
                        }
                    }
                    ebml_skip_element(&add_sub, &more);
                }
                else
                {
                    ebml_skip_element(&add_sub, &more);
                }
            }
            ebml_skip_element(bgsub, &child);
            break;
        }

        default:
            ebml_skip_element(bgsub, &child);
            break;
        }
    }

    if (!block_data || block_size == 0)
    {
        return 0;
    }

    return queue_block_packets(d, block_data, block_size, cluster_ts,
                               alpha_data, alpha_size, block_duration);
}

/* ================================================================
 * 公开 API 实现
 * ================================================================ */

int webm_demuxer_init(WebmDemuxer* demuxer, const uint8_t* data, size_t size)
{
    memset(demuxer, 0, sizeof(*demuxer));
    demuxer->timestamp_scale = 1000000; /* 默认值：1ms */
    demuxer->video_track_idx = -1;
    demuxer->audio_track_idx = -1;

    ebml_stream_init(&demuxer->stream, data, size);

    /* 1. 解析 EBML Header */
    if (!parse_ebml_header(demuxer, &demuxer->stream))
    {
        return 0;
    }

    /* 2. 打开 Segment */
    EbmlElement seg_elem;
    if (!ebml_read_element(&demuxer->stream, &seg_elem) ||
        seg_elem.id != EBML_ID_SEGMENT)
    {
        set_error(demuxer, "Segment element not found");
        return 0;
    }

    ebml_sub_stream(&demuxer->stream, &seg_elem, &demuxer->segment_stream);

    /* 3. 解析 Segment 头部（Info + Tracks） */
    if (!parse_segment_headers(demuxer))
    {
        return 0;
    }

    demuxer->in_cluster = 0;
    demuxer->initialized = 1;
    return 1;
}

int webm_demuxer_read_packet(WebmDemuxer* demuxer, WebmPacket* pkt)
{
    if (!demuxer->initialized || demuxer->video_track_idx < 0)
    {
        return 0;
    }

    const WebmTrackInfo* vtrack = &demuxer->tracks[demuxer->video_track_idx];
    EbmlStream* seg = &demuxer->segment_stream;

    for (;;)
    {
        while (pop_pending_packet(demuxer, pkt))
        {
            if (pkt->track_number == vtrack->track_number)
            {
                return 1;
            }
        }

        /* 如果正在 Cluster 内部，继续读取 Block */
        if (demuxer->in_cluster)
        {
            EbmlStream* cs = &demuxer->cluster_stream;
            while (ebml_stream_remaining(cs) > 0)
            {
                EbmlElement child;
                if (!ebml_read_element(cs, &child))
                {
                    break;
                }

                if (child.id == EBML_ID_TIMESTAMP)
                {
                    demuxer->cluster_ts = (int64_t)ebml_read_uint(cs,
                                                        (size_t)child.size);
                    continue;
                }

                if (child.id == EBML_ID_SIMPLEBLOCK)
                {
                    const uint8_t* bdata = ebml_read_binary(cs,
                                                            (size_t)child.size);
                    if (!bdata) continue;

                    if (queue_block_packets(demuxer, bdata, (size_t)child.size,
                                            demuxer->cluster_ts, NULL, 0, 0))
                    {
                        while (pop_pending_packet(demuxer, pkt))
                        {
                            if (pkt->track_number == vtrack->track_number)
                            {
                                return 1; /* 成功返回一个视频包 */
                            }
                        }
                    }
                    continue;
                }

                if (child.id == EBML_ID_BLOCKGROUP)
                {
                    EbmlStream bgsub;
                    ebml_sub_stream(cs, &child, &bgsub);

                    if (queue_block_group_packets(demuxer, &bgsub,
                                                  demuxer->cluster_ts))
                    {
                        ebml_skip_element(cs, &child);
                        while (pop_pending_packet(demuxer, pkt))
                        {
                            if (pkt->track_number == vtrack->track_number)
                            {
                                return 1;
                            }
                        }
                    }
                    else
                    {
                        ebml_skip_element(cs, &child);
                    }
                    continue;
                }

                /* 跳过其他元素 */
                ebml_skip_element(cs, &child);
            }

            /* 当前 Cluster 读完 */
            demuxer->in_cluster = 0;
        }

        /* 查找下一个 Cluster */
        if (ebml_stream_remaining(seg) < 4)
        {
            return 0; /* EOF */
        }

        EbmlElement seg_child;
        if (!ebml_read_element(seg, &seg_child))
        {
            return 0;
        }

        if (seg_child.id == EBML_ID_CLUSTER)
        {
            ebml_sub_stream(seg, &seg_child, &demuxer->cluster_stream);
            demuxer->in_cluster = 1;
            demuxer->cluster_ts = 0;

            /* 记录 Cluster 后的位置以便跳过 */
            if (seg_child.size >= 0)
            {
                seg->pos = seg_child.data_offset + (size_t)seg_child.size;
            }
            /* 不确定长度的 Cluster 会在下次读到新 Cluster ID 时自然终止 */
        }
        else
        {
            /* 跳过非 Cluster 元素（Cues 等） */
            ebml_skip_element(seg, &seg_child);
        }
    }
}

int webm_demuxer_rewind(WebmDemuxer* demuxer)
{
    if (!demuxer->initialized)
    {
        return 0;
    }

    /* 回退 Segment 流到第一个 Cluster */
    demuxer->segment_stream.pos = demuxer->cluster_start;
    demuxer->in_cluster = 0;
    demuxer->cluster_ts = 0;
    clear_pending_packets(demuxer);
    return 1;
}

const WebmTrackInfo* webm_demuxer_get_video_track(const WebmDemuxer* demuxer)
{
    if (demuxer->video_track_idx < 0)
    {
        return NULL;
    }
    return &demuxer->tracks[demuxer->video_track_idx];
}

const WebmTrackInfo* webm_demuxer_get_audio_track(const WebmDemuxer* demuxer)
{
    if (demuxer->audio_track_idx < 0)
    {
        return NULL;
    }
    return &demuxer->tracks[demuxer->audio_track_idx];
}

int webm_demuxer_read_next_packet(WebmDemuxer* demuxer, WebmPacket* pkt)
{
    if (!demuxer->initialized) return 0;
    EbmlStream* seg = &demuxer->segment_stream;

    for (;;)
    {
        if (pop_pending_packet(demuxer, pkt))
        {
            return 1;
        }

        if (demuxer->in_cluster)
        {
            EbmlStream* cs = &demuxer->cluster_stream;
            while (ebml_stream_remaining(cs) > 0)
            {
                EbmlElement child;
                if (!ebml_read_element(cs, &child)) break;

                if (child.id == EBML_ID_TIMESTAMP)
                {
                    demuxer->cluster_ts = (int64_t)ebml_read_uint(cs, (size_t)child.size);
                    continue;
                }

                if (child.id == EBML_ID_SIMPLEBLOCK)
                {
                    const uint8_t* bdata = ebml_read_binary(cs, (size_t)child.size);
                    if (!bdata) continue;
                    if (queue_block_packets(demuxer, bdata, (size_t)child.size,
                                            demuxer->cluster_ts, NULL, 0, 0) &&
                        pop_pending_packet(demuxer, pkt))
                    {
                        return 1; /* 返回任意轨道的包 */
                    }
                    continue;
                }

                if (child.id == EBML_ID_BLOCKGROUP)
                {
                    EbmlStream bgsub;
                    ebml_sub_stream(cs, &child, &bgsub);
                    if (queue_block_group_packets(demuxer, &bgsub, demuxer->cluster_ts))
                    {
                        ebml_skip_element(cs, &child);
                        if (pop_pending_packet(demuxer, pkt))
                        {
                            return 1;
                        }
                    }
                    else
                    {
                        ebml_skip_element(cs, &child);
                    }
                    continue;
                }

                ebml_skip_element(cs, &child);
            }
            demuxer->in_cluster = 0;
        }

        if (ebml_stream_remaining(seg) < 4) return 0;

        EbmlElement seg_child;
        if (!ebml_read_element(seg, &seg_child)) return 0;

        if (seg_child.id == EBML_ID_CLUSTER)
        {
            ebml_sub_stream(seg, &seg_child, &demuxer->cluster_stream);
            demuxer->in_cluster = 1;
            demuxer->cluster_ts = 0;
            if (seg_child.size >= 0)
            {
                seg->pos = seg_child.data_offset + (size_t)seg_child.size;
            }
        }
        else
        {
            ebml_skip_element(seg, &seg_child);
        }
    }
}
