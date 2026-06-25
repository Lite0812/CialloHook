#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "../../../third/miniz/miniz.h"
#include "../../../third/lzma/LzmaDec.h"
#include "../../../third/lzma/LzmaEnc.h"
#include "../../../third/zstd/zstd.h"

#define CIALLO_MAGIC "CialloPAK"
#define CIALLO_MAGIC_SIZE 9
#define CIALLO_VERSION 4
#define CIALLO_HASH_SIZE 16
#define CIALLO_NONCE_SIZE 12
#define CIALLO_MASTER_KEY "CialloPakCustomKey::v4"
#define CIALLO_MASTER_KEY_SIZE 22
#define CIALLO_FLAG_DEDUP_REUSED 0x02
#define CIALLO_MODE_RAW 0
#define CIALLO_MODE_ZLIB 1
#define CIALLO_MODE_ZSTD 2
#define CIALLO_MODE_LZMA 3
#define CIALLO_HEADER_SIZE (CIALLO_MAGIC_SIZE + 2 + 4 + 8 + 8 + CIALLO_NONCE_SIZE)
#define CIALLO_INDEX_ENTRY_SIZE (CIALLO_HASH_SIZE + 1 + 8 + 8 + 8 + CIALLO_NONCE_SIZE)
#define CIALLO_LZMA_LC 4
#define CIALLO_LZMA_LP 0
#define CIALLO_LZMA_PB 4
#define CIALLO_LZMA_DICT_SIZE (1u << 27)
#define BLAKE2B_BLOCK_SIZE 128

typedef struct ByteVec {
    uint8_t* data;
    size_t size;
    size_t capacity;
} ByteVec;

typedef struct FileItem {
    wchar_t* full_path;
    char* rel;
    uint64_t size;
} FileItem;

typedef struct FileList {
    FileItem* items;
    size_t count;
    size_t capacity;
} FileList;

typedef struct PakEntry {
    uint8_t hash[CIALLO_HASH_SIZE];
    uint8_t flags;
    uint64_t orig_size;
    uint64_t stored_size;
    uint64_t offset;
    uint8_t nonce[CIALLO_NONCE_SIZE];
} PakEntry;

typedef struct EntryList {
    PakEntry* items;
    size_t count;
    size_t capacity;
} EntryList;

typedef struct DedupRecord {
    uint8_t content_id[32];
    uint64_t offset;
    uint64_t stored_size;
    uint64_t orig_size;
    uint8_t nonce[CIALLO_NONCE_SIZE];
    uint8_t mode;
} DedupRecord;

typedef struct DedupList {
    DedupRecord* items;
    size_t count;
    size_t capacity;
} DedupList;

typedef struct ManifestEntry {
    char hash_hex[33];
    char* rel;
} ManifestEntry;

typedef struct ManifestList {
    ManifestEntry* items;
    size_t count;
    size_t capacity;
} ManifestList;

typedef struct Blake2bCtx {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[BLAKE2B_BLOCK_SIZE];
    size_t buflen;
    size_t outlen;
} Blake2bCtx;

static const uint64_t blake2b_iv[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

static const uint8_t blake2b_sigma[12][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 },
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8 },
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13 },
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9 },
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11 },
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10 },
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5 },
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 }
};

static void set_console_utf8(void) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static void* xmalloc(size_t size) {
    void* p = malloc(size ? size : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

static void* xrealloc(void* ptr, size_t size) {
    void* p = realloc(ptr, size ? size : 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return p;
}

static char* xstrdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* out = (char*)xmalloc(n);
    memcpy(out, s, n);
    return out;
}

static wchar_t* xwcsdup(const wchar_t* s) {
    size_t n = wcslen(s) + 1;
    wchar_t* out = (wchar_t*)xmalloc(n * sizeof(wchar_t));
    memcpy(out, s, n * sizeof(wchar_t));
    return out;
}

static void vec_free(ByteVec* v) {
    free(v->data);
    v->data = NULL;
    v->size = 0;
    v->capacity = 0;
}

static void vec_reserve(ByteVec* v, size_t capacity) {
    if (capacity > v->capacity) {
        v->data = (uint8_t*)xrealloc(v->data, capacity);
        v->capacity = capacity;
    }
}

static void vec_resize(ByteVec* v, size_t size) {
    vec_reserve(v, size);
    v->size = size;
}

static void vec_append(ByteVec* v, const void* data, size_t size) {
    if (size == 0) {
        return;
    }
    if (v->size > SIZE_MAX - size) {
        fprintf(stderr, "buffer too large\n");
        exit(1);
    }
    vec_reserve(v, v->size + size);
    memcpy(v->data + v->size, data, size);
    v->size += size;
}

static void vec_append_u8(ByteVec* v, uint8_t value) {
    vec_append(v, &value, 1);
}

static void free_file_list(FileList* list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].full_path);
        free(list->items[i].rel);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void file_list_add(FileList* list, const wchar_t* full_path, const char* rel, uint64_t size) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->items = (FileItem*)xrealloc(list->items, list->capacity * sizeof(FileItem));
    }
    list->items[list->count].full_path = xwcsdup(full_path);
    list->items[list->count].rel = xstrdup(rel);
    list->items[list->count].size = size;
    list->count += 1;
}

static void entry_list_add(EntryList* list, const PakEntry* entry) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->items = (PakEntry*)xrealloc(list->items, list->capacity * sizeof(PakEntry));
    }
    list->items[list->count++] = *entry;
}

static void dedup_list_add(DedupList* list, const DedupRecord* record) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->items = (DedupRecord*)xrealloc(list->items, list->capacity * sizeof(DedupRecord));
    }
    list->items[list->count++] = *record;
}

static void manifest_list_free(ManifestList* list) {
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i].rel);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void manifest_list_add(ManifestList* list, const char* hash_hex, const char* rel) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 64;
        list->items = (ManifestEntry*)xrealloc(list->items, list->capacity * sizeof(ManifestEntry));
    }
    memset(list->items[list->count].hash_hex, 0, sizeof(list->items[list->count].hash_hex));
    strncpy(list->items[list->count].hash_hex, hash_hex, 32);
    list->items[list->count].rel = xstrdup(rel);
    list->count += 1;
}

static int cmp_file_item(const void* a, const void* b) {
    const FileItem* fa = (const FileItem*)a;
    const FileItem* fb = (const FileItem*)b;
    return strcmp(fa->rel, fb->rel);
}

static int cmp_entry_hash(const void* a, const void* b) {
    const PakEntry* ea = (const PakEntry*)a;
    const PakEntry* eb = (const PakEntry*)b;
    return memcmp(ea->hash, eb->hash, CIALLO_HASH_SIZE);
}

static wchar_t* utf8_to_wide(const char* text) {
    if (!text) {
        return xwcsdup(L"");
    }
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (count <= 0) {
        count = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
        if (count <= 0) {
            return xwcsdup(L"");
        }
        wchar_t* out = (wchar_t*)xmalloc((size_t)count * sizeof(wchar_t));
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, count);
        return out;
    }
    wchar_t* out = (wchar_t*)xmalloc((size_t)count * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out, count);
    return out;
}

static char* wide_to_utf8(const wchar_t* text) {
    if (!text) {
        return xstrdup("");
    }
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        return xstrdup("");
    }
    char* out = (char*)xmalloc((size_t)count);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out, count, NULL, NULL);
    return out;
}

static wchar_t* join_path_w(const wchar_t* left, const wchar_t* right) {
    size_t ln = wcslen(left);
    size_t rn = wcslen(right);
    int need_slash = ln > 0 && left[ln - 1] != L'\\' && left[ln - 1] != L'/';
    wchar_t* out = (wchar_t*)xmalloc((ln + rn + (need_slash ? 2 : 1)) * sizeof(wchar_t));
    memcpy(out, left, ln * sizeof(wchar_t));
    size_t pos = ln;
    if (need_slash) {
        out[pos++] = L'\\';
    }
    memcpy(out + pos, right, rn * sizeof(wchar_t));
    out[pos + rn] = L'\0';
    return out;
}

