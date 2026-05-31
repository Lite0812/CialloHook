/*
 * LitePAK 压缩/解压缩封装
 * 复用项目 third/ 下的 miniz (zlib), zstd (仅解压), lzma
 * 对于 pack 操作中的 zstd 压缩，使用内嵌的简单实现或跳过
 */
#include "litepak.h"
#include <string.h>
#include <stdlib.h>

/* 引用项目已有的第三方库 */
#include "../../third/miniz/miniz.h"
#include "../../third/zstd/zstd.h"
#include "../../third/lzma/LzmaDec.h"

/* LZMA 编码器（用于 pack）- 需要额外引入 */
/* 注意：项目 third/lzma 只有 LzmaDec，pack 时需要 LzmaEnc */
/* 如果没有 LzmaEnc，pack 时 lzma 压缩将不可用，但解包可以 */
#ifdef LITEPAK_HAS_LZMA_ENC
#include "../../third/lzma/LzmaEnc.h"
#endif

/* ============================================================================
 * zlib 压缩/解压缩 (通过 miniz)
 * ============================================================================ */

static int compress_zlib(const uint8_t* data, size_t len, buffer_t* out) {
    mz_ulong bound = mz_compressBound((mz_ulong)len);
    buffer_reserve(out, out->len + bound);
    mz_ulong dest_len = bound;
    int ret = mz_compress2(out->data + out->len, &dest_len, data, (mz_ulong)len, MZ_BEST_COMPRESSION);
    if (ret != MZ_OK) return -1;
    out->len += (size_t)dest_len;
    return 0;
}

static int decompress_zlib(const uint8_t* data, size_t len, buffer_t* out, size_t expected_max) {
    size_t alloc_sz = expected_max > 0 ? expected_max : len * 4;
    if (alloc_sz < 256) alloc_sz = 256;

    for (int attempt = 0; attempt < 5; attempt++) {
        buffer_reserve(out, alloc_sz);
        mz_ulong dest_len = (mz_ulong)alloc_sz;
        int ret = mz_uncompress(out->data, &dest_len, data, (mz_ulong)len);
        if (ret == MZ_OK) {
            out->len = (size_t)dest_len;
            return 0;
        }
        if (ret == MZ_BUF_ERROR) {
            alloc_sz *= 2;
            continue;
        }
        return -1;
    }
    return -1;
}

/* ============================================================================
 * zstd 解压缩
 * ============================================================================ */

static int decompress_zstd(const uint8_t* data, size_t len, buffer_t* out, size_t expected_size) {
    unsigned long long frame_size = ZSTD_getFrameContentSize(data, len);
    size_t alloc_sz;
    if (expected_size > 0)
        alloc_sz = expected_size;
    else if (frame_size == ZSTD_CONTENTSIZE_UNKNOWN || frame_size == ZSTD_CONTENTSIZE_ERROR)
        alloc_sz = len * 4;
    else
        alloc_sz = (size_t)frame_size;

    if (buffer_reserve(out, alloc_sz) != 0) return -1;
    size_t ret = ZSTD_decompress(out->data, alloc_sz, data, len);
    if (ZSTD_isError(ret)) return -1;
    out->len = ret;
    return 0;
}

/* zstd 压缩 - 项目的 third/zstd 只有解压库 (zstddeclib.c)
 * 对于 pack 操作，如果没有完整 zstd 库，则回退到 zlib */
static int compress_zstd(const uint8_t* data, size_t len, int level, buffer_t* out) {
#ifdef LITEPAK_HAS_ZSTD_ENC
    /* 如果有完整 zstd 库（包含压缩） */
    size_t bound = ZSTD_compressBound(len);
    buffer_reserve(out, out->len + bound);
    size_t ret = ZSTD_compress(out->data + out->len, bound, data, len, level);
    if (ZSTD_isError(ret)) return -1;
    out->len += ret;
    return 0;
#else
    (void)data; (void)len; (void)level; (void)out;
    return -1;
#endif
}

