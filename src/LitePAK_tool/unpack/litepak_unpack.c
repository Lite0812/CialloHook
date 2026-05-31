#include "litepak.h"
#include "../common/litepak_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "zstd.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

static uint32_t vfs_cff_mix32(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}

static int vfs_cff_opaque_true(uint32_t seed) {
    volatile uint32_t x = seed | 1u;
    return ((x * (x + 1u)) & 1u) == 0u;
}

static int vfs_cff_opaque_false(uint32_t seed) {
    volatile uint32_t x = seed | 1u;
    return (((x * x) + x + 1u) & 1u) == 0u;
}

static uint32_t vfs_cff_select_state(uint32_t seed, uint32_t real_state, uint32_t bogus_state) {
    volatile uint32_t guard = vfs_cff_mix32(seed ^ real_state ^ (bogus_state << 1));
    uint32_t selected = vfs_cff_opaque_true(guard) ? real_state : bogus_state;
    if (vfs_cff_opaque_false(guard ^ 0x1BADC0DEu))
        selected ^= vfs_cff_mix32(guard);
    return selected;
}

static uint32_t vfs_cff_state_mask(uint32_t key) {
    return vfs_cff_mix32(key ^ 0x9E2A83C1u) ^ 0x4D1B6A33u;
}

static uint32_t vfs_cff_encode_state(uint32_t state, uint32_t key) {
    return state ^ vfs_cff_state_mask(key);
}

static uint32_t vfs_cff_decode_state(uint32_t state, uint32_t key) {
    return state ^ vfs_cff_state_mask(key);
}

static uint32_t vfs_cff_launder_state(uint32_t state, uint32_t key) {
    volatile uint32_t encoded = vfs_cff_encode_state(state, key);
    uint32_t decoded = vfs_cff_decode_state(encoded, key);
    if (vfs_cff_opaque_false(key ^ decoded ^ 0xF00DBA5Eu))
        decoded ^= vfs_cff_state_mask(key ^ 0x2468ACE0u);
    return decoded;
}

#define VFS_CFF_NEXT(seed, real_state, bogus_state) \
    vfs_cff_select_state((uint32_t)(seed), (uint32_t)(real_state), (uint32_t)(bogus_state))
#define VFS_CFF_FAKE(seed, bogus_state, real_state) \
    (vfs_cff_opaque_false((uint32_t)(seed)) ? (uint32_t)(bogus_state) : (uint32_t)(real_state))
#define VFS_CFF_ENCODE(state, key) \
    vfs_cff_encode_state((uint32_t)(state), (uint32_t)(key))
#define VFS_CFF_DECODE(state, key) \
    vfs_cff_decode_state((uint32_t)(state), (uint32_t)(key))
#define VFS_CFF_DISPATCH(state, key) \
    vfs_cff_launder_state((uint32_t)(state), (uint32_t)(key))

typedef struct {
    char hash_hex[33];
    char* rel_path;
} manifest_entry_t;

typedef struct {
    manifest_entry_t* items;
    size_t count;
    size_t cap;
} manifest_map_t;

typedef struct {
    chunk_record_t rec;
    buffer_t plain;
    uint32_t hits;
    double decode_ms;
    uint64_t last_touch;
} chunk_cache_entry_t;

typedef struct {
    chunk_cache_entry_t* items;
    size_t count;
    size_t cap;
    size_t bytes;
    uint64_t seq;
#ifdef _WIN32
    CRITICAL_SECTION lock;
#endif
} chunk_plain_cache_t;

typedef struct {
    uint8_t share_a[32];
    uint8_t share_b[32];
    uint32_t canary;
    uint32_t checksum;
    int initialized;
} master_key_ctx_t;

typedef struct {
    uint32_t none_count;
    uint32_t arx_count;
    uint32_t feistel_count;
    uint32_t mixed_count;
    uint32_t file_chunk_count;
    uint32_t k9_chunk_count;
    uint64_t original_total;
    uint64_t stored_total;
} v6_info_stats_t;

typedef struct {
    int memory_backed;
    const char* pak_path;
    const uint8_t* mem_data;
    size_t mem_size;
} litepak_read_source_t;

struct litepak_vfs_handle {
    litepak_read_source_t source;
    char* pak_path;
    uint8_t* memory_copy;
    size_t memory_size;
    litepak_header_t header;
    buffer_t index_plain;
    index_meta_t meta;
    master_key_ctx_t pre_master_key;
    master_key_ctx_t full_master_key;
    chunk_plain_cache_t cache;
    int cache_initialized;
    uint8_t key_material_signature[32];
    manifest_map_t manifest;
    int has_manifest;
};

static int load_core(const char* pak_path, int strict, litepak_header_t* header,
                     buffer_t* index_plain, index_meta_t* meta,
                     master_key_ctx_t* pre_master_key, master_key_ctx_t* full_master_key,
                     chunk_plain_cache_t* cache, uint8_t key_material_signature[32]);
static int read_chunk_plain(const char* pak_path, const chunk_record_t* rec,
                            const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                            chunk_plain_cache_t* cache, buffer_t* out_plain);
static int decode_entry_bytes(const char* pak_path, const entry_t* entry,
                              const chunk_record_t* chunk_records, uint32_t chunk_record_count,
                              const uint32_t* chunk_refs, uint32_t chunk_ref_count,
                              const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                              chunk_plain_cache_t* cache, buffer_t* out_plain);
static int load_core_from_source(const litepak_read_source_t* source, int strict, litepak_header_t* header,
                                 buffer_t* index_plain, index_meta_t* meta,
                                 master_key_ctx_t* pre_master_key, master_key_ctx_t* full_master_key,
                                 chunk_plain_cache_t* cache, uint8_t key_material_signature[32]);
static int read_chunk_plain_from_source(const litepak_read_source_t* source, const chunk_record_t* rec,
                                        const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                                        chunk_plain_cache_t* cache, buffer_t* out_plain);
static int decode_entry_bytes_from_source(const litepak_read_source_t* source, const entry_t* entry,
                                          const chunk_record_t* chunk_records, uint32_t chunk_record_count,
                                          const uint32_t* chunk_refs, uint32_t chunk_ref_count,
                                          const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                                          chunk_plain_cache_t* cache, buffer_t* out_plain);

#ifdef _WIN32
void __cdecl litepak_asm_xor32(const uint8_t* share_a, const uint8_t* share_b, uint8_t* out);
uint32_t __cdecl litepak_asm_fingerprint_mix(const void* fn, size_t window, uint32_t seed);
#define LITEPAK_SECURE_CORE_MASM 1
#else
#define LITEPAK_SECURE_CORE_MASM 0
#endif

static void master_key_ctx_clear(master_key_ctx_t* ctx) {
    if (!ctx) return;
    litepak_secure_bzero(ctx, sizeof(*ctx));
}

static void master_key_ctx_store(master_key_ctx_t* ctx, const uint8_t key[32], const char* label, const litepak_header_t* header) {
    uint8_t input[2 + 16 + 16 + 32];
    uint8_t mask[32];
    size_t label_len = label ? strlen(label) : 0;
    memset(ctx, 0, sizeof(*ctx));
    memset(input, 0, sizeof(input));
    input[0] = (uint8_t)header->seed;
    input[1] = (uint8_t)(header->seed >> 8);
    memcpy(input + 2, header->k2, 16);
    memcpy(input + 18, header->k8, 16);
    if (label_len > 32) label_len = 32;
    if (label_len > 0) memcpy(input + 34, label, label_len);
    blake2b_full(input, sizeof(input), mask, 32, NULL, 0, (const uint8_t*)"LiteCtxV6", 9);
    ctx->canary = litepak_crc32c(input, sizeof(input), 0xC0110CA7u);
    for (int i = 0; i < 32; i++) {
        ctx->share_b[i] = (uint8_t)(mask[i] ^ (uint8_t)(ctx->canary >> ((i & 3) * 8)));
        ctx->share_a[i] = (uint8_t)(key[i] ^ ctx->share_b[i]);
    }
    ctx->checksum = litepak_crc32c(key, 32, ctx->canary);
    ctx->initialized = 1;
    litepak_secure_bzero(mask, sizeof(mask));
    litepak_secure_bzero(input, sizeof(input));
}

static int master_key_ctx_materialize(const master_key_ctx_t* ctx, uint8_t out[32]) {
    if (!ctx || !ctx->initialized)
        return -1;
#if defined(_WIN32) && LITEPAK_SECURE_CORE_MASM
    litepak_asm_xor32(ctx->share_a, ctx->share_b, out);
#else
    for (int i = 0; i < 32; i++)
        out[i] = (uint8_t)(ctx->share_a[i] ^ ctx->share_b[i]);
#endif
    if (litepak_crc32c(out, 32, ctx->canary) != ctx->checksum) {
        litepak_secure_bzero(out, 32);
        return -1;
    }
    return 0;
}

static uint32_t fingerprint_function_window(const void* fn, size_t window) {
    uint32_t seed = 0x1F06C0DEu;
#if defined(_WIN32) && LITEPAK_SECURE_CORE_MASM
    seed = litepak_asm_fingerprint_mix(fn, window, seed);
#endif
    return litepak_crc32c((const uint8_t*)fn, window, seed);
}

static int litepak_core_self_check_once(void) {
    static int initialized = 0;
    static uint32_t fp_load_core;
    static uint32_t fp_read_chunk;
    static uint32_t fp_decode_entry;
    static uint32_t fp_load_core_source;
    static uint32_t fp_read_chunk_source;
    static uint32_t fp_decode_entry_source;
    static uint32_t fp_segmented_decrypt;
    const size_t window = 96;
#ifdef _WIN32
    HMODULE module = NULL;
    char module_path[MAX_PATH];
    const char* base;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&litepak_core_self_check_once, &module)) {
        fprintf(stderr, "litepak.dll self-check failed: module lookup\n");
        return -1;
    }
    if (GetModuleFileNameA(module, module_path, sizeof(module_path)) == 0) {
        fprintf(stderr, "litepak.dll self-check failed: module path\n");
        return -1;
    }
    base = strrchr(module_path, '\\');
    if (!base) base = strrchr(module_path, '/');
    base = base ? base + 1 : module_path;