static wchar_t* append_rel_w(const wchar_t* base, const wchar_t* name) {
    if (!base || base[0] == L'\0') {
        return xwcsdup(name);
    }
    size_t bn = wcslen(base);
    size_t nn = wcslen(name);
    wchar_t* out = (wchar_t*)xmalloc((bn + nn + 2) * sizeof(wchar_t));
    memcpy(out, base, bn * sizeof(wchar_t));
    out[bn] = L'/';
    memcpy(out + bn + 1, name, nn * sizeof(wchar_t));
    out[bn + nn + 1] = L'\0';
    return out;
}

static void put_u16_le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32_le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u64_le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
    }
}

static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= ((uint64_t)p[i]) << (8 * i);
    }
    return v;
}

static uint64_t load64_le(const void* src) {
    return read_u64_le((const uint8_t*)src);
}

static void store64_le(void* dst, uint64_t value) {
    put_u64_le((uint8_t*)dst, value);
}

static uint64_t rotr64(uint64_t w, unsigned c) {
    return (w >> c) | (w << (64 - c));
}

static void blake2b_compress(Blake2bCtx* ctx, const uint8_t block[BLAKE2B_BLOCK_SIZE]) {
    uint64_t m[16];
    uint64_t v[16];
    for (size_t i = 0; i < 16; ++i) {
        m[i] = load64_le(block + i * 8);
    }
    for (size_t i = 0; i < 8; ++i) {
        v[i] = ctx->h[i];
    }
    v[8] = blake2b_iv[0];
    v[9] = blake2b_iv[1];
    v[10] = blake2b_iv[2];
    v[11] = blake2b_iv[3];
    v[12] = blake2b_iv[4] ^ ctx->t[0];
    v[13] = blake2b_iv[5] ^ ctx->t[1];
    v[14] = blake2b_iv[6] ^ ctx->f[0];
    v[15] = blake2b_iv[7] ^ ctx->f[1];

#define B2B_G(a, b, c, d, x, y)        \
    do {                               \
        v[a] = v[a] + v[b] + (x);      \
        v[d] = rotr64(v[d] ^ v[a], 32);\
        v[c] = v[c] + v[d];            \
        v[b] = rotr64(v[b] ^ v[c], 24);\
        v[a] = v[a] + v[b] + (y);      \
        v[d] = rotr64(v[d] ^ v[a], 16);\
        v[c] = v[c] + v[d];            \
        v[b] = rotr64(v[b] ^ v[c], 63);\
    } while (0)

    for (size_t r = 0; r < 12; ++r) {
        B2B_G(0, 4, 8, 12, m[blake2b_sigma[r][0]], m[blake2b_sigma[r][1]]);
        B2B_G(1, 5, 9, 13, m[blake2b_sigma[r][2]], m[blake2b_sigma[r][3]]);
        B2B_G(2, 6, 10, 14, m[blake2b_sigma[r][4]], m[blake2b_sigma[r][5]]);
        B2B_G(3, 7, 11, 15, m[blake2b_sigma[r][6]], m[blake2b_sigma[r][7]]);
        B2B_G(0, 5, 10, 15, m[blake2b_sigma[r][8]], m[blake2b_sigma[r][9]]);
        B2B_G(1, 6, 11, 12, m[blake2b_sigma[r][10]], m[blake2b_sigma[r][11]]);
        B2B_G(2, 7, 8, 13, m[blake2b_sigma[r][12]], m[blake2b_sigma[r][13]]);
        B2B_G(3, 4, 9, 14, m[blake2b_sigma[r][14]], m[blake2b_sigma[r][15]]);
    }

#undef B2B_G

    for (size_t i = 0; i < 8; ++i) {
        ctx->h[i] ^= v[i] ^ v[i + 8];
    }
}

static int blake2b_init(Blake2bCtx* ctx, size_t outlen, const uint8_t* key, size_t keylen, const uint8_t* person, size_t personlen) {
    if (!ctx || outlen == 0 || outlen > 64 || keylen > 64 || personlen > 16) {
        return 0;
    }
    memset(ctx, 0, sizeof(*ctx));
    for (size_t i = 0; i < 8; ++i) {
        ctx->h[i] = blake2b_iv[i];
    }
    ctx->outlen = outlen;

    uint8_t param[64];
    memset(param, 0, sizeof(param));
    param[0] = (uint8_t)outlen;
    param[1] = (uint8_t)keylen;
    param[2] = 1;
    param[3] = 1;
    if (person && personlen) {
        memcpy(param + 48, person, personlen);
    }
    for (size_t i = 0; i < 8; ++i) {
        ctx->h[i] ^= load64_le(param + i * 8);
    }
    if (key && keylen) {
        uint8_t block[BLAKE2B_BLOCK_SIZE];
        memset(block, 0, sizeof(block));
        memcpy(block, key, keylen);
        ctx->t[0] += BLAKE2B_BLOCK_SIZE;
        if (ctx->t[0] < BLAKE2B_BLOCK_SIZE) {
            ctx->t[1] += 1;
        }
        blake2b_compress(ctx, block);
    }
    return 1;
}

static void blake2b_update(Blake2bCtx* ctx, const uint8_t* in, size_t inlen) {
    if (!ctx || !in || inlen == 0) {
        return;
    }
    size_t left = ctx->buflen;
    size_t fill = BLAKE2B_BLOCK_SIZE - left;
    if (inlen > fill) {
        ctx->buflen = 0;
        memcpy(ctx->buf + left, in, fill);
        ctx->t[0] += BLAKE2B_BLOCK_SIZE;
        if (ctx->t[0] < BLAKE2B_BLOCK_SIZE) {
            ctx->t[1] += 1;
        }
        blake2b_compress(ctx, ctx->buf);
        in += fill;
        inlen -= fill;
        while (inlen > BLAKE2B_BLOCK_SIZE) {
            ctx->t[0] += BLAKE2B_BLOCK_SIZE;
            if (ctx->t[0] < BLAKE2B_BLOCK_SIZE) {
                ctx->t[1] += 1;
            }
            blake2b_compress(ctx, in);
            in += BLAKE2B_BLOCK_SIZE;
            inlen -= BLAKE2B_BLOCK_SIZE;
        }
    }
    memcpy(ctx->buf + ctx->buflen, in, inlen);
    ctx->buflen += inlen;
}

static void blake2b_final(Blake2bCtx* ctx, uint8_t* out, size_t outlen) {
    uint8_t full[64];
    if (!ctx || !out || outlen < ctx->outlen) {
        return;
    }
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) {
        ctx->t[1] += 1;
    }
    ctx->f[0] = ~0ULL;
    memset(ctx->buf + ctx->buflen, 0, BLAKE2B_BLOCK_SIZE - ctx->buflen);
    blake2b_compress(ctx, ctx->buf);
    for (size_t i = 0; i < 8; ++i) {
        store64_le(full + i * 8, ctx->h[i]);
    }
    memcpy(out, full, ctx->outlen);
}

static void blake2b_digest(uint8_t* out, size_t outlen, const uint8_t* key, size_t keylen, const uint8_t* person, size_t personlen, const uint8_t* data, size_t data_size) {
    Blake2bCtx ctx;
    blake2b_init(&ctx, outlen, key, keylen, person, personlen);
    blake2b_update(&ctx, data, data_size);
    blake2b_final(&ctx, out, outlen);
}

static char* normalize_relpath_alloc(const char* rel) {
    wchar_t* w = utf8_to_wide(rel);
    for (wchar_t* p = w; *p; ++p) {
        if (*p == L'\\') {
            *p = L'/';
        }
        *p = (wchar_t)towlower(*p);
    }
    char* out = wide_to_utf8(w);
    free(w);
    return out;
}

static void custom_hash_bytes(const char* rel, uint8_t out[CIALLO_HASH_SIZE]) {
    static const uint8_t person[] = "CialloHashV4";
    char* norm = normalize_relpath_alloc(rel);
    blake2b_digest(out, CIALLO_HASH_SIZE, NULL, 0, person, sizeof(person) - 1, (const uint8_t*)norm, strlen(norm));
    free(norm);
}

static void content_hash_bytes(const uint8_t* data, size_t size, uint8_t out[32]) {
    static const uint8_t person[] = "CialloDedupV4";
    blake2b_digest(out, 32, NULL, 0, person, sizeof(person) - 1, data, size);
}