/* ============================================================================
 * LZMA 解压缩
 * ============================================================================ */

static void* lzma_alloc_func(ISzAllocPtr p, size_t size) {
    (void)p;
    return malloc(size);
}

static void lzma_free_func(ISzAllocPtr p, void* address) {
    (void)p;
    free(address);
}

static const ISzAlloc lzma_allocator = { lzma_alloc_func, lzma_free_func };

static int decompress_lzma_raw(const uint8_t* props, size_t props_size,
                                const uint8_t* data, size_t data_len,
                                uint8_t* out_buf, size_t* out_len) {
    ELzmaStatus status;
    SizeT dest_len = (SizeT)*out_len;
    SizeT src_len = (SizeT)data_len;
    SRes res = LzmaDecode(out_buf, &dest_len, data, &src_len,
                          props, (unsigned)props_size,
                          LZMA_FINISH_ANY, &status, &lzma_allocator);
    *out_len = (size_t)dest_len;
    if (res != SZ_OK)
        return -1;
    if (status != LZMA_STATUS_FINISHED_WITH_MARK &&
        status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK &&
        status != LZMA_STATUS_NOT_FINISHED)
        return -1;
    return 0;
}

/* LZMA 压缩 - 需要 LzmaEnc */
static int compress_lzma(const uint8_t* data, size_t len, buffer_t* out) {
#ifdef LITEPAK_HAS_LZMA_ENC
    /* 使用 LzmaEnc 进行压缩 */
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.lc = 4;
    props.lp = 0;
    props.pb = 4;
    props.dictSize = 1 << 27;
    props.level = 9;

    size_t props_size = LZMA_PROPS_SIZE;
    size_t dest_len = len + len / 3 + 128;
    uint8_t* dest = (uint8_t*)malloc(dest_len);
    uint8_t props_buf[LZMA_PROPS_SIZE];

    SRes res = LzmaEncode(dest, &dest_len, data, len,
                          &props, props_buf, &props_size,
                          1, NULL, &lzma_allocator, &lzma_allocator);
    if (res != SZ_OK) {
        free(dest);
        return -1;
    }

    /* 输出: props(5 bytes) + compressed data */
    buffer_append(out, props_buf, props_size);
    buffer_append(out, dest, dest_len);
    free(dest);
    return 0;
#else
    (void)data; (void)len; (void)out;
    return -1;
#endif
}

/* ============================================================================
 * 统一压缩/解压缩接口
 * ============================================================================ */

