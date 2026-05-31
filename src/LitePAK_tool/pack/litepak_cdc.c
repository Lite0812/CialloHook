/*
 * LitePAK CDC (Content-Defined Chunking) 分块
 * FastCDC-like normalized cut 实现
 */
#include "litepak.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CDC 参数
 * ============================================================================ */

cdc_params_t litepak_make_cdc_params(uint32_t avg_size) {
    cdc_params_t p;
    if (avg_size < 32 * 1024) avg_size = 32 * 1024;
    uint32_t min_size = avg_size / 4;
    if (min_size < 8 * 1024) min_size = 8 * 1024;
    uint32_t max_size = avg_size * 4;
    if (max_size <= avg_size) max_size = avg_size + 1;

    int avg_bits = 0;
    uint32_t tmp = avg_size;
    while (tmp > 1) { avg_bits++; tmp >>= 1; }

    p.min_size = min_size;
    p.avg_size = avg_size;
    p.max_size = max_size;
    p.mask_early = ((uint64_t)1 << (avg_bits + 1)) - 1;
    p.mask_late = ((uint64_t)1 << (avg_bits > 1 ? avg_bits - 1 : 1)) - 1;
    return p;
}

/* ============================================================================
 * Chunk List 管理
 * ============================================================================ */

void chunk_list_init(chunk_list_t* cl) {
    cl->chunks = NULL;
    cl->chunk_sizes = NULL;
    cl->chunk_count = 0;
    cl->chunk_cap = 0;
}

void chunk_list_free(chunk_list_t* cl) {
    for (int i = 0; i < cl->chunk_count; i++) {
        if (cl->chunks[i]) free(cl->chunks[i]);
    }
    if (cl->chunks) free(cl->chunks);
    if (cl->chunk_sizes) free(cl->chunk_sizes);
    cl->chunks = NULL;
    cl->chunk_sizes = NULL;
    cl->chunk_count = 0;
    cl->chunk_cap = 0;
}

void chunk_list_add(chunk_list_t* cl, const uint8_t* data, size_t len) {
    if (cl->chunk_count >= cl->chunk_cap) {
        int new_cap = cl->chunk_cap ? cl->chunk_cap * 2 : 16;
        cl->chunks = (uint8_t**)realloc(cl->chunks, sizeof(uint8_t*) * new_cap);
        cl->chunk_sizes = (size_t*)realloc(cl->chunk_sizes, sizeof(size_t) * new_cap);
        cl->chunk_cap = new_cap;
    }
    uint8_t* copy = (uint8_t*)malloc(len);
    memcpy(copy, data, len);
    cl->chunks[cl->chunk_count] = copy;
    cl->chunk_sizes[cl->chunk_count] = len;
    cl->chunk_count++;
}

/* ============================================================================
 * CDC 分块
 * ============================================================================ */

void litepak_split_chunks_cdc(const uint8_t* data, size_t len,
                               const cdc_params_t* params, chunk_list_t* out) {
    const uint64_t* gear = litepak_get_gear_table();

    if (len == 0) {
        chunk_list_add(out, (const uint8_t*)"", 0);
        return;
    }
    if (len <= params->min_size) {
        chunk_list_add(out, data, len);
        return;
    }

    size_t pos = 0;
    while (pos < len) {
        size_t max_end = pos + params->max_size;
        if (max_end > len) max_end = len;
        size_t min_end = pos + params->min_size;
        if (min_end > len) min_end = len;

        if (max_end <= min_end) {
            chunk_list_add(out, data + pos, max_end - pos);
            break;
        }

        uint64_t h = 0;
        size_t cut = 0;
        int found = 0;
        for (size_t i = min_end; i < max_end; i++) {
            h = ((h << 1) + gear[data[i]]) & 0xFFFFFFFFFFFFFFFFULL;
            size_t rel = i - pos;
            uint64_t mask = (rel < params->avg_size) ? params->mask_early : params->mask_late;
            if ((h & mask) == 0) {
                cut = i + 1;
                found = 1;
                break;
            }
        }

        if (!found) cut = max_end;
        chunk_list_add(out, data + pos, cut - pos);
        pos = cut;
    }
}

/* ============================================================================
 * 流式 CDC 分块器
 * ============================================================================ */

typedef struct {
    cdc_params_t params;
    uint8_t*     buf;
    size_t       buf_len;
    size_t       buf_cap;
    chunk_list_t ready;
} streaming_cdc_t;

void streaming_cdc_init(streaming_cdc_t* s, const cdc_params_t* params) {
    s->params = *params;
    s->buf = NULL;
    s->buf_len = 0;
    s->buf_cap = 0;
    chunk_list_init(&s->ready);
}

void streaming_cdc_feed(streaming_cdc_t* s, const uint8_t* data, size_t len) {
    size_t new_len = s->buf_len + len;
    if (new_len > s->buf_cap) {
        size_t new_cap = s->buf_cap ? s->buf_cap : 256;
        while (new_cap < new_len) new_cap *= 2;
        s->buf = (uint8_t*)realloc(s->buf, new_cap);
        s->buf_cap = new_cap;
    }
    memcpy(s->buf + s->buf_len, data, len);
    s->buf_len = new_len;

    const uint64_t* gear = litepak_get_gear_table();

    while (s->buf_len > s->params.max_size) {
        uint64_t h = 0;
        size_t cut = 0;
        int found = 0;
        size_t limit = s->params.max_size;
        if (limit > s->buf_len) limit = s->buf_len;

        for (size_t i = s->params.min_size; i < limit; i++) {
            h = ((h << 1) + gear[s->buf[i]]) & 0xFFFFFFFFFFFFFFFFULL;
            uint64_t mask = (i < s->params.avg_size) ? s->params.mask_early : s->params.mask_late;
            if ((h & mask) == 0) {
                cut = i + 1;
                found = 1;
                break;
            }
        }
        if (!found) cut = s->params.max_size;

        chunk_list_add(&s->ready, s->buf, cut);
        memmove(s->buf, s->buf + cut, s->buf_len - cut);
        s->buf_len -= cut;
    }
}

void streaming_cdc_finalize(streaming_cdc_t* s, chunk_list_t* out) {
    if (s->buf_len > 0) {
        if (s->buf_len <= s->params.max_size) {
            chunk_list_add(&s->ready, s->buf, s->buf_len);
        } else {
            litepak_split_chunks_cdc(s->buf, s->buf_len, &s->params, &s->ready);
        }
        s->buf_len = 0;
    }

    /* 转移所有 ready chunks 到 out */
    for (int i = 0; i < s->ready.chunk_count; i++) {
        chunk_list_add(out, s->ready.chunks[i], s->ready.chunk_sizes[i]);
    }
    chunk_list_free(&s->ready);
    if (s->buf) { free(s->buf); s->buf = NULL; }
    s->buf_cap = 0;
}