static void derive_key(const uint8_t* scope, size_t scope_size, const uint8_t* material, size_t material_size, uint64_t size, uint8_t out[32]) {
    static const uint8_t person[] = "CialloKeyV4";
    Blake2bCtx ctx;
    uint8_t le[8];
    put_u64_le(le, size);
    blake2b_init(&ctx, 32, (const uint8_t*)CIALLO_MASTER_KEY, CIALLO_MASTER_KEY_SIZE, person, sizeof(person) - 1);
    blake2b_update(&ctx, scope, scope_size);
    blake2b_update(&ctx, material, material_size);
    blake2b_update(&ctx, le, sizeof(le));
    blake2b_final(&ctx, out, 32);
}

static void xor_crypt(const uint8_t* data, size_t size, const uint8_t key[32], const uint8_t nonce[CIALLO_NONCE_SIZE], uint8_t* out) {
    static const uint8_t person[] = "CialloXorV4";
    size_t pos = 0;
    while (pos < size) {
        uint64_t counter = (uint64_t)(pos / 64);
        size_t inner = pos % 64;
        uint8_t counter_le[8];
        uint8_t block[64];
        Blake2bCtx ctx;
        put_u64_le(counter_le, counter);
        blake2b_init(&ctx, 64, key, 32, person, sizeof(person) - 1);
        blake2b_update(&ctx, nonce, CIALLO_NONCE_SIZE);
        blake2b_update(&ctx, counter_le, sizeof(counter_le));
        blake2b_final(&ctx, block, sizeof(block));
        size_t take = 64 - inner;
        if (take > size - pos) {
            take = size - pos;
        }
        for (size_t i = 0; i < take; ++i) {
            out[pos + i] = data[pos + i] ^ block[inner + i];
        }
        pos += take;
    }
}

static void to_hex_upper(const uint8_t* data, size_t size, char* out) {
    static const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = hex[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[data[i] & 0xF];
    }
    out[size * 2] = '\0';
}

static const char* mode_name(uint8_t mode) {
    switch (mode) {
    case CIALLO_MODE_RAW: return "raw";
    case CIALLO_MODE_ZLIB: return "zlib";
    case CIALLO_MODE_ZSTD: return "zstd";
    case CIALLO_MODE_LZMA: return "lzma";
    default: return "unknown";
    }
}

static int file_tell64(FILE* fp, uint64_t* out) {
    __int64 pos = _ftelli64(fp);
    if (pos < 0) {
        return 0;
    }
    *out = (uint64_t)pos;
    return 1;
}