int litepak_compress_chunk(const uint8_t* data, size_t len, const char* method,
                           bool whole_file_mode, buffer_t* out) {
    buffer_init(out);

    if (strcmp(method, "raw") == 0) {
        buffer_append_u8(out, MODE_RAW);
        buffer_append(out, data, len);
        return 0;
    }

    if (strcmp(method, "zlib") == 0) {
        buffer_t compressed;
        buffer_init(&compressed);
        if (compress_zlib(data, len, &compressed) == 0 && compressed.len + 1 < len) {
            buffer_append_u8(out, MODE_ZLIB);
            buffer_append(out, compressed.data, compressed.len);
            buffer_free(&compressed);
            return 0;
        }
        buffer_free(&compressed);
        buffer_append_u8(out, MODE_RAW);
        buffer_append(out, data, len);
        return 0;
    }

    if (strcmp(method, "zstd") == 0) {
        buffer_t compressed;
        buffer_init(&compressed);
        int level = whole_file_mode ? 18 : 12;
        if (compress_zstd(data, len, level, &compressed) == 0 && compressed.len + 1 < len) {
            buffer_append_u8(out, MODE_ZSTD);
            buffer_append(out, compressed.data, compressed.len);
            buffer_free(&compressed);
            return 0;
        }
        buffer_free(&compressed);
        buffer_append_u8(out, MODE_RAW);
        buffer_append(out, data, len);
        return 0;
    }

    if (strcmp(method, "lzma") == 0) {
        buffer_t compressed;
        buffer_init(&compressed);
        if (compress_lzma(data, len, &compressed) == 0 && compressed.len + 1 < len) {
            buffer_append_u8(out, MODE_LZMA);
            buffer_append(out, compressed.data, compressed.len);
            buffer_free(&compressed);
            return 0;
        }
        buffer_free(&compressed);
        buffer_append_u8(out, MODE_RAW);
        buffer_append(out, data, len);
        return 0;
    }

    if (strcmp(method, "auto") == 0) {
        /* 尝试多种压缩方式，选最小的 */
        buffer_t best;
        buffer_init(&best);
        buffer_append_u8(&best, MODE_RAW);
        buffer_append(&best, data, len);

        const char* candidates[3];
        int n_candidates = 0;

        if (whole_file_mode) {
            candidates[n_candidates++] = "zstd";
            candidates[n_candidates++] = "zlib";
            candidates[n_candidates++] = "lzma";
        } else if (len <= 8 * 1024) {
            candidates[n_candidates++] = "zlib";
        } else if (len <= 64 * 1024) {
            candidates[n_candidates++] = "zstd";
            candidates[n_candidates++] = "zlib";
        } else {
            candidates[n_candidates++] = "zstd";
            candidates[n_candidates++] = "lzma";
        }

        for (int i = 0; i < n_candidates; i++) {
            buffer_t candidate;
            buffer_init(&candidate);
            litepak_compress_chunk(data, len, candidates[i], whole_file_mode, &candidate);
            if (candidate.len < best.len) {
                buffer_free(&best);
                best = candidate;
            } else {
                buffer_free(&candidate);
            }
        }

        *out = best;
        return 0;
    }

    /* 未知方法，使用 raw */
    buffer_append_u8(out, MODE_RAW);
    buffer_append(out, data, len);
    return 0;
}

int litepak_decompress_chunk(const uint8_t* data, size_t len, size_t expected_size, buffer_t* out) {
    buffer_init(out);
    if (len == 0) return -1;

    uint8_t mode = data[0];
    const uint8_t* payload = data + 1;
    size_t payload_len = len - 1;

    switch (mode) {
    case MODE_RAW:
        if (buffer_append(out, payload, payload_len) != 0) return -1;
        return 0;

    case MODE_ZLIB:
        return decompress_zlib(payload, payload_len, out, expected_size);

    case MODE_ZSTD:
        return decompress_zstd(payload, payload_len, out, expected_size);

    case MODE_LZMA: {
        if (payload_len < 5) return -1;
        uint8_t prop0 = payload[0];
        uint32_t dict_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                             ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
        if (prop0 >= 9 * 5 * 5) return -1;

        /* 构建 5 字节 LZMA props */
        uint8_t props_buf[5];
        props_buf[0] = prop0;
        memcpy(props_buf + 1, payload + 1, 4);

        const uint8_t* lzma_data = payload + 5;
        size_t lzma_data_len = payload_len - 5;

        size_t out_len = expected_size > 0 ? expected_size : (dict_size > 0 ? dict_size : 1024 * 1024);
        if (out_len < lzma_data_len * 4) out_len = lzma_data_len * 4;

        for (int attempt = 0; attempt < 5; attempt++) {
            if (buffer_reserve(out, out_len) != 0) return -1;
            size_t actual_out = out_len;
            if (decompress_lzma_raw(props_buf, 5, lzma_data, lzma_data_len,
                                     out->data, &actual_out) == 0) {
                out->len = actual_out;
                return 0;
            }
            if (expected_size > 0) break;
            out_len *= 2;
        }
        return -1;
    }

    default:
        return -1;
    }
}

const char* litepak_mode_name(uint8_t mode) {
    switch (mode) {
    case MODE_RAW:  return "raw";
    case MODE_ZLIB: return "zlib";
    case MODE_ZSTD: return "zstd";
    case MODE_LZMA: return "lzma";
    default:        return "unknown";
    }
}