#ifdef LITEPAK_STATIC_RUNTIMECORE
    if (_stricmp(base, "CialloHook.dll") != 0 &&
        _stricmp(base, "version.dll") != 0 &&
        _stricmp(base, "winmm.dll") != 0 &&
        _stricmp(base, "litepak.dll") != 0) {
        fprintf(stderr, "litepak self-check failed: module name=%s\n", base);
        return -1;
    }
#else
    if (_stricmp(base, "litepak.dll") != 0) {
        fprintf(stderr, "litepak.dll self-check failed: module name=%s\n", base);
        return -1;
    }
#endif
#endif
    if (litepak_codecrypt_ensure_decrypted() != 0) {
        fprintf(stderr, "litepak.dll self-check failed: code decrypt\n");
        return -1;
    }
    if (!initialized) {
        fp_load_core = fingerprint_function_window((const void*)load_core, window);
        fp_read_chunk = fingerprint_function_window((const void*)read_chunk_plain, window);
        fp_decode_entry = fingerprint_function_window((const void*)decode_entry_bytes, window);
        fp_load_core_source = fingerprint_function_window((const void*)load_core_from_source, window);
        fp_read_chunk_source = fingerprint_function_window((const void*)read_chunk_plain_from_source, window);
        fp_decode_entry_source = fingerprint_function_window((const void*)decode_entry_bytes_from_source, window);
        fp_segmented_decrypt = fingerprint_function_window((const void*)litepak_segmented_decrypt, window);
        initialized = 1;
        return 0;
    }
    if (fp_load_core != fingerprint_function_window((const void*)load_core, window) ||
        fp_read_chunk != fingerprint_function_window((const void*)read_chunk_plain, window) ||
        fp_decode_entry != fingerprint_function_window((const void*)decode_entry_bytes, window) ||
        fp_load_core_source != fingerprint_function_window((const void*)load_core_from_source, window) ||
        fp_read_chunk_source != fingerprint_function_window((const void*)read_chunk_plain_from_source, window) ||
        fp_decode_entry_source != fingerprint_function_window((const void*)decode_entry_bytes_from_source, window) ||
        fp_segmented_decrypt != fingerprint_function_window((const void*)litepak_segmented_decrypt, window)) {
        fprintf(stderr, "litepak.dll self-check failed: runtime code fingerprint changed\n");
        return -1;
    }
    return 0;
}

static int litepak_core_self_check_fast(void) {
    return litepak_core_self_check_once();
}

static void manifest_map_init(manifest_map_t* map) {
    map->items = NULL;
    map->count = 0;
    map->cap = 0;
}

static void manifest_map_free(manifest_map_t* map) {
    if (!map) return;
    for (size_t i = 0; i < map->count; i++)
        free(map->items[i].rel_path);
    free(map->items);
    map->items = NULL;
    map->count = 0;
    map->cap = 0;
}

static int manifest_map_add(manifest_map_t* map, const char* hash_hex, const char* rel_path) {
    if (map->count >= map->cap) {
        size_t new_cap = map->cap ? map->cap * 2 : 64;
        manifest_entry_t* p = (manifest_entry_t*)realloc(map->items, new_cap * sizeof(manifest_entry_t));
        if (!p) return -1;
        map->items = p;
        map->cap = new_cap;
    }
    memset(&map->items[map->count], 0, sizeof(map->items[map->count]));
    strncpy(map->items[map->count].hash_hex, hash_hex, 32);
    map->items[map->count].hash_hex[32] = '\0';
    map->items[map->count].rel_path = _strdup(rel_path);
    if (!map->items[map->count].rel_path) return -1;
    map->count++;
    return 0;
}

static const char* manifest_map_find(const manifest_map_t* map, const char* hash_hex) {
    for (size_t i = 0; i < map->count; i++) {
        if (_stricmp(map->items[i].hash_hex, hash_hex) == 0)
            return map->items[i].rel_path;
    }
    return NULL;
}

static void chunk_cache_lock(chunk_plain_cache_t* cache) {
#ifdef _WIN32
    EnterCriticalSection(&cache->lock);
#else
    (void)cache;
#endif
}

static void chunk_cache_unlock(chunk_plain_cache_t* cache) {
#ifdef _WIN32
    LeaveCriticalSection(&cache->lock);
#else
    (void)cache;
#endif
}

static void chunk_cache_init(chunk_plain_cache_t* cache) {
    memset(cache, 0, sizeof(*cache));
#ifdef _WIN32
    InitializeCriticalSection(&cache->lock);
#endif
}

static void chunk_cache_free(chunk_plain_cache_t* cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->count; i++)
        buffer_free(&cache->items[i].plain);
    free(cache->items);
#ifdef _WIN32
    DeleteCriticalSection(&cache->lock);
#endif
    memset(cache, 0, sizeof(*cache));
}

static int chunk_cache_key_equal(const chunk_record_t* a, const chunk_record_t* b) {
    return a->original_size == b->original_size &&
           a->stored_size == b->stored_size &&
           a->data_offset == b->data_offset &&
           a->chunk_crc32c == b->chunk_crc32c &&
           a->chunk_kind == b->chunk_kind &&
           a->mode == b->mode &&
           a->transform_flags == b->transform_flags &&
           memcmp(a->file_id, b->file_id, LITEPAK_FILE_ID_SIZE) == 0 &&
           memcmp(a->chunk_hash, b->chunk_hash, LITEPAK_CHUNK_HASH_SIZE) == 0;
}

static int chunk_cache_copy_out(const buffer_t* src, buffer_t* out) {
    buffer_init(out);
    if (buffer_reserve(out, src->len) != 0)
        return -1;
    if (src->len > 0)
        memcpy(out->data, src->data, src->len);
    out->len = src->len;
    return out->failed ? -1 : 0;
}

static int chunk_cache_get(chunk_plain_cache_t* cache, const chunk_record_t* rec, buffer_t* out) {
    if (!cache) return 0;
    chunk_cache_lock(cache);
    for (size_t i = 0; i < cache->count; i++) {
        if (chunk_cache_key_equal(&cache->items[i].rec, rec)) {
            int ok;
            cache->items[i].hits++;
            cache->items[i].last_touch = ++cache->seq;
            ok = chunk_cache_copy_out(&cache->items[i].plain, out) == 0;
            chunk_cache_unlock(cache);
            return ok;
        }
    }
    chunk_cache_unlock(cache);
    return 0;
}

static double chunk_cache_score(const chunk_cache_entry_t* e) {
    double size = e->plain.len > 0 ? (double)e->plain.len : 1.0;
    return ((double)e->hits + 1.0) * (e->decode_ms + 0.01) / size;
}

static void chunk_cache_evict_one(chunk_plain_cache_t* cache) {
    size_t victim = 0;
    double victim_score = chunk_cache_score(&cache->items[0]);
    for (size_t i = 1; i < cache->count; i++) {
        double score = chunk_cache_score(&cache->items[i]);
        if (score < victim_score || (score == victim_score && cache->items[i].last_touch < cache->items[victim].last_touch)) {
            victim = i;
            victim_score = score;
        }
    }
    cache->bytes -= cache->items[victim].plain.len;
    buffer_free(&cache->items[victim].plain);
    if (victim + 1 < cache->count)
        memmove(&cache->items[victim], &cache->items[victim + 1], (cache->count - victim - 1) * sizeof(cache->items[0]));
    cache->count--;
}

static void chunk_cache_put(chunk_plain_cache_t* cache, const chunk_record_t* rec, const buffer_t* plain, double decode_ms) {
    if (!cache || plain->len > LITEPAK_CHUNK_CACHE_MAX_BYTES)
        return;
    chunk_cache_lock(cache);
    for (size_t i = 0; i < cache->count; i++) {
        if (chunk_cache_key_equal(&cache->items[i].rec, rec)) {
            cache->items[i].hits++;
            cache->items[i].decode_ms = decode_ms;
            cache->items[i].last_touch = ++cache->seq;
            chunk_cache_unlock(cache);
            return;
        }
    }
    while (cache->count > 0 && (cache->count >= LITEPAK_CHUNK_CACHE_MAX_ENTRIES || cache->bytes + plain->len > LITEPAK_CHUNK_CACHE_MAX_BYTES))
        chunk_cache_evict_one(cache);
    if (cache->count >= cache->cap) {
        size_t new_cap = cache->cap ? cache->cap * 2 : 64;
        chunk_cache_entry_t* p = (chunk_cache_entry_t*)realloc(cache->items, new_cap * sizeof(cache->items[0]));
        if (!p) {
            chunk_cache_unlock(cache);
            return;
        }
        cache->items = p;
        cache->cap = new_cap;
    }
    memset(&cache->items[cache->count], 0, sizeof(cache->items[cache->count]));
    cache->items[cache->count].rec = *rec;
    if (chunk_cache_copy_out(plain, &cache->items[cache->count].plain) == 0) {
        cache->items[cache->count].decode_ms = decode_ms;
        cache->items[cache->count].last_touch = ++cache->seq;
        cache->bytes += cache->items[cache->count].plain.len;
        cache->count++;
    } else {
        buffer_free(&cache->items[cache->count].plain);
    }
    chunk_cache_unlock(cache);
}

static void hash_bytes_to_hex_upper(const uint8_t* in16, char out33[33]) {
    static const char* HEX = "0123456789ABCDEF";
    for (int i = 0; i < 16; i++) {
        out33[i * 2] = HEX[(in16[i] >> 4) & 0xF];
        out33[i * 2 + 1] = HEX[in16[i] & 0xF];
    }
    out33[32] = '\0';
}

static int mkdir_one(const char* path) {
#ifdef _WIN32
    int r = litepak_mkdir_utf8(path);
#else
    int r = mkdir(path, 0755);
#endif
    if (r == 0 || errno == EEXIST)
        return 0;
    return -1;
}

static int mkdir_parent_dirs(const char* file_path) {
    char* tmp = _strdup(file_path);
    char* p;
    if (!tmp) return -1;
    for (p = tmp; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            if (tmp[0] != '\0' && mkdir_one(tmp) != 0) {
                *p = saved;
                free(tmp);
                return -1;
            }
            *p = saved;
        }
    }
    free(tmp);
    return 0;
}