static uint64_t file_size_w(const wchar_t* path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) {
        return 0;
    }
    return ((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow;
}

static int read_file_w(const wchar_t* path, ByteVec* out) {
    FILE* fp = _wfopen(path, L"rb");
    if (!fp) {
        fwprintf(stderr, L"open failed: %ls\n", path);
        return 0;
    }
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    __int64 sz = _ftelli64(fp);
    if (sz < 0 || (uint64_t)sz > (uint64_t)SIZE_MAX) {
        fclose(fp);
        fprintf(stderr, "file too large for this build\n");
        return 0;
    }
    if (_fseeki64(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    vec_resize(out, (size_t)sz);
    if (out->size && fread(out->data, 1, out->size, fp) != out->size) {
        fclose(fp);
        fprintf(stderr, "read failed\n");
        return 0;
    }
    fclose(fp);
    return 1;
}

static int write_file_w(const wchar_t* path, const uint8_t* data, size_t size);

static int create_dirs_w(const wchar_t* path) {
    if (!path || !path[0]) {
        return 1;
    }
    wchar_t* tmp = xwcsdup(path);
    size_t len = wcslen(tmp);
    for (size_t i = 0; i < len; ++i) {
        if (tmp[i] == L'/') {
            tmp[i] = L'\\';
        }
    }
    for (size_t i = 0; i < len; ++i) {
        if (tmp[i] == L'\\') {
            if (i == 0 || (i == 2 && tmp[1] == L':')) {
                continue;
            }
            wchar_t saved = tmp[i];
            tmp[i] = L'\0';
            if (tmp[0] && !CreateDirectoryW(tmp, NULL)) {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS) {
                    free(tmp);
                    return 0;
                }
            }
            tmp[i] = saved;
        }
    }
    if (!CreateDirectoryW(tmp, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            free(tmp);
            return 0;
        }
    }
    free(tmp);
    return 1;
}

static wchar_t* parent_dir_w(const wchar_t* path) {
    wchar_t* tmp = xwcsdup(path);
    wchar_t* last1 = wcsrchr(tmp, L'\\');
    wchar_t* last2 = wcsrchr(tmp, L'/');
    wchar_t* last = last1 > last2 ? last1 : last2;
    if (!last) {
        tmp[0] = L'\0';
        return tmp;
    }
    if (last == tmp) {
        last[1] = L'\0';
    } else {
        *last = L'\0';
    }
    return tmp;
}

static int ensure_parent_dir_w(const wchar_t* path) {
    wchar_t* parent = parent_dir_w(path);
    int ok = 1;
    if (parent[0]) {
        ok = create_dirs_w(parent);
    }
    free(parent);
    return ok;
}

static int write_file_w(const wchar_t* path, const uint8_t* data, size_t size) {
    if (!ensure_parent_dir_w(path)) {
        fwprintf(stderr, L"mkdir failed: %ls\n", path);
        return 0;
    }
    FILE* fp = _wfopen(path, L"wb");
    if (!fp) {
        fwprintf(stderr, L"open output failed: %ls\n", path);
        return 0;
    }
    if (size && fwrite(data, 1, size, fp) != size) {
        fclose(fp);
        fprintf(stderr, "write failed\n");
        return 0;
    }
    fclose(fp);
    return 1;
}

static int collect_recursive(const wchar_t* dir, const wchar_t* rel_base, FileList* list) {
    wchar_t* pattern = join_path_w(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        wchar_t* full = join_path_w(dir, fd.cFileName);
        wchar_t* rel = append_rel_w(rel_base, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collect_recursive(full, rel, list);
        } else {
            char* rel_utf8 = wide_to_utf8(rel);
            uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;
            file_list_add(list, full, rel_utf8, size);
            free(rel_utf8);
        }
        free(full);
        free(rel);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

static int collect_files(const wchar_t* input_dir, FileList* out) {
    DWORD attr = GetFileAttributesW(input_dir);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        fwprintf(stderr, L"input directory not found: %ls\n", input_dir);
        return 0;
    }
    collect_recursive(input_dir, L"", out);
    qsort(out->items, out->count, sizeof(FileItem), cmp_file_item);
    return out->count > 0;
}

static int compress_zlib(const uint8_t* data, size_t size, ByteVec* out) {
    if (size > 0xFFFFFFFFu) {
        fprintf(stderr, "zlib path supports files up to 4GB\n");
        return 0;
    }
    mz_ulong bound = mz_compressBound((mz_ulong)size);
    vec_resize(out, (size_t)bound);
    mz_ulong dest_len = bound;
    int res = mz_compress2(out->data, &dest_len, data, (mz_ulong)size, MZ_BEST_COMPRESSION);
    if (res != MZ_OK) {
        vec_free(out);
        fprintf(stderr, "zlib compress failed: %d\n", res);
        return 0;
    }
    out->size = (size_t)dest_len;
    return 1;
}

static int compress_zstd(const uint8_t* data, size_t size, int level, ByteVec* out) {
    size_t bound = ZSTD_compressBound(size);
    if (ZSTD_isError(bound)) {
        fprintf(stderr, "zstd compressBound failed: %s\n", ZSTD_getErrorName(bound));
        return 0;
    }
    vec_resize(out, bound);
    size_t ret = ZSTD_compress(out->data, out->size, data, size, level);
    if (ZSTD_isError(ret)) {
        vec_free(out);
        fprintf(stderr, "zstd compress failed: %s\n", ZSTD_getErrorName(ret));
        return 0;
    }
    out->size = ret;
    return 1;
}

static void* sz_alloc(ISzAllocPtr p, size_t size) {
    (void)p;
    return malloc(size);
}

static void sz_free(ISzAllocPtr p, void* address) {
    (void)p;
    free(address);
}

static int compress_lzma(const uint8_t* data, size_t size, ByteVec* out) {
    if (size > (uint64_t)SIZE_MAX - 1024 * 1024) {
        return 0;
    }
    CLzmaEncProps props;
    LzmaEncProps_Init(&props);
    props.level = 9;
    props.dictSize = CIALLO_LZMA_DICT_SIZE;
    props.lc = CIALLO_LZMA_LC;
    props.lp = CIALLO_LZMA_LP;
    props.pb = CIALLO_LZMA_PB;
    props.algo = 1;
    props.fb = 273;
    props.btMode = 1;
    props.numHashBytes = 4;
    props.numThreads = 1;

    ISzAlloc alloc = { sz_alloc, sz_free };
    size_t cap = size + size / 3 + 1024 * 1024 + 64;
    if (cap < 1024) {
        cap = 1024;
    }

    for (int attempt = 0; attempt < 4; ++attempt) {
        vec_resize(out, cap + LZMA_PROPS_SIZE);
        SizeT dest_len = (SizeT)cap;
        SizeT props_size = LZMA_PROPS_SIZE;
        SRes res = LzmaEncode(out->data + LZMA_PROPS_SIZE, &dest_len, data, (SizeT)size,
                              &props, out->data, &props_size, 1, NULL, &alloc, &alloc);
        if (res == SZ_OK && props_size == LZMA_PROPS_SIZE) {
            out->size = LZMA_PROPS_SIZE + (size_t)dest_len;
            return 1;
        }
        if (res == SZ_ERROR_OUTPUT_EOF) {
            cap *= 2;
            continue;
        }
        vec_free(out);
        fprintf(stderr, "lzma compress failed: %d\n", (int)res);
        return 0;
    }
    vec_free(out);
    fprintf(stderr, "lzma compress output overflow\n");
    return 0;
}

static int decompress_zlib(const uint8_t* payload, size_t payload_size, uint64_t expected, ByteVec* out) {
    if (expected > 0xFFFFFFFFu || payload_size > 0xFFFFFFFFu || expected > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "zlib payload too large\n");
        return 0;
    }
    vec_resize(out, (size_t)expected);
    mz_ulong dest_len = (mz_ulong)expected;
    int res = mz_uncompress(out->data, &dest_len, payload, (mz_ulong)payload_size);
    if (res != MZ_OK || dest_len != (mz_ulong)expected) {
        vec_free(out);
        fprintf(stderr, "zlib decompress failed: %d\n", res);
        return 0;
    }
    return 1;
}

static int decompress_zstd(const uint8_t* payload, size_t payload_size, uint64_t expected, ByteVec* out) {
    if (expected > (uint64_t)SIZE_MAX) {
        return 0;
    }
    vec_resize(out, (size_t)expected);
    size_t ret = ZSTD_decompress(out->data, out->size, payload, payload_size);
    if (ZSTD_isError(ret) || ret != out->size) {
        fprintf(stderr, "zstd decompress failed: %s\n", ZSTD_isError(ret) ? ZSTD_getErrorName(ret) : "size mismatch");
        vec_free(out);
        return 0;
    }
    return 1;
}

static int decompress_lzma(const uint8_t* payload, size_t payload_size, uint64_t expected, ByteVec* out) {
    if (payload_size < LZMA_PROPS_SIZE || expected > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "bad lzma payload\n");
        return 0;
    }
    vec_resize(out, (size_t)expected);
    SizeT dst_len = (SizeT)expected;
    SizeT src_len = (SizeT)(payload_size - LZMA_PROPS_SIZE);
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    ISzAlloc alloc = { sz_alloc, sz_free };
    SRes res = LzmaDecode(out->data, &dst_len, payload + LZMA_PROPS_SIZE, &src_len,
                          payload, LZMA_PROPS_SIZE, LZMA_FINISH_END, &status, &alloc);
    if (res != SZ_OK || dst_len != (SizeT)expected || src_len != (SizeT)(payload_size - LZMA_PROPS_SIZE)) {
        vec_free(out);
        fprintf(stderr, "lzma decompress failed: %d\n", (int)res);
        return 0;
    }
    return 1;
}

static int make_packed_payload(const uint8_t* data, size_t size, const char* compression, ByteVec* out, uint8_t* mode_out) {
    ByteVec zlib_payload = { 0 };
    ByteVec zstd_payload = { 0 };
    ByteVec lzma_payload = { 0 };
    const char* c = compression ? compression : "auto";

    if (_stricmp(c, "raw") == 0) {
        vec_append_u8(out, CIALLO_MODE_RAW);
        vec_append(out, data, size);
        *mode_out = CIALLO_MODE_RAW;
        return 1;
    }

    if (_stricmp(c, "zstd") == 0) {
        if (!compress_zstd(data, size, 22, &zstd_payload)) {
            return 0;
        }
        if (zstd_payload.size + 1 < size) {
            vec_append_u8(out, CIALLO_MODE_ZSTD);
            vec_append(out, zstd_payload.data, zstd_payload.size);
            *mode_out = CIALLO_MODE_ZSTD;
        } else {
            vec_append_u8(out, CIALLO_MODE_RAW);
            vec_append(out, data, size);
            *mode_out = CIALLO_MODE_RAW;
        }
        vec_free(&zstd_payload);
        return 1;
    }

    if (_stricmp(c, "zlib") == 0) {
        if (!compress_zlib(data, size, &zlib_payload)) {
            return 0;
        }
        if (zlib_payload.size + 1 < size) {
            vec_append_u8(out, CIALLO_MODE_ZLIB);
            vec_append(out, zlib_payload.data, zlib_payload.size);
            *mode_out = CIALLO_MODE_ZLIB;
        } else {
            vec_append_u8(out, CIALLO_MODE_RAW);
            vec_append(out, data, size);
            *mode_out = CIALLO_MODE_RAW;
        }
        vec_free(&zlib_payload);
        return 1;
    }

    if (_stricmp(c, "lzma") == 0) {
        if (!compress_lzma(data, size, &lzma_payload)) {
            return 0;
        }
        if (lzma_payload.size + 1 < size) {
            vec_append_u8(out, CIALLO_MODE_LZMA);
            vec_append(out, lzma_payload.data, lzma_payload.size);
            *mode_out = CIALLO_MODE_LZMA;
        } else {
            vec_append_u8(out, CIALLO_MODE_RAW);
            vec_append(out, data, size);
            *mode_out = CIALLO_MODE_RAW;
        }
        vec_free(&lzma_payload);
        return 1;
    }

    if (_stricmp(c, "auto") == 0) {
        const uint8_t* best_data = data;
        size_t best_size = size;
        uint8_t best_mode = CIALLO_MODE_RAW;
        if (compress_zlib(data, size, &zlib_payload) && zlib_payload.size < best_size) {
            best_data = zlib_payload.data;
            best_size = zlib_payload.size;
            best_mode = CIALLO_MODE_ZLIB;
        }
        if (compress_zstd(data, size, 22, &zstd_payload) && zstd_payload.size < best_size) {
            best_data = zstd_payload.data;
            best_size = zstd_payload.size;
            best_mode = CIALLO_MODE_ZSTD;
        }
        if (compress_lzma(data, size, &lzma_payload) && lzma_payload.size < best_size) {
            best_data = lzma_payload.data;
            best_size = lzma_payload.size;
            best_mode = CIALLO_MODE_LZMA;
        }
        vec_append_u8(out, best_mode);
        vec_append(out, best_data, best_size);
        *mode_out = best_mode;
        vec_free(&zlib_payload);
        vec_free(&zstd_payload);
        vec_free(&lzma_payload);
        return 1;
    }

    fprintf(stderr, "unsupported compression: %s\n", c);
    return 0;
}

static int unpack_packed_payload(const uint8_t* packed, size_t packed_size, uint64_t orig_size, ByteVec* raw) {
    if (packed_size < 1) {
        fprintf(stderr, "empty packed payload\n");
        return 0;
    }
    uint8_t mode = packed[0];
    const uint8_t* payload = packed + 1;
    size_t payload_size = packed_size - 1;
    switch (mode) {
    case CIALLO_MODE_RAW:
        if (orig_size > (uint64_t)SIZE_MAX || payload_size != (size_t)orig_size) {
            fprintf(stderr, "raw size mismatch\n");
            return 0;
        }
        vec_append(raw, payload, payload_size);
        return 1;
    case CIALLO_MODE_ZLIB:
        return decompress_zlib(payload, payload_size, orig_size, raw);
    case CIALLO_MODE_ZSTD:
        return decompress_zstd(payload, payload_size, orig_size, raw);
    case CIALLO_MODE_LZMA:
        return decompress_lzma(payload, payload_size, orig_size, raw);
    default:
        fprintf(stderr, "unknown compression mode: %u\n", (unsigned)mode);
        return 0;
    }
}

static void build_header(uint8_t out[CIALLO_HEADER_SIZE], uint32_t file_count, uint64_t index_offset, uint64_t index_size, const uint8_t index_nonce[CIALLO_NONCE_SIZE]) {
    memcpy(out, CIALLO_MAGIC, CIALLO_MAGIC_SIZE);
    put_u16_le(out + 9, CIALLO_VERSION);
    put_u32_le(out + 11, file_count);
    put_u64_le(out + 15, index_offset);
    put_u64_le(out + 23, index_size);
    memcpy(out + 31, index_nonce, CIALLO_NONCE_SIZE);
}

static int read_header(FILE* fp, uint32_t* file_count, uint64_t* index_offset, uint64_t* index_size, uint8_t index_nonce[CIALLO_NONCE_SIZE]) {
    uint8_t h[CIALLO_HEADER_SIZE];
    if (fread(h, 1, sizeof(h), fp) != sizeof(h)) {
        fprintf(stderr, "pak too small\n");
        return 0;
    }
    if (memcmp(h, CIALLO_MAGIC, CIALLO_MAGIC_SIZE) != 0) {
        fprintf(stderr, "bad magic\n");
        return 0;
    }
    uint16_t version = read_u16_le(h + 9);
    if (version != CIALLO_VERSION) {
        fprintf(stderr, "unsupported version: %u\n", (unsigned)version);
        return 0;
    }
    *file_count = read_u32_le(h + 11);
    *index_offset = read_u64_le(h + 15);
    *index_size = read_u64_le(h + 23);
    memcpy(index_nonce, h + 31, CIALLO_NONCE_SIZE);
    return 1;
}

static void build_index(const EntryList* entries, ByteVec* out) {
    PakEntry* sorted = (PakEntry*)xmalloc(entries->count * sizeof(PakEntry));
    memcpy(sorted, entries->items, entries->count * sizeof(PakEntry));
    qsort(sorted, entries->count, sizeof(PakEntry), cmp_entry_hash);
    uint8_t tmp[8];
    put_u32_le(tmp, (uint32_t)entries->count);
    vec_append(out, tmp, 4);
    for (size_t i = 0; i < entries->count; ++i) {
        vec_append(out, sorted[i].hash, CIALLO_HASH_SIZE);
        vec_append_u8(out, sorted[i].flags);
        put_u64_le(tmp, sorted[i].orig_size);
        vec_append(out, tmp, 8);
        put_u64_le(tmp, sorted[i].stored_size);
        vec_append(out, tmp, 8);
        put_u64_le(tmp, sorted[i].offset);
        vec_append(out, tmp, 8);
        vec_append(out, sorted[i].nonce, CIALLO_NONCE_SIZE);
    }
    free(sorted);
}

static int parse_index(const uint8_t* data, size_t size, EntryList* entries) {
    if (size < 4) {
        fprintf(stderr, "bad index\n");
        return 0;
    }
    uint32_t count = read_u32_le(data);
    size_t need = 4 + (size_t)count * CIALLO_INDEX_ENTRY_SIZE;
    if (count > (SIZE_MAX - 4) / CIALLO_INDEX_ENTRY_SIZE || need > size) {
        fprintf(stderr, "truncated index\n");
        return 0;
    }
    size_t pos = 4;
    for (uint32_t i = 0; i < count; ++i) {
        PakEntry e;
        memcpy(e.hash, data + pos, CIALLO_HASH_SIZE);
        pos += CIALLO_HASH_SIZE;
        e.flags = data[pos++];
        e.orig_size = read_u64_le(data + pos);
        pos += 8;
        e.stored_size = read_u64_le(data + pos);
        pos += 8;
        e.offset = read_u64_le(data + pos);
        pos += 8;
        memcpy(e.nonce, data + pos, CIALLO_NONCE_SIZE);
        pos += CIALLO_NONCE_SIZE;
        entry_list_add(entries, &e);
    }
    return 1;
}

static int load_index_w(const wchar_t* pak_path, EntryList* entries, uint32_t* file_count_out) {
    FILE* fp = _wfopen(pak_path, L"rb");
    if (!fp) {
        fwprintf(stderr, L"open pak failed: %ls\n", pak_path);
        return 0;
    }
    uint32_t file_count = 0;
    uint64_t index_offset = 0;
    uint64_t index_size = 0;
    uint8_t index_nonce[CIALLO_NONCE_SIZE];
    if (!read_header(fp, &file_count, &index_offset, &index_size, index_nonce)) {
        fclose(fp);
        return 0;
    }
    if (index_size > (uint64_t)SIZE_MAX) {
        fclose(fp);
        fprintf(stderr, "index too large\n");
        return 0;
    }
    if (_fseeki64(fp, (__int64)index_offset, SEEK_SET) != 0) {
        fclose(fp);
        fprintf(stderr, "seek index failed\n");
        return 0;
    }
    ByteVec enc = { 0 };
    ByteVec plain = { 0 };
    vec_resize(&enc, (size_t)index_size);
    if (enc.size && fread(enc.data, 1, enc.size, fp) != enc.size) {
        fclose(fp);
        vec_free(&enc);
        fprintf(stderr, "read index failed\n");
        return 0;
    }
    fclose(fp);
    vec_resize(&plain, enc.size);
    uint8_t key[32];
    derive_key((const uint8_t*)"INDEX", 5, (const uint8_t*)"__index__", 9, index_size, key);
    xor_crypt(enc.data, enc.size, key, index_nonce, plain.data);
    int ok = parse_index(plain.data, plain.size, entries);
    vec_free(&enc);
    vec_free(&plain);
    if (file_count_out) {
        *file_count_out = file_count;
    }
    return ok;
}

static const DedupRecord* find_dedup(const DedupList* list, const uint8_t content_id[32]) {
    for (size_t i = 0; i < list->count; ++i) {
        if (memcmp(list->items[i].content_id, content_id, 32) == 0) {
            return &list->items[i];
        }
    }
    return NULL;
}

static int hash_seen_collision(const EntryList* entries, const uint8_t hash[CIALLO_HASH_SIZE]) {
    for (size_t i = 0; i < entries->count; ++i) {
        if (memcmp(entries->items[i].hash, hash, CIALLO_HASH_SIZE) == 0) {
            return 1;
        }
    }
    return 0;
}

static int write_manifest_w(const wchar_t* manifest_path, const FileList* files) {
    if (!ensure_parent_dir_w(manifest_path)) {
        return 0;
    }
    FILE* fp = _wfopen(manifest_path, L"wb");
    if (!fp) {
        fwprintf(stderr, L"open manifest failed: %ls\n", manifest_path);
        return 0;
    }
    for (size_t i = 0; i < files->count; ++i) {
        uint8_t hash[CIALLO_HASH_SIZE];
        char hex[33];
        custom_hash_bytes(files->items[i].rel, hash);
        to_hex_upper(hash, CIALLO_HASH_SIZE, hex);
        fprintf(fp, "%s\t%s\t%s", hex, hex, files->items[i].rel);
        if (i + 1 < files->count) {
            fputc('\n', fp);
        }
    }
    fclose(fp);
    return 1;
}

static int pack_command(const wchar_t* input_dir, const wchar_t* pak_path, const wchar_t* manifest_path, int dedup, const char* compression) {
    FileList files = { 0 };
    EntryList entries = { 0 };
    DedupList dedups = { 0 };
    uint64_t logical_size = 0;
    uint64_t reused_count = 0;
    clock_t start = clock();

    if (!collect_files(input_dir, &files)) {
        fprintf(stderr, "input directory has no files\n");
        free_file_list(&files);
        return 1;
    }
    for (size_t i = 0; i < files.count; ++i) {
        logical_size += files.items[i].size;
    }
    if (!write_manifest_w(manifest_path, &files)) {
        free_file_list(&files);
        return 1;
    }
    if (!ensure_parent_dir_w(pak_path)) {
        free_file_list(&files);
        return 1;
    }
    FILE* fp = _wfopen(pak_path, L"wb+");
    if (!fp) {
        fwprintf(stderr, L"open pak output failed: %ls\n", pak_path);
        free_file_list(&files);
        return 1;
    }
    uint8_t zero_header[CIALLO_HEADER_SIZE] = { 0 };
    fwrite(zero_header, 1, sizeof(zero_header), fp);

    for (size_t i = 0; i < files.count; ++i) {
        ByteVec raw = { 0 };
        ByteVec packed = { 0 };
        ByteVec enc = { 0 };
        uint8_t path_hash[CIALLO_HASH_SIZE];
        uint8_t content_id[32];
        uint8_t mode = CIALLO_MODE_RAW;
        PakEntry entry;
        memset(&entry, 0, sizeof(entry));

        if (!read_file_w(files.items[i].full_path, &raw)) {
            fclose(fp);
            free_file_list(&files);
            return 1;
        }
        custom_hash_bytes(files.items[i].rel, path_hash);
        if (hash_seen_collision(&entries, path_hash)) {
            fprintf(stderr, "hash collision or duplicate path: %s\n", files.items[i].rel);
            vec_free(&raw);
            fclose(fp);
            free_file_list(&files);
            return 1;
        }
        memcpy(entry.hash, path_hash, CIALLO_HASH_SIZE);
        content_hash_bytes(raw.data, raw.size, content_id);

        const DedupRecord* reused = dedup ? find_dedup(&dedups, content_id) : NULL;
        if (reused) {
            entry.flags = CIALLO_FLAG_DEDUP_REUSED;
            entry.orig_size = reused->orig_size;
            entry.stored_size = reused->stored_size;
            entry.offset = reused->offset;
            memcpy(entry.nonce, reused->nonce, CIALLO_NONCE_SIZE);
            mode = reused->mode;
            reused_count += 1;
        } else {
            if (!make_packed_payload(raw.data, raw.size, compression, &packed, &mode)) {
                vec_free(&raw);
                fclose(fp);
                free_file_list(&files);
                return 1;
            }
            static const uint8_t nonce_person[] = "CialloNonceV4";
            blake2b_digest(entry.nonce, CIALLO_NONCE_SIZE, NULL, 0, nonce_person, sizeof(nonce_person) - 1, content_id, sizeof(content_id));
            uint8_t key[32];
            derive_key((const uint8_t*)"DATA", 4, entry.nonce, CIALLO_NONCE_SIZE, (uint64_t)raw.size, key);
            vec_resize(&enc, packed.size);
            xor_crypt(packed.data, packed.size, key, entry.nonce, enc.data);
            if (!file_tell64(fp, &entry.offset)) {
                fprintf(stderr, "tell failed\n");
                return 1;
            }
            if (enc.size && fwrite(enc.data, 1, enc.size, fp) != enc.size) {
                fprintf(stderr, "pak write failed\n");
                return 1;
            }
            entry.orig_size = (uint64_t)raw.size;
            entry.stored_size = (uint64_t)enc.size;
            if (dedup) {
                DedupRecord rec;
                memcpy(rec.content_id, content_id, 32);
                rec.offset = entry.offset;
                rec.stored_size = entry.stored_size;
                rec.orig_size = entry.orig_size;
                memcpy(rec.nonce, entry.nonce, CIALLO_NONCE_SIZE);
                rec.mode = mode;
                dedup_list_add(&dedups, &rec);
            }
        }
        entry_list_add(&entries, &entry);
        printf("[%zu/%zu] %s %s | mode=%s | %llu/%llu\n",
               i + 1, files.count, reused ? "reuse" : "write", files.items[i].rel, mode_name(mode),
               (unsigned long long)entry.stored_size, (unsigned long long)entry.orig_size);
        vec_free(&raw);
        vec_free(&packed);
        vec_free(&enc);
    }

    uint64_t index_offset = 0;
    file_tell64(fp, &index_offset);
    ByteVec index_plain = { 0 };
    ByteVec index_enc = { 0 };
    build_index(&entries, &index_plain);
    uint8_t nonce_material[12];
    put_u32_le(nonce_material, (uint32_t)entries.count);
    put_u64_le(nonce_material + 4, index_offset);
    uint8_t index_nonce[CIALLO_NONCE_SIZE];
    static const uint8_t index_person[] = "CialloIdxV4";
    blake2b_digest(index_nonce, CIALLO_NONCE_SIZE, NULL, 0, index_person, sizeof(index_person) - 1, nonce_material, sizeof(nonce_material));
    uint8_t index_key[32];
    derive_key((const uint8_t*)"INDEX", 5, (const uint8_t*)"__index__", 9, (uint64_t)index_plain.size, index_key);
    vec_resize(&index_enc, index_plain.size);
    xor_crypt(index_plain.data, index_plain.size, index_key, index_nonce, index_enc.data);
    if (index_enc.size && fwrite(index_enc.data, 1, index_enc.size, fp) != index_enc.size) {
        fprintf(stderr, "write index failed\n");
        fclose(fp);
        return 1;
    }
    uint8_t header[CIALLO_HEADER_SIZE];
    build_header(header, (uint32_t)entries.count, index_offset, (uint64_t)index_enc.size, index_nonce);
    _fseeki64(fp, 0, SEEK_SET);
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);

    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    uint64_t pak_size = file_size_w(pak_path);
    printf("pack summary: files=%zu | original=%llu | pak=%llu | ratio=%.2f%% | dedup=%llu | time=%.3fs\n",
           entries.count, (unsigned long long)logical_size, (unsigned long long)pak_size,
           logical_size ? (100.0 * (double)pak_size / (double)logical_size) : 0.0,
           (unsigned long long)reused_count, elapsed);

    vec_free(&index_plain);
    vec_free(&index_enc);
    free_file_list(&files);
    free(entries.items);
    free(dedups.items);
    return 0;
}

static int load_manifest_w(const wchar_t* manifest_path, ManifestList* out) {
    ByteVec raw = { 0 };
    if (!manifest_path) {
        return 1;
    }
    if (!read_file_w(manifest_path, &raw)) {
        return 0;
    }
    vec_append_u8(&raw, 0);
    char* text = (char*)raw.data;
    char* line = text;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) {
            *next++ = '\0';
        }
        size_t len = strlen(line);
        if (len && line[len - 1] == '\r') {
            line[len - 1] = '\0';
        }
        if (*line) {
            char* first = strchr(line, '\t');
            if (!first) {
                fprintf(stderr, "bad manifest line: %s\n", line);
                vec_free(&raw);
                return 0;
            }
            *first = '\0';
            char* second = strchr(first + 1, '\t');
            char* rel = NULL;
            if (second) {
                *second = '\0';
                rel = second + 1;
            } else {
                rel = first + 1;
            }
            for (char* p = line; *p; ++p) {
                if (*p >= 'a' && *p <= 'f') {
                    *p = (char)(*p - 'a' + 'A');
                }
            }
            manifest_list_add(out, line, rel);
        }
        line = next;
    }
    vec_free(&raw);
    return 1;
}

static const char* manifest_lookup(const ManifestList* list, const uint8_t hash[CIALLO_HASH_SIZE]) {
    char hex[33];
    to_hex_upper(hash, CIALLO_HASH_SIZE, hex);
    for (size_t i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i].hash_hex, hex) == 0) {
            return list->items[i].rel;
        }
    }
    return NULL;
}