static int is_safe_relative_path(const char* path) {
    const char* p = path;
    if (!path || !*path)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 0;
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':')
        return 0;
    while (*p) {
        const char* start = p;
        size_t len;
        while (*p && *p != '/' && *p != '\\')
            p++;
        len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.')
            return 0;
        if (*p)
            p++;
    }
    return 1;
}

static char* join_path2(const char* a, const char* b) {
    size_t la;
    size_t lb;
    int need_sep;
    char* out;
    if (!is_safe_relative_path(b))
        return NULL;
    la = strlen(a);
    lb = strlen(b);
    need_sep = (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\');
    out = (char*)malloc(la + lb + (need_sep ? 2 : 1));
    if (!out) return NULL;
    memcpy(out, a, la);
    if (need_sep) out[la++] = '/';
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static int load_verify_public_key(const char* verify_key_path, uint8_t out_public_key[32], const uint8_t** out_key) {
    FILE* fp;
    if (out_key)
        *out_key = NULL;
    if (!verify_key_path || !verify_key_path[0])
        return 0;
    fp = litepak_fopen_utf8(verify_key_path, "rb");
    if (!fp)
        return -1;
    if (fread(out_public_key, 1, 32, fp) != 32) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    if (out_key)
        *out_key = out_public_key;
    return 0;
}

static int load_manifest(const char* manifest_path, manifest_map_t* map) {
    FILE* fp;
    char line[8192];
    manifest_map_init(map);
    if (!manifest_path) return 0;
    fp = litepak_fopen_utf8(manifest_path, "rb");
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        char* parts[3] = {0};
        int n = 0;
        while (*p && (*p == '\r' || *p == '\n')) ++p;
        if (*p == '\0') continue;
        while (n < 3 && p) {
            char* tab = strchr(p, '\t');
            if (tab) *tab = '\0';
            parts[n++] = p;
            p = tab ? (tab + 1) : NULL;
        }
        if (n == 2) {
            char* end = parts[1] + strlen(parts[1]);
            while (end > parts[1] && (end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
            if (manifest_map_add(map, parts[0], parts[1]) != 0) {
                fclose(fp);
                manifest_map_free(map);
                return -1;
            }
        } else if (n >= 3) {
            char* end = parts[2] + strlen(parts[2]);
            while (end > parts[2] && (end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
            if (manifest_map_add(map, parts[0], parts[2]) != 0) {
                fclose(fp);
                manifest_map_free(map);
                return -1;
            }
        }
    }
    fclose(fp);
    return 0;
}

static int read_chunk_plain(const char* pak_path, const chunk_record_t* rec,
                            const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                            chunk_plain_cache_t* cache, buffer_t* out_plain) {
    FILE* fp;
    buffer_t enc;
    buffer_t packed;
    uint8_t master[32];
    int master_ready = 0;
#ifdef _WIN32
    ULONGLONG start_ms = GetTickCount64();
#else
    double start_ms = 0.0;
#endif

    if (chunk_cache_get(cache, rec, out_plain))
        return 0;

    buffer_init(&enc);
    buffer_init(&packed);
    buffer_init(out_plain);

    fp = litepak_fopen_utf8(pak_path, "rb");
    if (!fp) return -1;
    if (_fseeki64(fp, (long long)rec->data_offset, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    if (buffer_reserve(&enc, (size_t)rec->stored_size) != 0) {
        fclose(fp);
        buffer_free(&enc);
        return -1;
    }
    enc.len = (size_t)rec->stored_size;
    if (fread(enc.data, 1, enc.len, fp) != enc.len) {
        fclose(fp);
        buffer_free(&enc);
        return -1;
    }
    fclose(fp);

    {
        const master_key_ctx_t* master_ctx = (rec->chunk_kind == CHUNK_KIND_K9) ? pre_master_key : full_master_key;
        if (master_key_ctx_materialize(master_ctx, master) != 0) {
            fprintf(stderr, "chunk decode failed: offset=%llu stage=keyctx\n",
                    (unsigned long long)rec->data_offset);
            buffer_free(&enc);
            return -1;
        }
        master_ready = 1;
        if (litepak_segmented_decrypt(enc.data, enc.len, master, rec->data_nonce,
                                      rec->original_size, rec->chunk_kind,
                                      rec->chunk_kind == CHUNK_KIND_K9, &packed) != 0) {
            fprintf(stderr, "chunk decode failed: offset=%llu stored=%llu original=%llu kind=%u stage=decrypt\n",
                    (unsigned long long)rec->data_offset,
                    (unsigned long long)rec->stored_size,
                    (unsigned long long)rec->original_size,
                    (unsigned)rec->chunk_kind);
            litepak_secure_bzero(master, sizeof(master));
            buffer_free(&enc);
            return -1;
        }
    }

    if (litepak_decompress_chunk(packed.data, packed.len, (size_t)rec->original_size, out_plain) != 0) {
        fprintf(stderr, "chunk decode failed: offset=%llu stored=%llu original=%llu kind=%u stage=decompress packed_len=%zu mode=%u rec_mode=%u\n",
                (unsigned long long)rec->data_offset,
                (unsigned long long)rec->stored_size,
                (unsigned long long)rec->original_size,
                (unsigned)rec->chunk_kind,
                packed.len,
                packed.len > 0 ? (unsigned)packed.data[0] : 255U,
                (unsigned)rec->mode);
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        return -1;
    }
    if (out_plain->failed || out_plain->len != (size_t)rec->original_size) {
        fprintf(stderr, "chunk decode failed: offset=%llu stored=%llu original=%llu kind=%u stage=size\n",
                (unsigned long long)rec->data_offset,
                (unsigned long long)rec->stored_size,
                (unsigned long long)rec->original_size,
                (unsigned)rec->chunk_kind);
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        buffer_free(out_plain);
        return -1;
    }
    if (rec->transform_flags & CHUNK_TRANSFORM_FEISTEL)
        litepak_apply_feistel_transform(out_plain->data, out_plain->len, master, rec->file_id, rec->data_nonce, rec->original_size);
    if (rec->transform_flags & CHUNK_TRANSFORM_ARX)
        litepak_apply_arx_transform(out_plain->data, out_plain->len, master, rec->file_id, rec->data_nonce, rec->original_size);
    if (litepak_crc32c(out_plain->data, out_plain->len, 0) != rec->chunk_crc32c) {
        fprintf(stderr, "chunk decode failed: offset=%llu stored=%llu original=%llu kind=%u stage=crc\n",
                (unsigned long long)rec->data_offset,
                (unsigned long long)rec->stored_size,
                (unsigned long long)rec->original_size,
                (unsigned)rec->chunk_kind);
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        buffer_free(out_plain);
        return -1;
    }

    {
#ifdef _WIN32
        double decode_ms = (double)(GetTickCount64() - start_ms);
#else
        double decode_ms = start_ms;
#endif
        chunk_cache_put(cache, rec, out_plain, decode_ms);
    }
    if (master_ready) litepak_secure_bzero(master, sizeof(master));
    buffer_free(&enc);
    buffer_free(&packed);
    return 0;
}

static int decode_entry_bytes(const char* pak_path, const entry_t* entry,
                              const chunk_record_t* chunk_records, uint32_t chunk_record_count,
                              const uint32_t* chunk_refs, uint32_t chunk_ref_count,
                              const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                              chunk_plain_cache_t* cache, buffer_t* out_plain) {
    buffer_t chunk;
    buffer_init(out_plain);
    for (uint32_t i = 0; i < entry->chunk_count; i++) {
        uint32_t ref_pos = entry->chunk_ref_start + i;
        uint32_t rec_index;
        if (ref_pos >= chunk_ref_count) {
            fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=ref-range\n",
                    entry->chunk_ref_start, entry->chunk_count);
            buffer_free(out_plain);
            return -1;
        }
        rec_index = chunk_refs[ref_pos];
        if (rec_index >= chunk_record_count) {
            fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=chunk-index\n",
                    entry->chunk_ref_start, entry->chunk_count);
            buffer_free(out_plain);
            return -1;
        }
        if (read_chunk_plain(pak_path, &chunk_records[rec_index], pre_master_key, full_master_key,
                             cache, &chunk) != 0) {
            fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=read-chunk ref=%u rec=%u\n",
                    entry->chunk_ref_start, entry->chunk_count, ref_pos, rec_index);
            buffer_free(out_plain);
            return -1;
        }
        if (buffer_append(out_plain, chunk.data, chunk.len) != 0) {
            fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=append ref=%u rec=%u\n",
                    entry->chunk_ref_start, entry->chunk_count, ref_pos, rec_index);
            buffer_free(&chunk);
            buffer_free(out_plain);
            return -1;
        }
        buffer_free(&chunk);
    }
    if (out_plain->failed || out_plain->len != (size_t)entry->original_size) {
        fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=size\n",
                entry->chunk_ref_start, entry->chunk_count);
        buffer_free(out_plain);
        return -1;
    }
    if (litepak_crc32c(out_plain->data, out_plain->len, 0) != entry->file_crc32c) {
        fprintf(stderr, "entry decode failed: chunk_ref_start=%u chunk_count=%u stage=crc\n",
                entry->chunk_ref_start, entry->chunk_count);
        buffer_free(out_plain);
        return -1;
    }
    return 0;
}

static int load_core(const char* pak_path, int strict, litepak_header_t* header,
                     buffer_t* index_plain, index_meta_t* meta,
                     master_key_ctx_t* pre_master_key, master_key_ctx_t* full_master_key,
                     chunk_plain_cache_t* cache, uint8_t key_material_signature[32]) {
    FILE* fp = litepak_fopen_utf8(pak_path, "rb");
    buffer_t k9_bytes;
    uint8_t pre_raw[32];
    uint8_t full_raw[32];
    int rc = -1;
    buffer_init(&k9_bytes);
    memset(pre_raw, 0, sizeof(pre_raw));
    memset(full_raw, 0, sizeof(full_raw));
    master_key_ctx_clear(pre_master_key);
    master_key_ctx_clear(full_master_key);
    if (!fp) goto cleanup;
    if (litepak_read_header(fp, header) != 0)
        goto cleanup;
    litepak_derive_pre_master_key_ex(header->k2, header->k8, true, pre_raw);
    master_key_ctx_store(pre_master_key, pre_raw, "pre", header);
    if (litepak_decrypt_index(fp, header, pre_raw, index_plain) != 0)
        goto cleanup;
    litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    if (litepak_parse_index(index_plain->data, index_plain->len, strict != 0,
                            header->seed, header->v6_features, meta) != 0)
        goto cleanup;
    fclose(fp);
    fp = NULL;
    if (!meta->k9_entry) goto cleanup;
    if (decode_entry_bytes(pak_path, meta->k9_entry, meta->chunk_records, meta->chunk_record_count,
                           meta->chunk_refs, meta->chunk_ref_count,
                           pre_master_key, full_master_key,
                           cache, &k9_bytes) != 0)
        goto cleanup;
    if (k9_bytes.len < 16)
        goto cleanup;
    litepak_derive_full_master_key_ex(header->k2, meta->k6, header->k8, k9_bytes.data, meta->k10,
                                      true, full_raw);
    if (key_material_signature) {
        if (master_key_ctx_materialize(pre_master_key, pre_raw) != 0)
            goto cleanup;
        litepak_compute_key_material_signature(header->k2, meta->k6, header->k8, k9_bytes.data, meta->k10,
                                               header->v6_features, pre_raw, full_raw,
                                               key_material_signature);
        litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    }
    master_key_ctx_store(full_master_key, full_raw, "full", header);
    rc = 0;

cleanup:
    if (fp) fclose(fp);
    litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    litepak_secure_bzero(full_raw, sizeof(full_raw));
    if (k9_bytes.data)
        litepak_secure_bzero(k9_bytes.data, k9_bytes.len);
    buffer_free(&k9_bytes);
    return rc;
}



static uint16_t vfs_read_u16le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t vfs_read_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t vfs_read_u64le(const uint8_t* p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static int source_get_size(const litepak_read_source_t* source, uint64_t* out_size) {
    FILE* fp;
    long long pos;
    if (!source || !out_size)
        return -1;
    if (source->memory_backed) {
        *out_size = (uint64_t)source->mem_size;
        return 0;
    }
    fp = litepak_fopen_utf8(source->pak_path, "rb");
    if (!fp)
        return -1;
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    pos = _ftelli64(fp);
    fclose(fp);
    if (pos < 0)
        return -1;
    *out_size = (uint64_t)pos;
    return 0;
}

static int source_read_exact(const litepak_read_source_t* source, uint64_t offset, void* out, size_t len) {
    FILE* fp;
    if (!source || (!out && len != 0))
        return -1;
    if (source->memory_backed) {
        if (offset > (uint64_t)source->mem_size || len > source->mem_size - (size_t)offset)
            return -1;
        if (len > 0)
            memcpy(out, source->mem_data + (size_t)offset, len);
        return 0;
    }
    fp = litepak_fopen_utf8(source->pak_path, "rb");
    if (!fp)
        return -1;
    if (_fseeki64(fp, (long long)offset, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (len > 0 && fread(out, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int source_read_buffer(const litepak_read_source_t* source, uint64_t offset, uint64_t len64, buffer_t* out) {
    if (!out || len64 > (uint64_t)SIZE_MAX)
        return -1;
    buffer_init(out);
    if (buffer_reserve(out, (size_t)len64) != 0)
        return -1;
    out->len = (size_t)len64;
    if (source_read_exact(source, offset, out->data, out->len) != 0) {
        buffer_free(out);
        return -1;
    }
    return 0;
}

static int decompress_zstd_payload_vfs(const uint8_t* data, size_t len, size_t expected_plain, buffer_t* out) {
    size_t alloc_sz = expected_plain;
    if (alloc_sz == 0 || alloc_sz == (size_t)ZSTD_CONTENTSIZE_UNKNOWN || alloc_sz == (size_t)ZSTD_CONTENTSIZE_ERROR) {
        unsigned long long frame_size = ZSTD_getFrameContentSize(data, len);
        if (frame_size != ZSTD_CONTENTSIZE_UNKNOWN && frame_size != ZSTD_CONTENTSIZE_ERROR)
            alloc_sz = (size_t)frame_size;
        else
            alloc_sz = len ? len * 4 : 256;
    }
    if (alloc_sz < 256)
        alloc_sz = 256;
    for (int i = 0; i < 6; i++) {
        if (buffer_reserve(out, alloc_sz) != 0)
            return -1;
        size_t ret = ZSTD_decompress(out->data, alloc_sz, data, len);
        if (!ZSTD_isError(ret)) {
            out->len = ret;
            return 0;
        }
        alloc_sz *= 2;
    }
    return -1;
}

LITEPAK_PROTECTED_BEGIN
static LITEPAK_PROTECTED_NOINLINE int read_header_from_source_impl_protected(const litepak_read_source_t* source, litepak_header_t* header) {
    uint8_t raw[LITEPAK_HEADER_SIZE];
    if (source_read_exact(source, 0, raw, sizeof(raw)) != 0)
        return -1;
    if (memcmp(raw, LITEPAK_MAGIC, LITEPAK_MAGIC_LEN) != 0)
        return -1;
    memset(header, 0, sizeof(*header));
    header->version = raw[7];
    if (header->version != LITEPAK_VERSION)
        return -1;
    header->flags = vfs_read_u16le(raw + 8);
    {
        const uint16_t required_flags = FLAG_HAS_TRAILER | FLAG_CHUNK_INDEX | FLAG_FULL_VERIFY |
                                        FLAG_AES_GCM | FLAG_ED25519_SIGNED |
                                        FLAG_INDEX_OBFUSCATED | FLAG_WB_STRONG;
        if ((header->flags & required_flags) != required_flags)
            return -1;
    }
    header->file_count = vfs_read_u32le(raw + 10);
    header->index_offset = vfs_read_u64le(raw + 14);
    header->index_encrypted_sz = vfs_read_u64le(raw + 22);
    header->index_plain_sz = vfs_read_u64le(raw + 30);
    memcpy(header->index_nonce, raw + 38, LITEPAK_NONCE_SIZE);
    if (litepak_crc32c(raw, 50, 0) != vfs_read_u32le(raw + 50))
        return -1;
    memcpy(header->k2, raw + 54, 16);
    memcpy(header->k8, raw + 70, 16);
    header->seed = vfs_read_u16le(raw + 86);
    header->v6_features = vfs_read_u32le(raw + 88);
    if ((header->v6_features & LITEPAK_V6_FEATURES) != LITEPAK_V6_FEATURES)
        return -1;
    if (vfs_read_u32le(raw + 92) != LITEPAK_HEADER_SIZE)
        return -1;
    header->has_trailer = (header->flags & FLAG_HAS_TRAILER) != 0;
    header->index_compressed = (header->flags & FLAG_INDEX_COMPRESSED) != 0;
    header->chunk_index = (header->flags & FLAG_CHUNK_INDEX) != 0;
    header->aes_gcm = (header->flags & FLAG_AES_GCM) != 0;
    header->ed25519_signed = (header->flags & FLAG_ED25519_SIGNED) != 0;
    header->index_obfuscated = (header->flags & FLAG_INDEX_OBFUSCATED) != 0;
    header->strong_wb = (header->flags & FLAG_WB_STRONG) != 0;
    return 0;
}
LITEPAK_PROTECTED_END

static int read_header_from_source(const litepak_read_source_t* source, litepak_header_t* header) {
    if (litepak_codecrypt_ensure_decrypted() != 0)
        return -1;
    return read_header_from_source_impl_protected(source, header);
}

LITEPAK_PROTECTED_BEGIN
static LITEPAK_PROTECTED_NOINLINE int decrypt_index_from_source_impl_protected(const litepak_read_source_t* source, const litepak_header_t* header,
                                                       const uint8_t pre_master_key[32], buffer_t* out_plain) {
    enum {
        SD_READ = 0x6100u,
        SD_DERIVE = 0x6101u,
        SD_ALLOC_PAYLOAD = 0x6102u,
        SD_DECRYPT = 0x6103u,
        SD_ROUTE_PAYLOAD = 0x6104u,
        SD_DECOMPRESS = 0x6105u,
        SD_COPY_PAYLOAD = 0x6106u,
        SD_SIZE_CHECK = 0x6107u,
        SD_ROUTE_DEOBF = 0x6108u,
        SD_DEOBF_ALLOC = 0x6109u,
        SD_DEOBF_APPLY = 0x610Au,
        SD_DONE = 0x610Bu,
        SD_FAIL = 0x610Cu,
        SD_BOGUS_A = 0x6F01u,
        SD_BOGUS_B = 0x6F02u
    };
    buffer_t enc;
    buffer_t payload;
    uint8_t idx_key[32];
    uint8_t* tmp = NULL;
    size_t tmp_len = 0;
    int rc = -1;
    uint32_t after_bogus = SD_FAIL;
    uint32_t state_key = vfs_cff_mix32((uint32_t)header->seed ^ header->v6_features ^ (uint32_t)source->mem_size ^ 0x5DDEC0DEu);
    volatile uint32_t state = SD_READ;
    volatile uint32_t cff_noise = (uint32_t)header->seed ^ header->v6_features ^ (uint32_t)source->mem_size;
    uint32_t cff_shadow = (uint32_t)header->index_encrypted_sz ^ (uint32_t)header->index_plain_sz;

    buffer_init(&enc);
    buffer_init(&payload);
    buffer_init(out_plain);
    memset(idx_key, 0, sizeof(idx_key));

    for (;;) {
        switch (VFS_CFF_DISPATCH(state, state_key)) {
        case SD_READ:
            if (source_read_buffer(source, header->index_offset, header->index_encrypted_sz, &enc) != 0) {
                state = SD_FAIL;
                break;
            }
            after_bogus = SD_DERIVE;
            state = VFS_CFF_NEXT(cff_noise ^ enc.len, SD_DERIVE, SD_BOGUS_A);
            break;

        case SD_DERIVE:
            litepak_derive_index_key(pre_master_key, header->index_plain_sz, idx_key);
            if (enc.len < LITEPAK_GCM_TAG_SIZE) {
                state = SD_FAIL;
                break;
            }
            state = SD_ALLOC_PAYLOAD;
            break;

        case SD_ALLOC_PAYLOAD:
            if (buffer_reserve(&payload, enc.len - LITEPAK_GCM_TAG_SIZE) != 0) {
                state = SD_FAIL;
                break;
            }
            payload.len = enc.len - LITEPAK_GCM_TAG_SIZE;
            after_bogus = SD_DECRYPT;
            state = VFS_CFF_NEXT(cff_noise ^ payload.len, SD_DECRYPT, SD_BOGUS_B);
            break;

        case SD_DECRYPT:
            if (aes_gcm_decrypt(enc.data, enc.len, idx_key, header->index_nonce, payload.data) != 0) {
                state = SD_FAIL;
                break;
            }
            state = SD_ROUTE_PAYLOAD;
            break;

        case SD_ROUTE_PAYLOAD:
            state = header->index_compressed ? SD_DECOMPRESS : SD_COPY_PAYLOAD;
            break;

        case SD_DECOMPRESS:
            if (decompress_zstd_payload_vfs(payload.data, payload.len, (size_t)header->index_plain_sz, out_plain) != 0) {
                state = SD_FAIL;
                break;
            }
            after_bogus = SD_SIZE_CHECK;
            state = VFS_CFF_NEXT(cff_noise ^ out_plain->len, SD_SIZE_CHECK, SD_BOGUS_A);
            break;

        case SD_COPY_PAYLOAD:
            if (buffer_append(out_plain, payload.data, payload.len) != 0) {
                state = SD_FAIL;
                break;
            }
            after_bogus = SD_SIZE_CHECK;
            state = VFS_CFF_FAKE(cff_noise ^ payload.len, SD_BOGUS_B, SD_SIZE_CHECK);
            break;

        case SD_SIZE_CHECK:
            if (out_plain->len != (size_t)header->index_plain_sz) {
                state = SD_FAIL;
                break;
            }
            after_bogus = SD_ROUTE_DEOBF;
            state = VFS_CFF_NEXT(cff_noise ^ out_plain->len ^ 0x510u, SD_ROUTE_DEOBF, SD_BOGUS_B);
            break;

        case SD_ROUTE_DEOBF:
            state = header->index_obfuscated ? SD_DEOBF_ALLOC : SD_DONE;
            break;

        case SD_DEOBF_ALLOC:
            tmp_len = out_plain->len;
            tmp = (uint8_t*)malloc(tmp_len);
            if (!tmp) {
                state = SD_FAIL;
                break;
            }
            state = SD_DEOBF_APPLY;
            break;

        case SD_DEOBF_APPLY:
            litepak_deobfuscate_index(out_plain->data, out_plain->len, header->seed, pre_master_key, tmp);
            memcpy(out_plain->data, tmp, out_plain->len);
            state = SD_DONE;
            break;

        case SD_BOGUS_A:
            cff_shadow ^= vfs_cff_mix32((uint32_t)cff_noise ^ (uint32_t)enc.len ^ 0xA17E5AA5u);
            cff_noise = vfs_cff_mix32(cff_shadow ^ (uint32_t)header->index_offset);
            state = after_bogus;
            break;

        case SD_BOGUS_B:
            cff_shadow += vfs_cff_mix32((uint32_t)payload.len ^ (uint32_t)source->memory_backed ^ 0x6D2B79F5u);
            cff_noise ^= vfs_cff_mix32(cff_shadow ^ header->v6_features);
            state = after_bogus;
            break;

        case SD_DONE:
            rc = 0;
            goto cleanup;

        case SD_FAIL:
            rc = -1;
            goto cleanup;

        default:
            rc = -1;
            goto cleanup;
        }
    }

cleanup:
    if (tmp) {
        litepak_secure_bzero(tmp, tmp_len);
        free(tmp);
    }
    litepak_secure_bzero(idx_key, sizeof(idx_key));
    buffer_free(&enc);
    buffer_free(&payload);
    if (rc != 0)
        buffer_free(out_plain);
    return rc;
}
LITEPAK_PROTECTED_END

static int decrypt_index_from_source(const litepak_read_source_t* source, const litepak_header_t* header,
                                     const uint8_t pre_master_key[32], buffer_t* out_plain) {
    if (litepak_codecrypt_ensure_decrypted() != 0)
        return -1;
    return decrypt_index_from_source_impl_protected(source, header, pre_master_key, out_plain);
}

static int read_trailer_from_source(const litepak_read_source_t* source, const uint8_t* index_plain, size_t index_plain_len,
                                    const litepak_header_t* header, const uint8_t* verify_public_key,
                                    const uint8_t expected_key_material_signature[32]) {
    uint8_t raw[LITEPAK_TRAILER_SIZE];
    uint8_t hash_buf[32];
    uint8_t expected8[8];
    uint8_t index_plain_hash[32];
    uint8_t header_bytes[50];
    uint8_t header_chain_input[82];
    buffer_t data_area;
    uint64_t archive_size;
    buffer_init(&data_area);
    if (source_get_size(source, &archive_size) != 0 || archive_size < LITEPAK_TRAILER_SIZE)
        return 0;
    if (source_read_exact(source, archive_size - LITEPAK_TRAILER_SIZE, raw, sizeof(raw)) != 0)
        return 0;
    if (memcmp(raw + 72, "LiteTRLR", 8) != 0)
        return 0;
    if (vfs_read_u64le(raw + 80) != LITEPAK_TRAILER_SIZE)
        return 0;
    if (expected_key_material_signature && memcmp(raw + 88, expected_key_material_signature, 32) != 0)
        return 0;
    blake2b_full(raw, 120, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteTRv6", 8);
    if (memcmp(hash_buf, raw + 120, 8) != 0)
        return 0;
    blake2b_full(index_plain, index_plain_len, index_plain_hash, 32, NULL, 0, (const uint8_t*)"LiteIHv6", 8);
    if (memcmp(index_plain_hash, raw + 0, 32) != 0)
        return 0;
    if (header->index_offset > LITEPAK_HEADER_SIZE) {
        uint64_t area_size64 = header->index_offset - LITEPAK_HEADER_SIZE;
        if (source_read_buffer(source, LITEPAK_HEADER_SIZE, area_size64, &data_area) != 0)
            return 0;
        blake2b_full(data_area.data, data_area.len, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    } else {
        blake2b_full("", 0, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    }
    if (memcmp(hash_buf, raw + 32, 32) != 0) {
        buffer_free(&data_area);
        return 0;
    }
    if (source_read_exact(source, 0, header_bytes, sizeof(header_bytes)) != 0) {
        buffer_free(&data_area);
        return 0;
    }
    memcpy(header_chain_input, header_bytes, 50);
    memcpy(header_chain_input + 50, index_plain_hash, 32);
    blake2b_full(header_chain_input, sizeof(header_chain_input), hash_buf, 32, NULL, 0, (const uint8_t*)"LiteHCv6", 8);
    memcpy(expected8, hash_buf, 8);
    if (memcmp(expected8, raw + 64, 8) != 0) {
        buffer_free(&data_area);
        return 0;
    }
    if (header->ed25519_signed && !litepak_verify_trailer_sig(raw, 128, raw + 128, verify_public_key)) {
        buffer_free(&data_area);
        return 0;
    }
    buffer_free(&data_area);
    return 1;
}

static int read_chunk_plain_from_source(const litepak_read_source_t* source, const chunk_record_t* rec,
                                        const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                                        chunk_plain_cache_t* cache, buffer_t* out_plain) {
    buffer_t enc;
    buffer_t packed;
    uint8_t master[32];
    int master_ready = 0;
#ifdef _WIN32
    ULONGLONG start_ms = GetTickCount64();
#else
    double start_ms = 0.0;
#endif
    if (chunk_cache_get(cache, rec, out_plain))
        return 0;
    buffer_init(&enc);
    buffer_init(&packed);
    buffer_init(out_plain);
    if (source_read_buffer(source, rec->data_offset, rec->stored_size, &enc) != 0)
        return -1;
    {
        const master_key_ctx_t* master_ctx = (rec->chunk_kind == CHUNK_KIND_K9) ? pre_master_key : full_master_key;
        if (master_key_ctx_materialize(master_ctx, master) != 0) {
            buffer_free(&enc);
            return -1;
        }
        master_ready = 1;
        if (litepak_segmented_decrypt(enc.data, enc.len, master, rec->data_nonce,
                                      rec->original_size, rec->chunk_kind,
                                      rec->chunk_kind == CHUNK_KIND_K9, &packed) != 0) {
            litepak_secure_bzero(master, sizeof(master));
            buffer_free(&enc);
            return -1;
        }
    }
    if (litepak_decompress_chunk(packed.data, packed.len, (size_t)rec->original_size, out_plain) != 0) {
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        return -1;
    }
    if (out_plain->failed || out_plain->len != (size_t)rec->original_size) {
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        buffer_free(out_plain);
        return -1;
    }
    if (rec->transform_flags & CHUNK_TRANSFORM_FEISTEL)
        litepak_apply_feistel_transform(out_plain->data, out_plain->len, master, rec->file_id, rec->data_nonce, rec->original_size);
    if (rec->transform_flags & CHUNK_TRANSFORM_ARX)
        litepak_apply_arx_transform(out_plain->data, out_plain->len, master, rec->file_id, rec->data_nonce, rec->original_size);
    if (litepak_crc32c(out_plain->data, out_plain->len, 0) != rec->chunk_crc32c) {
        if (master_ready) litepak_secure_bzero(master, sizeof(master));
        buffer_free(&enc);
        buffer_free(&packed);
        buffer_free(out_plain);
        return -1;
    }
    {
#ifdef _WIN32
        double decode_ms = (double)(GetTickCount64() - start_ms);
#else
        double decode_ms = start_ms;
#endif
        chunk_cache_put(cache, rec, out_plain, decode_ms);
    }
    if (master_ready) litepak_secure_bzero(master, sizeof(master));
    buffer_free(&enc);
    buffer_free(&packed);
    return 0;
}

static int decode_entry_bytes_from_source(const litepak_read_source_t* source, const entry_t* entry,
                                          const chunk_record_t* chunk_records, uint32_t chunk_record_count,
                                          const uint32_t* chunk_refs, uint32_t chunk_ref_count,
                                          const master_key_ctx_t* pre_master_key, const master_key_ctx_t* full_master_key,
                                          chunk_plain_cache_t* cache, buffer_t* out_plain) {
    buffer_t chunk;
    buffer_init(out_plain);
    for (uint32_t i = 0; i < entry->chunk_count; i++) {
        uint32_t ref_pos = entry->chunk_ref_start + i;
        uint32_t rec_index;
        if (ref_pos >= chunk_ref_count) {
            buffer_free(out_plain);
            return -1;
        }
        rec_index = chunk_refs[ref_pos];
        if (rec_index >= chunk_record_count) {
            buffer_free(out_plain);
            return -1;
        }
        if (read_chunk_plain_from_source(source, &chunk_records[rec_index], pre_master_key, full_master_key,
                                         cache, &chunk) != 0) {
            buffer_free(out_plain);
            return -1;
        }
        if (buffer_append(out_plain, chunk.data, chunk.len) != 0) {
            buffer_free(&chunk);
            buffer_free(out_plain);
            return -1;
        }
        buffer_free(&chunk);
    }
    if (out_plain->failed || out_plain->len != (size_t)entry->original_size) {
        buffer_free(out_plain);
        return -1;
    }
    if (litepak_crc32c(out_plain->data, out_plain->len, 0) != entry->file_crc32c) {
        buffer_free(out_plain);
        return -1;
    }
    return 0;
}

static int load_core_from_source(const litepak_read_source_t* source, int strict, litepak_header_t* header,
                                 buffer_t* index_plain, index_meta_t* meta,
                                 master_key_ctx_t* pre_master_key, master_key_ctx_t* full_master_key,
                                 chunk_plain_cache_t* cache, uint8_t key_material_signature[32]) {
    buffer_t k9_bytes;
    uint8_t pre_raw[32];
    uint8_t full_raw[32];
    int rc = -1;
    buffer_init(&k9_bytes);
    memset(pre_raw, 0, sizeof(pre_raw));
    memset(full_raw, 0, sizeof(full_raw));
    master_key_ctx_clear(pre_master_key);
    master_key_ctx_clear(full_master_key);
    if (read_header_from_source(source, header) != 0)
        goto cleanup;
    litepak_derive_pre_master_key_ex(header->k2, header->k8, true, pre_raw);
    master_key_ctx_store(pre_master_key, pre_raw, "pre", header);
    if (decrypt_index_from_source(source, header, pre_raw, index_plain) != 0)
        goto cleanup;
    litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    if (litepak_parse_index(index_plain->data, index_plain->len, strict != 0,
                            header->seed, header->v6_features, meta) != 0)
        goto cleanup;
    if (!meta->k9_entry)
        goto cleanup;
    if (decode_entry_bytes_from_source(source, meta->k9_entry, meta->chunk_records, meta->chunk_record_count,
                                       meta->chunk_refs, meta->chunk_ref_count,
                                       pre_master_key, full_master_key,
                                       cache, &k9_bytes) != 0)
        goto cleanup;
    if (k9_bytes.len < 16)
        goto cleanup;
    litepak_derive_full_master_key_ex(header->k2, meta->k6, header->k8, k9_bytes.data, meta->k10,
                                      true, full_raw);
    if (key_material_signature) {
        if (master_key_ctx_materialize(pre_master_key, pre_raw) != 0)
            goto cleanup;
        litepak_compute_key_material_signature(header->k2, meta->k6, header->k8, k9_bytes.data, meta->k10,
                                               header->v6_features, pre_raw, full_raw,
                                               key_material_signature);
        litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    }
    master_key_ctx_store(full_master_key, full_raw, "full", header);
    rc = 0;
cleanup:
    litepak_secure_bzero(pre_raw, sizeof(pre_raw));
    litepak_secure_bzero(full_raw, sizeof(full_raw));
    if (k9_bytes.data)
        litepak_secure_bzero(k9_bytes.data, k9_bytes.len);
    buffer_free(&k9_bytes);
    return rc;
}

static const entry_t* find_entry_by_hash_vfs(const index_meta_t* meta, const uint8_t hash[LITEPAK_PATH_HASH_SIZE]) {
    if (!meta || !hash)
        return NULL;
    for (uint32_t i = 0; i < meta->entry_count; i++) {
        const entry_t* e = &meta->entries[i];
        if (e->flags == ENTRY_KEY_PAYLOAD)
            continue;
        if (memcmp(e->hash_bytes, hash, LITEPAK_PATH_HASH_SIZE) == 0)
            return e;
    }
    return NULL;
}

static int litepak_vfs_init_handle(litepak_vfs_handle_t* handle, const char* manifest_path) {
    if (!handle)
        return -1;
    buffer_init(&handle->index_plain);
    memset(&handle->meta, 0, sizeof(handle->meta));
    memset(&handle->pre_master_key, 0, sizeof(handle->pre_master_key));
    memset(&handle->full_master_key, 0, sizeof(handle->full_master_key));
    manifest_map_init(&handle->manifest);
    chunk_cache_init(&handle->cache);
    handle->cache_initialized = 1;
    if (litepak_core_self_check_fast() != 0)
        return -1;
    if (load_core_from_source(&handle->source, 1, &handle->header, &handle->index_plain, &handle->meta,
                              &handle->pre_master_key, &handle->full_master_key, &handle->cache,
                              handle->key_material_signature) != 0)
        return -1;
    if (!read_trailer_from_source(&handle->source, handle->index_plain.data, handle->index_plain.len,
                                  &handle->header, NULL, handle->key_material_signature))
        return -1;
    if (manifest_path && manifest_path[0] != '\0' && load_manifest(manifest_path, &handle->manifest) == 0)
        handle->has_manifest = handle->manifest.count > 0;
    return 0;
}

int litepak_vfs_open_path(const char* pak_path, const char* manifest_path,
                          litepak_vfs_handle_t** out_handle) {
    litepak_vfs_handle_t* h;
    if (!pak_path || !out_handle)
        return -1;
    *out_handle = NULL;
    h = (litepak_vfs_handle_t*)calloc(1, sizeof(*h));
    if (!h)
        return -1;
    h->pak_path = _strdup(pak_path);
    if (!h->pak_path) {
        free(h);
        return -1;
    }
    h->source.memory_backed = 0;
    h->source.pak_path = h->pak_path;
    if (litepak_vfs_init_handle(h, manifest_path) != 0) {
        litepak_vfs_close(h);
        return -1;
    }
    *out_handle = h;
    return 0;
}

int litepak_vfs_open_memory(const void* data, size_t size, const char* archive_tag,
                            const char* manifest_path, litepak_vfs_handle_t** out_handle) {
    litepak_vfs_handle_t* h;
    if (!data || size == 0 || !out_handle)
        return -1;
    *out_handle = NULL;
    h = (litepak_vfs_handle_t*)calloc(1, sizeof(*h));
    if (!h)
        return -1;
    h->memory_copy = (uint8_t*)malloc(size);
    if (!h->memory_copy) {
        free(h);
        return -1;
    }
    memcpy(h->memory_copy, data, size);
    h->memory_size = size;
    h->pak_path = _strdup(archive_tag && archive_tag[0] ? archive_tag : "<memory.lpk>");
    if (!h->pak_path) {
        litepak_vfs_close(h);
        return -1;
    }
    h->source.memory_backed = 1;
    h->source.pak_path = h->pak_path;
    h->source.mem_data = h->memory_copy;
    h->source.mem_size = h->memory_size;
    if (litepak_vfs_init_handle(h, manifest_path) != 0) {
        litepak_vfs_close(h);
        return -1;
    }
    *out_handle = h;
    return 0;
}

void litepak_vfs_close(litepak_vfs_handle_t* handle) {
    if (!handle)
        return;
    master_key_ctx_clear(&handle->pre_master_key);
    master_key_ctx_clear(&handle->full_master_key);
    litepak_secure_bzero(handle->key_material_signature, sizeof(handle->key_material_signature));
    if (handle->cache_initialized)
        if (handle->cache_initialized)
        chunk_cache_free(&handle->cache);
    manifest_map_free(&handle->manifest);
    litepak_free_index_meta(&handle->meta);
    buffer_free(&handle->index_plain);
    if (handle->memory_copy) {
        litepak_secure_bzero(handle->memory_copy, handle->memory_size);
        free(handle->memory_copy);
    }
    free(handle->pak_path);
    free(handle);
}

int litepak_vfs_get_entry_count(litepak_vfs_handle_t* handle, size_t* out_count) {
    if (!handle || !out_count)
        return -1;
    *out_count = (size_t)handle->meta.entry_count;
    return 0;
}

int litepak_vfs_get_entry(litepak_vfs_handle_t* handle, size_t index,
                          litepak_vfs_entry_info_t* out_info) {
    const entry_t* e;
    char hash_hex[33];
    if (!handle || !out_info || index >= handle->meta.entry_count)
        return -1;
    e = &handle->meta.entries[index];
    memset(out_info, 0, sizeof(*out_info));
    memcpy(out_info->hash_bytes, e->hash_bytes, LITEPAK_PATH_HASH_SIZE);
    out_info->flags = e->flags;
    out_info->original_size = e->original_size;
    if (handle->has_manifest) {
        hash_bytes_to_hex_upper(e->hash_bytes, hash_hex);
        out_info->rel_path = manifest_map_find(&handle->manifest, hash_hex);
    }
    return 0;
}

int litepak_vfs_read_file_by_hash(litepak_vfs_handle_t* handle,
                                  const uint8_t hash[LITEPAK_PATH_HASH_SIZE],
                                  uint8_t** out_data, size_t* out_size) {
    const entry_t* e;
    buffer_t plain;
    if (!handle || !hash || !out_data || !out_size)
        return -1;
    *out_data = NULL;
    *out_size = 0;
    e = find_entry_by_hash_vfs(&handle->meta, hash);
    if (!e)
        return 1;
    if (decode_entry_bytes_from_source(&handle->source, e, handle->meta.chunk_records, handle->meta.chunk_record_count,
                                       handle->meta.chunk_refs, handle->meta.chunk_ref_count,
                                       &handle->pre_master_key, &handle->full_master_key,
                                       &handle->cache, &plain) != 0)
        return -1;
    if (plain.len > 0) {
        uint8_t* copy = (uint8_t*)malloc(plain.len);
        if (!copy) {
            buffer_free(&plain);
            return -1;
        }
        memcpy(copy, plain.data, plain.len);
        *out_data = copy;
    }
    *out_size = plain.len;
    buffer_free(&plain);
    return 0;
}

int litepak_vfs_query_file_by_hash(litepak_vfs_handle_t* handle,
                                   const uint8_t hash[LITEPAK_PATH_HASH_SIZE],
                                   uint64_t* out_size) {
    const entry_t* e;
    if (!handle || !hash || !out_size)
        return -1;
    e = find_entry_by_hash_vfs(&handle->meta, hash);
    if (!e)
        return 1;
    *out_size = e->original_size;
    return 0;
}

void litepak_vfs_free_bytes(uint8_t* data) {
    free(data);
}

static void collect_v6_info_stats(const index_meta_t* meta, v6_info_stats_t* stats) {
    memset(stats, 0, sizeof(*stats));
    for (uint32_t i = 0; i < meta->chunk_record_count; i++) {
        const chunk_record_t* rec = &meta->chunk_records[i];
        uint8_t tf = rec->transform_flags;
        stats->original_total += rec->original_size;
        stats->stored_total += rec->stored_size;
        if (rec->chunk_kind == CHUNK_KIND_K9)
            stats->k9_chunk_count++;
        else if (rec->chunk_kind == CHUNK_KIND_FILE)
            stats->file_chunk_count++;
        if (tf == CHUNK_TRANSFORM_NONE)
            stats->none_count++;
        else if (tf == CHUNK_TRANSFORM_ARX)
            stats->arx_count++;
        else if (tf == CHUNK_TRANSFORM_FEISTEL)
            stats->feistel_count++;
        else
            stats->mixed_count++;
    }
}

static void print_v6_feature_profile(uint32_t features) {
    printf("v6_features: 0x%08X (key_sig=%d, keyed_index=%d, file_id_mask=%d, arx_layer=%d, feistel_head=%d)\n",
           features,
           (features & LITEPAK_V6_FEATURE_KEY_SIG) != 0,
           (features & LITEPAK_V6_FEATURE_KEYED_INDEX) != 0,
           (features & LITEPAK_V6_FEATURE_FILE_ID_MASK) != 0,
           (features & LITEPAK_V6_FEATURE_ARX_LAYER) != 0,
           (features & LITEPAK_V6_FEATURE_FEISTEL_HEAD) != 0);
}

static int write_file_bytes(const char* output_path, const uint8_t* data, size_t len) {
    FILE* fp;
    if (mkdir_parent_dirs(output_path) != 0)
        return -1;
    fp = litepak_fopen_utf8(output_path, "wb");
    if (!fp) return -1;
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

int litepak_unpack_ex2(const char* pak_path, const char* output_dir,
                       const char* manifest_path, bool show_progress,
                       bool verify_trailer, int workers, const char* verify_key_path) {
    litepak_header_t header;
    buffer_t index_plain;
    index_meta_t meta;
    master_key_ctx_t pre_master_key;
    master_key_ctx_t full_master_key;
    uint8_t key_material_signature[32];
    manifest_map_t manifest;
    chunk_plain_cache_t cache;
    uint8_t verify_public_key[32];
    const uint8_t* verify_key = NULL;
    uint32_t file_total = 0;
    uint64_t logical_size = 0;
    int result = -1;
    FILE* fp = NULL;
    double start_ts = (double)GetTickCount64() / 1000.0;

    (void)workers;
    if (litepak_core_self_check_fast() != 0)
        return -1;
    memset(&pre_master_key, 0, sizeof(pre_master_key));
    memset(&full_master_key, 0, sizeof(full_master_key));
    if (load_verify_public_key(verify_key_path, verify_public_key, &verify_key) != 0)
        return -1;
    buffer_init(&index_plain);
    memset(&meta, 0, sizeof(meta));
    manifest_map_init(&manifest);
    chunk_cache_init(&cache);

    {
        FILE* header_fp = litepak_fopen_utf8(pak_path, "rb");
        if (!header_fp)
            goto cleanup;
        if (litepak_read_header(header_fp, &header) != 0) {
            fclose(header_fp);
            goto cleanup;
        }
        fclose(header_fp);
        litepak_emit_log("Header: v=%u file_count=%u, flags=0x%04X, seed=0x%04X",
                         header.version, header.file_count, header.flags, header.seed);
    }
    litepak_emit_log("正在解密索引...");

    if (load_core(pak_path, 0, &header, &index_plain, &meta,
                  &pre_master_key, &full_master_key, &cache, key_material_signature) != 0)
        goto cleanup;

    if (manifest_path && load_manifest(manifest_path, &manifest) != 0)
        goto cleanup;

    if (verify_trailer && header.has_trailer) {
        int trailer_ok;
        fp = litepak_fopen_utf8(pak_path, "rb");
        if (!fp) goto cleanup;
        trailer_ok = litepak_read_trailer(fp, index_plain.data, index_plain.len, &header,
                                          verify_key, key_material_signature);
        fclose(fp);
        fp = NULL;
        litepak_emit_log("Trailer: %s (%s)", trailer_ok ? "校验通过" : "校验失败",
                         header.ed25519_signed ? "Ed25519已验证" : "无签名");
        if (!trailer_ok)
            goto cleanup;
    }

    if (mkdir_one(output_dir) != 0)
        goto cleanup;

    for (uint32_t i = 0; i < meta.entry_count; i++) {
        const entry_t* e = &meta.entries[i];
        buffer_t plain;
        char hash_hex[33];
        char fallback_name[40];
        const char* rel;
        char* out_path;
        if (e->flags != ENTRY_FILE)
            continue;
        hash_bytes_to_hex_upper(e->hash_bytes, hash_hex);
        rel = manifest_map_find(&manifest, hash_hex);
        if (!rel) {
            snprintf(fallback_name, sizeof(fallback_name), "%s.bin", hash_hex);
            rel = fallback_name;
        }
        out_path = join_path2(output_dir, rel);
        if (!out_path) goto cleanup;
        if (decode_entry_bytes(pak_path, e, meta.chunk_records, meta.chunk_record_count,
                               meta.chunk_refs, meta.chunk_ref_count,
                               &pre_master_key, &full_master_key,
                               &cache, &plain) != 0) {
            free(out_path);
            goto cleanup;
        }
        if (write_file_bytes(out_path, plain.data, plain.len) != 0) {
            free(out_path);
            buffer_free(&plain);
            goto cleanup;
        }
        buffer_free(&plain);
        free(out_path);
        file_total++;
        logical_size += e->original_size;
        if (show_progress)
            litepak_print_progress("解包处理中", (int)file_total, (int)header.file_count);
    }

    {
        char size_buf[64];
        char pak_size_buf[64];
        char duration_buf[64];
        long long pak_size = 0;
        FILE* size_fp = litepak_fopen_utf8(pak_path, "rb");
        if (size_fp) {
            if (_fseeki64(size_fp, 0, SEEK_END) == 0)
                pak_size = _ftelli64(size_fp);
            fclose(size_fp);
        }
        double elapsed = (double)GetTickCount64() / 1000.0 - start_ts;
        double throughput = elapsed > 0.0 ? ((double)logical_size / (1024.0 * 1024.0)) / elapsed : 0.0;
        litepak_format_size(logical_size, size_buf, sizeof(size_buf));
        litepak_format_size((uint64_t)(pak_size > 0 ? pak_size : 0), pak_size_buf, sizeof(pak_size_buf));
        litepak_format_duration(elapsed, duration_buf, sizeof(duration_buf));
        litepak_emit_log("解包汇总: 文件数=%u | 输出=%s | 封包=%s | 耗时=%s | throughput=%.2f MiB/s",
                         file_total, size_buf, pak_size_buf, duration_buf, throughput);
    }
    result = 0;

cleanup:
    if (fp) fclose(fp);
    master_key_ctx_clear(&pre_master_key);
    master_key_ctx_clear(&full_master_key);
    chunk_cache_free(&cache);
    manifest_map_free(&manifest);
    litepak_free_index_meta(&meta);
    buffer_free(&index_plain);
    return result;
}

int litepak_unpack_ex(const char* pak_path, const char* output_dir,
                      const char* manifest_path, bool show_progress,
                      bool verify_trailer, int workers) {
    return litepak_unpack_ex2(pak_path, output_dir, manifest_path, show_progress, verify_trailer, workers, NULL);
}

int litepak_unpack(const char* pak_path, const char* output_dir,
                   const char* manifest_path, bool show_progress,
                   bool verify_trailer) {
    return litepak_unpack_ex2(pak_path, output_dir, manifest_path, show_progress, verify_trailer, 1, NULL);
}

int litepak_extract_by_name(const char* pak_path, const char* rel_name,
                            const char* output_path) {
    litepak_header_t header;
    buffer_t index_plain;
    index_meta_t meta;
    chunk_plain_cache_t cache;
    master_key_ctx_t pre_master_key;
    master_key_ctx_t full_master_key;
    uint8_t target_hash[16];
    int result = -1;

    if (litepak_core_self_check_fast() != 0)
        return -1;
    memset(&pre_master_key, 0, sizeof(pre_master_key));
    memset(&full_master_key, 0, sizeof(full_master_key));
    buffer_init(&index_plain);
    memset(&meta, 0, sizeof(meta));
    chunk_cache_init(&cache);
    litepak_path_hash_bytes(rel_name, target_hash);

    if (load_core(pak_path, 0, &header, &index_plain, &meta,
                  &pre_master_key, &full_master_key, &cache, NULL) != 0)
        goto cleanup;

    for (uint32_t i = 0; i < meta.entry_count; i++) {
        buffer_t plain;
        if (meta.entries[i].flags != ENTRY_FILE)
            continue;
        if (memcmp(meta.entries[i].hash_bytes, target_hash, 16) != 0)
            continue;
        if (decode_entry_bytes(pak_path, &meta.entries[i], meta.chunk_records, meta.chunk_record_count,
                               meta.chunk_refs, meta.chunk_ref_count,
                               &pre_master_key, &full_master_key,
                               &cache, &plain) != 0) {
            goto cleanup;
        }
        result = write_file_bytes(output_path, plain.data, plain.len);
        buffer_free(&plain);
        goto cleanup;
    }

cleanup:
    master_key_ctx_clear(&pre_master_key);
    master_key_ctx_clear(&full_master_key);
    chunk_cache_free(&cache);
    litepak_free_index_meta(&meta);
    buffer_free(&index_plain);
    return result;
}

int litepak_info_ex(const char* pak_path, const char* verify_key_path) {
    litepak_header_t header;
    buffer_t index_plain;
    index_meta_t meta;
    chunk_plain_cache_t cache;
    master_key_ctx_t pre_master_key;
    master_key_ctx_t full_master_key;
    uint8_t key_material_signature[32];
    FILE* fp = NULL;
    uint8_t verify_public_key[32];
    const uint8_t* verify_key = NULL;
    v6_info_stats_t stats;
    int trailer_state = -1;
    int rc = -1;

    if (litepak_core_self_check_fast() != 0)
        return -1;
    memset(&pre_master_key, 0, sizeof(pre_master_key));
    memset(&full_master_key, 0, sizeof(full_master_key));
    if (load_verify_public_key(verify_key_path, verify_public_key, &verify_key) != 0)
        return -1;

    buffer_init(&index_plain);
    memset(&meta, 0, sizeof(meta));
    chunk_cache_init(&cache);

    if (load_core(pak_path, 0, &header, &index_plain, &meta,
                  &pre_master_key, &full_master_key, &cache, key_material_signature) != 0)
        goto cleanup;

    if (header.has_trailer) {
        fp = litepak_fopen_utf8(pak_path, "rb");
        if (!fp) goto cleanup;
        trailer_state = litepak_read_trailer(fp, index_plain.data, index_plain.len, &header,
                                             verify_key, key_material_signature);
        fclose(fp);
        fp = NULL;
    }

    collect_v6_info_stats(&meta, &stats);
    printf("文件:     %s\n", pak_path);
    printf("版本:     %u\n", header.version);
    printf("Flags:    0x%04X (trailer=%d, idx_compressed=%d, chunk_index=%d, aes_gcm=%d, ed25519=%d, idx_obfuscated=%d, strong_wb=%d)\n",
           header.flags, header.has_trailer, header.index_compressed, header.chunk_index,
           header.aes_gcm, header.ed25519_signed, header.index_obfuscated, header.strong_wb);
    print_v6_feature_profile(header.v6_features);
    printf("secure_core: masm=%d, keyctx=1, selfcheck=1\n", LITEPAK_SECURE_CORE_MASM);
    printf("Seed:     0x%04X\n", header.seed);
    printf("文件数:   %u\n", header.file_count);
    printf("chunk数:  %u\n", meta.chunk_record_count);
    printf("chunk引用:%u\n", meta.chunk_ref_count);
    printf("chunk_kind: file=%u, k9=%u\n", stats.file_chunk_count, stats.k9_chunk_count);
    printf("transform_none: %u\n", stats.none_count);
    printf("transform_arx: %u\n", stats.arx_count);
    printf("transform_feistel: %u\n", stats.feistel_count);
    printf("transform_mixed: %u\n", stats.mixed_count);
    {
        char size_buf[64];
        char stored_buf[64];
        double ratio = stats.original_total > 0 ? (double)stats.stored_total / (double)stats.original_total : 0.0;
        litepak_format_size(stats.original_total, size_buf, sizeof(size_buf));
        litepak_format_size(stats.stored_total, stored_buf, sizeof(stored_buf));
        printf("payload原始: %s\n", size_buf);
        printf("payload存储: %s\n", stored_buf);
        printf("payload压缩率: %.2f%%\n", ratio * 100.0);
        litepak_format_size(meta.whole_file_threshold, size_buf, sizeof(size_buf));
        printf("整文件阈值: %s\n", size_buf);
        litepak_format_size(meta.cdc_min_size, size_buf, sizeof(size_buf));
        printf("CDC(min): %s\n", size_buf);
        litepak_format_size(meta.cdc_avg_size, size_buf, sizeof(size_buf));
        printf("CDC(avg): %s\n", size_buf);
        litepak_format_size(meta.cdc_max_size, size_buf, sizeof(size_buf));
        printf("CDC(max): %s\n", size_buf);
    }
    printf("索引偏移: %llu\n", (unsigned long long)header.index_offset);
    printf("索引加密: %llu\n", (unsigned long long)header.index_encrypted_sz);
    printf("索引明文: %llu\n", (unsigned long long)header.index_plain_sz);
    if (header.has_trailer)
        printf("Trailer:  %s (key_material_signature=%s)\n",
               trailer_state > 0 ? "校验通过" : "校验失败",
               trailer_state > 0 ? "已验证" : "未验证");
    rc = 0;

cleanup:
    if (fp) fclose(fp);
    master_key_ctx_clear(&pre_master_key);
    master_key_ctx_clear(&full_master_key);
    chunk_cache_free(&cache);
    litepak_free_index_meta(&meta);
    buffer_free(&index_plain);
    return rc;
}

int litepak_info(const char* pak_path) {
    return litepak_info_ex(pak_path, NULL);
}

int litepak_verify_ex(const char* pak_path, const char* verify_key_path) {
    litepak_header_t header;
    buffer_t index_plain;
    index_meta_t meta;
    chunk_plain_cache_t cache;
    master_key_ctx_t pre_master_key;
    master_key_ctx_t full_master_key;
    uint8_t key_material_signature[32];
    int chunks_ok = 0, chunks_fail = 0, files_ok = 0, files_fail = 0;
    v6_info_stats_t stats;
    double start_ts = (double)GetTickCount64() / 1000.0;
    int trailer_ok = 1;
    FILE* fp = NULL;
    uint8_t verify_public_key[32];
    const uint8_t* verify_key = NULL;
    int rc = -1;

    if (litepak_core_self_check_fast() != 0)
        return -1;
    memset(&pre_master_key, 0, sizeof(pre_master_key));
    memset(&full_master_key, 0, sizeof(full_master_key));
    if (load_verify_public_key(verify_key_path, verify_public_key, &verify_key) != 0)
        return -1;

    buffer_init(&index_plain);
    memset(&meta, 0, sizeof(meta));
    chunk_cache_init(&cache);

    printf("正在完整校验: %s\n", pak_path);
    if (load_core(pak_path, 1, &header, &index_plain, &meta,
                  &pre_master_key, &full_master_key, &cache, key_material_signature) != 0)
        goto cleanup;
    collect_v6_info_stats(&meta, &stats);

    for (uint32_t i = 0; i < meta.chunk_record_count; i++) {
        buffer_t plain;
        if (read_chunk_plain(pak_path, &meta.chunk_records[i], &pre_master_key, &full_master_key,
                             &cache, &plain) == 0) {
            chunks_ok++;
            buffer_free(&plain);
        } else {
            fprintf(stderr, "verify chunk failed: index=%u\n", i);
            chunks_fail++;
        }
    }

    for (uint32_t i = 0; i < meta.entry_count; i++) {
        buffer_t plain;
        if (meta.entries[i].flags != ENTRY_FILE)
            continue;
        if (decode_entry_bytes(pak_path, &meta.entries[i], meta.chunk_records, meta.chunk_record_count,
                               meta.chunk_refs, meta.chunk_ref_count,
                               &pre_master_key, &full_master_key,
                               &cache, &plain) == 0) {
            files_ok++;
            buffer_free(&plain);
        } else {
            fprintf(stderr, "verify file failed: entry=%u chunk_ref_start=%u chunk_count=%u\n",
                    i, meta.entries[i].chunk_ref_start, meta.entries[i].chunk_count);
            files_fail++;
        }
    }

    if (header.has_trailer) {
        fp = litepak_fopen_utf8(pak_path, "rb");
        if (!fp) goto cleanup;
        trailer_ok = litepak_read_trailer(fp, index_plain.data, index_plain.len, &header,
                                          verify_key, key_material_signature);
        fclose(fp);
        fp = NULL;
    }

    printf("  header_crc: true\n");
    printf("  index_decrypt: true\n");
    printf("  index_hashes: true\n");
    printf("  k9_decrypt: true\n");
    printf("  secure_core: masm=%d, keyctx=1, selfcheck=1\n", LITEPAK_SECURE_CORE_MASM);
    printf("  chunks_ok: %d\n", chunks_ok);
    printf("  chunks_fail: %d\n", chunks_fail);
    {
        double elapsed = (double)GetTickCount64() / 1000.0 - start_ts;
        double logical_mb = (double)stats.original_total / (1024.0 * 1024.0);
        double stored_mb = (double)stats.stored_total / (1024.0 * 1024.0);
        double mbps = elapsed > 0.0 ? logical_mb / elapsed : 0.0;
        double chunks_per_sec = elapsed > 0.0 ? (double)meta.chunk_record_count / elapsed : 0.0;
        printf("  files_ok: %d\n", files_ok);
        printf("  files_fail: %d\n", files_fail);
        printf("  trailer: %s\n", header.has_trailer ? (trailer_ok ? "true" : "false") : "null");
        printf("  bench_total_ms: %.2f\n", elapsed * 1000.0);
        printf("  bench_logical_mb: %.2f\n", logical_mb);
        printf("  bench_stored_mb: %.2f\n", stored_mb);
        printf("  bench_verify_mbps: %.2f\n", mbps);
        printf("  bench_chunks_per_sec: %.2f\n", chunks_per_sec);
    }
    printf("校验结果: %s\n", (chunks_fail == 0 && files_fail == 0 && trailer_ok) ? "全部通过" : "存在问题");
    rc = (chunks_fail == 0 && files_fail == 0 && trailer_ok) ? 0 : -1;

cleanup:
    if (fp) fclose(fp);
    master_key_ctx_clear(&pre_master_key);
    master_key_ctx_clear(&full_master_key);
    chunk_cache_free(&cache);
    litepak_free_index_meta(&meta);
    buffer_free(&index_plain);
    return rc;
}

int litepak_verify(const char* pak_path) {
    return litepak_verify_ex(pak_path, NULL);
}