static wchar_t* build_output_path(const wchar_t* output_dir, const char* rel) {
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) {
        return NULL;
    }
    wchar_t* rel_w = utf8_to_wide(rel);
    for (wchar_t* p = rel_w; *p; ++p) {
        if (*p == L'/') {
            *p = L'\\';
        }
    }
    wchar_t* scan = rel_w;
    while (*scan) {
        wchar_t* end = wcschr(scan, L'\\');
        if (end) {
            *end = L'\0';
        }
        if (wcscmp(scan, L"..") == 0 || wcscmp(scan, L".") == 0 || scan[0] == L'\0') {
            free(rel_w);
            return NULL;
        }
        if (!end) {
            break;
        }
        *end = L'\\';
        scan = end + 1;
    }
    wchar_t* out = join_path_w(output_dir, rel_w);
    free(rel_w);
    return out;
}

static int read_entry_raw(FILE* fp, const PakEntry* entry, ByteVec* raw) {
    if (entry->stored_size > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "entry too large\n");
        return 0;
    }
    if (_fseeki64(fp, (__int64)entry->offset, SEEK_SET) != 0) {
        fprintf(stderr, "seek entry failed\n");
        return 0;
    }
    ByteVec enc = { 0 };
    ByteVec packed = { 0 };
    vec_resize(&enc, (size_t)entry->stored_size);
    if (enc.size && fread(enc.data, 1, enc.size, fp) != enc.size) {
        vec_free(&enc);
        fprintf(stderr, "read entry failed\n");
        return 0;
    }
    uint8_t key[32];
    derive_key((const uint8_t*)"DATA", 4, entry->nonce, CIALLO_NONCE_SIZE, entry->orig_size, key);
    vec_resize(&packed, enc.size);
    xor_crypt(enc.data, enc.size, key, entry->nonce, packed.data);
    int ok = unpack_packed_payload(packed.data, packed.size, entry->orig_size, raw);
    vec_free(&enc);
    vec_free(&packed);
    return ok;
}

static int remove_tree_contents_w(const wchar_t* dir) {
    wchar_t* pattern = join_path_w(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }
        wchar_t* full = join_path_w(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            remove_tree_contents_w(full);
            RemoveDirectoryW(full);
        } else {
            SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(full);
        }
        free(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}

static int unpack_command(const wchar_t* pak_path, const wchar_t* manifest_path, const wchar_t* output_dir, int clear_output) {
    EntryList entries = { 0 };
    ManifestList manifest = { 0 };
    uint32_t file_count = 0;
    clock_t start = clock();
    if (!load_index_w(pak_path, &entries, &file_count)) {
        return 1;
    }
    if (manifest_path && !load_manifest_w(manifest_path, &manifest)) {
        free(entries.items);
        return 1;
    }
    if (clear_output) {
        remove_tree_contents_w(output_dir);
    }
    create_dirs_w(output_dir);
    FILE* fp = _wfopen(pak_path, L"rb");
    if (!fp) {
        fwprintf(stderr, L"open pak failed: %ls\n", pak_path);
        free(entries.items);
        manifest_list_free(&manifest);
        return 1;
    }
    uint64_t logical = 0;
    for (size_t i = 0; i < entries.count; ++i) {
        char generated[64];
        const char* rel = NULL;
        if (manifest.count) {
            rel = manifest_lookup(&manifest, entries.items[i].hash);
            if (!rel) {
                char hex[33];
                to_hex_upper(entries.items[i].hash, CIALLO_HASH_SIZE, hex);
                fprintf(stderr, "manifest lacks hash: %s\n", hex);
                fclose(fp);
                free(entries.items);
                manifest_list_free(&manifest);
                return 1;
            }
        } else {
            char hex[33];
            to_hex_upper(entries.items[i].hash, CIALLO_HASH_SIZE, hex);
            snprintf(generated, sizeof(generated), "%s.bin", hex);
            rel = generated;
        }
        ByteVec raw = { 0 };
        if (!read_entry_raw(fp, &entries.items[i], &raw)) {
            fclose(fp);
            free(entries.items);
            manifest_list_free(&manifest);
            return 1;
        }
        wchar_t* out_path = build_output_path(output_dir, rel);
        if (!out_path) {
            fprintf(stderr, "unsafe output path: %s\n", rel);
            vec_free(&raw);
            fclose(fp);
            free(entries.items);
            manifest_list_free(&manifest);
            return 1;
        }
        if (!write_file_w(out_path, raw.data, raw.size)) {
            free(out_path);
            vec_free(&raw);
            fclose(fp);
            free(entries.items);
            manifest_list_free(&manifest);
            return 1;
        }
        logical += (uint64_t)raw.size;
        printf("[%zu/%zu] unpack %s | %llu\n", i + 1, entries.count, rel, (unsigned long long)raw.size);
        free(out_path);
        vec_free(&raw);
    }
    fclose(fp);
    double elapsed = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    printf("unpack summary: files=%zu | output=%llu | pak=%llu | time=%.3fs\n",
           entries.count, (unsigned long long)logical, (unsigned long long)file_size_w(pak_path), elapsed);
    free(entries.items);
    manifest_list_free(&manifest);
    return 0;
}

static int read_command(const wchar_t* pak_path, const char* name, const wchar_t* output_path) {
    EntryList entries = { 0 };
    uint8_t target[CIALLO_HASH_SIZE];
    custom_hash_bytes(name, target);
    if (!load_index_w(pak_path, &entries, NULL)) {
        return 1;
    }
    const PakEntry* found = NULL;
    for (size_t i = 0; i < entries.count; ++i) {
        if (memcmp(entries.items[i].hash, target, CIALLO_HASH_SIZE) == 0) {
            found = &entries.items[i];
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "entry not found: %s\n", name);
        free(entries.items);
        return 1;
    }
    FILE* fp = _wfopen(pak_path, L"rb");
    if (!fp) {
        free(entries.items);
        return 1;
    }
    ByteVec raw = { 0 };
    int ok = read_entry_raw(fp, found, &raw);
    fclose(fp);
    if (ok) {
        ok = write_file_w(output_path, raw.data, raw.size);
        printf("exported: %llu bytes\n", (unsigned long long)raw.size);
    }
    vec_free(&raw);
    free(entries.items);
    return ok ? 0 : 1;
}

static int info_command(const wchar_t* pak_path) {
    EntryList entries = { 0 };
    uint32_t file_count = 0;
    if (!load_index_w(pak_path, &entries, &file_count)) {
        return 1;
    }
    uint64_t logical = 0;
    uint64_t stored = 0;
    for (size_t i = 0; i < entries.count; ++i) {
        logical += entries.items[i].orig_size;
        stored += entries.items[i].stored_size;
    }
    wprintf(L"file: %ls\n", pak_path);
    printf("magic: %s\n", CIALLO_MAGIC);
    printf("version: %u\n", CIALLO_VERSION);
    printf("file_count(header/index): %u/%zu\n", file_count, entries.count);
    printf("original_size: %llu\n", (unsigned long long)logical);
    printf("stored_size: %llu\n", (unsigned long long)stored);
    for (size_t i = 0; i < entries.count; ++i) {
        char hex[33];
        to_hex_upper(entries.items[i].hash, CIALLO_HASH_SIZE, hex);
        printf("%s\torig=%llu\tstored=%llu\toffset=%llu\tflags=0x%02X\n",
               hex,
               (unsigned long long)entries.items[i].orig_size,
               (unsigned long long)entries.items[i].stored_size,
               (unsigned long long)entries.items[i].offset,
               (unsigned)entries.items[i].flags);
    }
    free(entries.items);
    return 0;
}

static int write_text_w(const wchar_t* path, const char* text) {
    return write_file_w(path, (const uint8_t*)text, strlen(text));
}

static int compare_files_w(const wchar_t* a, const wchar_t* b) {
    ByteVec av = { 0 };
    ByteVec bv = { 0 };
    int ok = read_file_w(a, &av) && read_file_w(b, &bv) && av.size == bv.size && memcmp(av.data, bv.data, av.size) == 0;
    vec_free(&av);
    vec_free(&bv);
    return ok;
}

static int selftest_command(const wchar_t* workdir) {
    wchar_t* input = join_path_w(workdir, L"ciallopak_c_input");
    wchar_t* output = join_path_w(workdir, L"ciallopak_c_output");
    wchar_t* unpacked = join_path_w(workdir, L"ciallopak_c_unpacked");
    wchar_t* script_dir = join_path_w(input, L"script");
    wchar_t* asset_dir = join_path_w(input, L"assets");
    wchar_t* script_file = join_path_w(script_dir, L"start.ks");
    wchar_t* asset_file = join_path_w(asset_dir, L"blob.bin");
    wchar_t* dup_file = join_path_w(asset_dir, L"blob_copy.bin");
    wchar_t* pak = join_path_w(output, L"demo.cpk");
    wchar_t* manifest = join_path_w(output, L"demo_manifest.txt");

    remove_tree_contents_w(input);
    remove_tree_contents_w(output);
    remove_tree_contents_w(unpacked);
    create_dirs_w(script_dir);
    create_dirs_w(asset_dir);
    create_dirs_w(output);
    create_dirs_w(unpacked);
    write_text_w(script_file, "Ciallo test\njump label_01\n");
    ByteVec blob = { 0 };
    for (int i = 0; i < 8192; ++i) {
        uint8_t v = (uint8_t)(i & 0x3F);
        vec_append_u8(&blob, v);
    }
    write_file_w(asset_file, blob.data, blob.size);
    write_file_w(dup_file, blob.data, blob.size);
    vec_free(&blob);

    int rc = pack_command(input, pak, manifest, 1, "zlib");
    if (rc == 0) {
        rc = unpack_command(pak, manifest, unpacked, 1);
    }
    wchar_t* out_script = join_path_w(unpacked, L"script\\start.ks");
    wchar_t* out_asset = join_path_w(unpacked, L"assets\\blob.bin");
    wchar_t* out_dup = join_path_w(unpacked, L"assets\\blob_copy.bin");
    if (rc == 0 && !compare_files_w(script_file, out_script)) {
        fprintf(stderr, "selftest script mismatch\n");
        rc = 1;
    }
    if (rc == 0 && !compare_files_w(asset_file, out_asset)) {
        fprintf(stderr, "selftest asset mismatch\n");
        rc = 1;
    }
    if (rc == 0 && !compare_files_w(dup_file, out_dup)) {
        fprintf(stderr, "selftest duplicate mismatch\n");
        rc = 1;
    }
    printf("selftest: %s\n", rc == 0 ? "OK" : "FAILED");

    free(input);
    free(output);
    free(unpacked);
    free(script_dir);
    free(asset_dir);
    free(script_file);
    free(asset_file);
    free(dup_file);
    free(pak);
    free(manifest);
    free(out_script);
    free(out_asset);
    free(out_dup);
    return rc;
}

static const char* get_arg_value(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc - 1; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

static int has_flag(int argc, char** argv, const char* name) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void print_help(void) {
    puts("CialloPAK C tool");
    puts("usage:");
    puts("  CialloPAK_tool pack --input DIR --pak OUT.cpk --manifest OUT_manifest.txt [--dedup] [--compression auto|zlib|zstd|lzma|raw]");
    puts("  CialloPAK_tool unpack --pak IN.cpk [--manifest MANIFEST.txt] --output DIR");
    puts("  CialloPAK_tool read --pak IN.cpk --name path/in/pak --output OUT");
    puts("  CialloPAK_tool info --pak IN.cpk");
    puts("  CialloPAK_tool selftest [--workdir DIR]");
}

static int is_known_command(const char* s) {
    return strcmp(s, "pack") == 0 || strcmp(s, "unpack") == 0 || strcmp(s, "read") == 0 ||
           strcmp(s, "info") == 0 || strcmp(s, "selftest") == 0 || strcmp(s, "-h") == 0 || strcmp(s, "--help") == 0;
}

static int ends_with_i(const wchar_t* s, const wchar_t* suffix) {
    size_t sn = wcslen(s);
    size_t tn = wcslen(suffix);
    if (sn < tn) {
        return 0;
    }
    return _wcsicmp(s + sn - tn, suffix) == 0;
}

static int auto_drag_mode(const wchar_t* path) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return 2;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        wchar_t* pak = (wchar_t*)xmalloc((wcslen(path) + 5) * sizeof(wchar_t));
        wcscpy(pak, path);
        wcscat(pak, L".cpk");
        wchar_t* manifest = (wchar_t*)xmalloc((wcslen(path) + 20) * sizeof(wchar_t));
        wcscpy(manifest, path);
        wcscat(manifest, L"_manifest.txt");
        int rc = pack_command(path, pak, manifest, 1, "auto");
        free(pak);
        free(manifest);
        return rc;
    }
    if (ends_with_i(path, L".cpk")) {
        wchar_t* out = (wchar_t*)xmalloc((wcslen(path) + 16) * sizeof(wchar_t));
        wcscpy(out, path);
        wchar_t* dot = wcsrchr(out, L'.');
        if (dot) {
            *dot = L'\0';
        }
        wcscat(out, L"_unpacked");
        int rc = unpack_command(path, NULL, out, 1);
        free(out);
        return rc;
    }
    return 2;
}

int wmain(int argc, wchar_t** wargv) {
    set_console_utf8();
    char** argv = (char**)xmalloc((size_t)argc * sizeof(char*));
    for (int i = 0; i < argc; ++i) {
        argv[i] = wide_to_utf8(wargv[i]);
    }

    int rc = 0;
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help();
        rc = argc < 2 ? 2 : 0;
        goto done;
    }

    if (!is_known_command(argv[1])) {
        rc = auto_drag_mode(wargv[1]);
        goto done;
    }

    if (strcmp(argv[1], "pack") == 0) {
        const char* input = get_arg_value(argc, argv, "--input");
        const char* pak = get_arg_value(argc, argv, "--pak");
        const char* manifest = get_arg_value(argc, argv, "--manifest");
        const char* compression = get_arg_value(argc, argv, "--compression");
        int dedup = has_flag(argc, argv, "--dedup");
        if (!input || !pak || !manifest) {
            print_help();
            rc = 2;
            goto done;
        }
        wchar_t* input_w = utf8_to_wide(input);
        wchar_t* pak_w = utf8_to_wide(pak);
        wchar_t* manifest_w = utf8_to_wide(manifest);
        rc = pack_command(input_w, pak_w, manifest_w, dedup, compression ? compression : "auto");
        free(input_w);
        free(pak_w);
        free(manifest_w);
    } else if (strcmp(argv[1], "unpack") == 0) {
        const char* pak = get_arg_value(argc, argv, "--pak");
        const char* manifest = get_arg_value(argc, argv, "--manifest");
        const char* output = get_arg_value(argc, argv, "--output");
        if (!pak || !output) {
            print_help();
            rc = 2;
            goto done;
        }
        wchar_t* pak_w = utf8_to_wide(pak);
        wchar_t* manifest_w = manifest ? utf8_to_wide(manifest) : NULL;
        wchar_t* output_w = utf8_to_wide(output);
        rc = unpack_command(pak_w, manifest_w, output_w, 1);
        free(pak_w);
        free(manifest_w);
        free(output_w);
    } else if (strcmp(argv[1], "read") == 0) {
        const char* pak = get_arg_value(argc, argv, "--pak");
        const char* name = get_arg_value(argc, argv, "--name");
        const char* output = get_arg_value(argc, argv, "--output");
        if (!pak || !name || !output) {
            print_help();
            rc = 2;
            goto done;
        }
        wchar_t* pak_w = utf8_to_wide(pak);
        wchar_t* output_w = utf8_to_wide(output);
        rc = read_command(pak_w, name, output_w);
        free(pak_w);
        free(output_w);
    } else if (strcmp(argv[1], "info") == 0) {
        const char* pak = get_arg_value(argc, argv, "--pak");
        if (!pak) {
            print_help();
            rc = 2;
            goto done;
        }
        wchar_t* pak_w = utf8_to_wide(pak);
        rc = info_command(pak_w);
        free(pak_w);
    } else if (strcmp(argv[1], "selftest") == 0) {
        const char* workdir = get_arg_value(argc, argv, "--workdir");
        wchar_t* workdir_w = workdir ? utf8_to_wide(workdir) : xwcsdup(L".");
        create_dirs_w(workdir_w);
        rc = selftest_command(workdir_w);
        free(workdir_w);
    } else {
        print_help();
        rc = 2;
    }

done:
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
    return rc;
}
