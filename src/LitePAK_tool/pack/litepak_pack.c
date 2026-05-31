/*
 * LitePAK 封包主流程
 *
 * 这一层负责：
 * 1. 递归扫描输入目录并按相对路径排序
 * 2. 生成 manifest
 * 3. 小文件整文件模式 / 大文件 CDC 模式
 * 4. chunk 去重、整文件去重、压缩、加密、落盘
 * 5. 构建 index / header / trailer
 */
#include "litepak.h"
#include "../common/litepak_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../third/zstd/zstd.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

/* ============================================================================
 * 动态数组
 * ============================================================================ */

typedef struct {
    file_entry_t* items;
    size_t count;
    size_t cap;
} file_list_t;

typedef struct {
    chunk_record_t* items;
    uint32_t count;
    uint32_t cap;
} chunk_record_list_t;

typedef struct {
    entry_t* items;
    uint32_t count;
    uint32_t cap;
} entry_list_t;

typedef struct {
    uint32_t* items;
    uint32_t count;
    uint32_t cap;
} u32_list_t;

typedef struct {
    chunk_dedup_entry_t* items;
    size_t count;
    size_t cap;
} chunk_dedup_list_t;

typedef struct {
    file_dedup_entry_t* items;
    size_t count;
    size_t cap;
} file_dedup_list_t;

typedef struct {
    uint8_t mode;
    uint32_t count;
} mode_count_t;

/* ============================================================================
 * 基础辅助
 * ============================================================================ */

static char* dup_string(const char* s) {
    size_t len;
    char* out;
    if (!s)
        return NULL;
    len = strlen(s);
    out = (char*)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static void bytes_to_hex_upper(const uint8_t* data, size_t len, char* out) {
    static const char* HEX = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = HEX[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = HEX[data[i] & 0xF];
    }
    out[len * 2] = '\0';
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
    char* tmp = dup_string(file_path);
    char* p;
    if (!tmp)
        return -1;
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

static char* join_path_fs(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    int need_sep = (la > 0 && a[la - 1] != '/' && a[la - 1] != '\\');
    char* out = (char*)malloc(la + lb + (need_sep ? 2 : 1));
    if (!out)
        return NULL;
    memcpy(out, a, la);
    if (need_sep)
        out[la++] = '/';
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

static char* join_rel_path(const char* a, const char* b) {
    size_t la;
    size_t lb;
    char* out;
    if (!a || !a[0])
        return dup_string(b);
    la = strlen(a);
    lb = strlen(b);
    out = (char*)malloc(la + lb + 2);
    if (!out)
        return NULL;
    memcpy(out, a, la);
    out[la] = '/';
    memcpy(out + la + 1, b, lb);
    out[la + lb + 1] = '\0';
    return out;
}

static int cmp_file_entry_by_rel(const void* a, const void* b) {
    const file_entry_t* fa = (const file_entry_t*)a;
    const file_entry_t* fb = (const file_entry_t*)b;
    return strcmp(fa->rel_path, fb->rel_path);
}

/* ============================================================================
 * 动态数组管理
 * ============================================================================ */

static void file_list_init(file_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void file_list_free(file_list_t* list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].full_path);
        free(list->items[i].rel_path);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int file_list_push(file_list_t* list, const char* full_path, const char* rel_path, uint64_t file_size) {
    file_entry_t* items;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 64;
        items = (file_entry_t*)realloc(list->items, new_cap * sizeof(file_entry_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    memset(&list->items[list->count], 0, sizeof(list->items[list->count]));
    list->items[list->count].full_path = dup_string(full_path);
    list->items[list->count].rel_path = dup_string(rel_path);
    list->items[list->count].file_size = file_size;
    if (!list->items[list->count].full_path || !list->items[list->count].rel_path)
        return -1;
    list->count++;
    return 0;
}

static void chunk_record_list_init(chunk_record_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void chunk_record_list_free(chunk_record_list_t* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int chunk_record_list_push(chunk_record_list_t* list, const chunk_record_t* rec) {
    chunk_record_t* items;
    if (list->count >= list->cap) {
        uint32_t new_cap = list->cap ? list->cap * 2 : 128;
        items = (chunk_record_t*)realloc(list->items, (size_t)new_cap * sizeof(chunk_record_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count] = *rec;
    list->count++;
    return 0;
}

static void entry_list_init(entry_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void entry_list_free(entry_list_t* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int entry_list_push(entry_list_t* list, const entry_t* entry) {
    entry_t* items;
    if (list->count >= list->cap) {
        uint32_t new_cap = list->cap ? list->cap * 2 : 64;
        items = (entry_t*)realloc(list->items, (size_t)new_cap * sizeof(entry_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count] = *entry;
    list->count++;
    return 0;
}

static void u32_list_init(u32_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void u32_list_free(u32_list_t* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int u32_list_push(u32_list_t* list, uint32_t value) {
    uint32_t* items;
    if (list->count >= list->cap) {
        uint32_t new_cap = list->cap ? list->cap * 2 : 128;
        items = (uint32_t*)realloc(list->items, (size_t)new_cap * sizeof(uint32_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count] = value;
    list->count++;
    return 0;
}

static void chunk_dedup_list_init(chunk_dedup_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void chunk_dedup_list_free(chunk_dedup_list_t* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int chunk_dedup_list_push(chunk_dedup_list_t* list, const chunk_dedup_entry_t* entry) {
    chunk_dedup_entry_t* items;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 256;
        items = (chunk_dedup_entry_t*)realloc(list->items, new_cap * sizeof(chunk_dedup_entry_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count] = *entry;
    list->count++;
    return 0;
}

static void file_dedup_list_init(file_dedup_list_t* list) {
    memset(list, 0, sizeof(*list));
}

static void file_dedup_list_free(file_dedup_list_t* list) {
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int file_dedup_list_push(file_dedup_list_t* list, const file_dedup_entry_t* entry) {
    file_dedup_entry_t* items;
    if (list->count >= list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 128;
        items = (file_dedup_entry_t*)realloc(list->items, new_cap * sizeof(file_dedup_entry_t));
        if (!items)
            return -1;
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->count] = *entry;
    list->count++;
    return 0;
}

/* ============================================================================
 * 文件扫描 / 清单 / 读写
 * ============================================================================ */

static int collect_files_recursive(file_list_t* out, const char* base_dir, const char* rel_dir) {
#ifdef _WIN32
    char* dir_path = rel_dir && rel_dir[0] ? join_path_fs(base_dir, rel_dir) : dup_string(base_dir);
    char* search_path;
    wchar_t* wide_search = NULL;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (!dir_path)
        return -1;
    search_path = join_path_fs(dir_path, "*");
    if (!search_path) {
        free(dir_path);
        return -1;
    }
    if (litepak_utf8_to_wide_dup(search_path, &wide_search) != 0) {
        free(search_path);
        free(dir_path);
        return -1;
    }

    h = FindFirstFileW(wide_search, &fd);
    free(wide_search);
    free(search_path);
    if (h == INVALID_HANDLE_VALUE) {
        free(dir_path);
        return -1;
    }

    do {
        char* name_utf8 = NULL;
        char* next_rel;
        char* next_full;
        uint64_t size;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (litepak_wide_to_utf8_dup(fd.cFileName, &name_utf8) != 0) {
            FindClose(h);
            free(dir_path);
            return -1;
        }
        next_rel = join_rel_path(rel_dir, name_utf8);
        next_full = join_path_fs(dir_path, name_utf8);
        if (!next_rel || !next_full) {
            free(name_utf8);
            free(next_rel);
            free(next_full);
            FindClose(h);
            free(dir_path);
            return -1;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (collect_files_recursive(out, base_dir, next_rel) != 0) {
                free(name_utf8);
                free(next_rel);
                free(next_full);
                FindClose(h);
                free(dir_path);
                return -1;
            }
        } else {
            size = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;
            if (file_list_push(out, next_full, next_rel, size) != 0) {
                free(name_utf8);
                free(next_rel);
                free(next_full);
                FindClose(h);
                free(dir_path);
                return -1;
            }
        }
        free(name_utf8);
        free(next_rel);
        free(next_full);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    free(dir_path);
    return 0;
#else
    (void)out;
    (void)base_dir;
    (void)rel_dir;
    return -1;
#endif
}

static int write_manifest_file(const file_list_t* files, const char* manifest_path) {
    FILE* fp;
    if (mkdir_parent_dirs(manifest_path) != 0)
        return -1;
    fp = litepak_fopen_utf8(manifest_path, "wb");
    if (!fp)
        return -1;
    for (size_t i = 0; i < files->count; i++) {
        uint8_t hash_bytes[16];
        char hash_hex[33];
        litepak_path_hash_bytes(files->items[i].rel_path, hash_bytes);
        bytes_to_hex_upper(hash_bytes, 16, hash_hex);
        fprintf(fp, "%s\t%s\t%s", hash_hex, hash_hex, files->items[i].rel_path);
        if (i + 1 < files->count)
            fputc('\n', fp);
    }
    fclose(fp);
    return 0;
}

static int read_file_all(const char* path, buffer_t* out) {
    FILE* fp;
    long long sz;
    buffer_init(out);
    fp = litepak_fopen_utf8(path, "rb");
    if (!fp)
        return -1;
    if (_fseeki64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    sz = _ftelli64(fp);
    if (sz < 0) {
        fclose(fp);
        return -1;
    }
    if (_fseeki64(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    buffer_reserve(out, (size_t)sz);
    out->len = (size_t)sz;
    if (sz > 0 && fread(out->data, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        buffer_free(out);
        return -1;
    }
    fclose(fp);
    return 0;
}

typedef struct {
    buffer_t raw;
    uint8_t path_hash[16];
    uint8_t content_id[32];
    uint32_t file_crc32c;
    int ok;
} pack_preread_result_t;

#ifdef _WIN32
typedef struct {
    const file_list_t* files;
    pack_preread_result_t* results;
    uint32_t next_index;
    CRITICAL_SECTION lock;
} pack_preread_context_t;

static DWORD WINAPI pack_preread_worker(LPVOID param) {
    pack_preread_context_t* ctx = (pack_preread_context_t*)param;
    for (;;) {
        uint32_t index;
        const file_entry_t* file;
        pack_preread_result_t* result;
        EnterCriticalSection(&ctx->lock);
        index = ctx->next_index++;
        LeaveCriticalSection(&ctx->lock);
        if (index >= ctx->files->count)
            break;
        file = &ctx->files->items[index];
        result = &ctx->results[index];
        buffer_init(&result->raw);
        if (read_file_all(file->full_path, &result->raw) != 0) {
            result->ok = 0;
            continue;
        }
        litepak_path_hash_bytes(file->rel_path, result->path_hash);
        blake2b_full(result->raw.data, result->raw.len, result->content_id, 32, NULL, 0, (const uint8_t*)"LiteDedV6", 9);
        result->file_crc32c = litepak_crc32c(result->raw.data, result->raw.len, 0);
        result->ok = 1;
    }
    return 0;
}
#endif

static void pack_preread_results_free(pack_preread_result_t* results, uint32_t count) {
    if (!results) return;
    for (uint32_t i = 0; i < count; i++)
        buffer_free(&results[i].raw);
    free(results);
}

static int pack_preread_all(const file_list_t* files, int workers, pack_preread_result_t** out_results) {
#ifndef _WIN32
    (void)files;
    (void)workers;
    *out_results = NULL;
    return 0;
#else
    pack_preread_context_t ctx;
    HANDLE* threads;
    int thread_count;
    pack_preread_result_t* results;
    if (!out_results || workers <= 1) {
        if (out_results) *out_results = NULL;
        return 0;
    }
    thread_count = workers;
    if (thread_count > 32) thread_count = 32;
    if ((size_t)thread_count > files->count) thread_count = (int)files->count;
    if (thread_count <= 1) {
        *out_results = NULL;
        return 0;
    }
    results = (pack_preread_result_t*)calloc(files->count, sizeof(pack_preread_result_t));
    threads = (HANDLE*)calloc((size_t)thread_count, sizeof(HANDLE));
    if (!results || !threads) {
        free(results);
        free(threads);
        return -1;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.files = files;
    ctx.results = results;
    InitializeCriticalSection(&ctx.lock);
    for (int i = 0; i < thread_count; i++) {
        threads[i] = CreateThread(NULL, 0, pack_preread_worker, &ctx, 0, NULL);
        if (!threads[i]) {
            DeleteCriticalSection(&ctx.lock);
            for (int j = 0; j < i; j++) CloseHandle(threads[j]);
            free(threads);
            pack_preread_results_free(results, (uint32_t)files->count);
            return -1;
        }
    }
    WaitForMultipleObjects((DWORD)thread_count, threads, TRUE, INFINITE);
    for (int i = 0; i < thread_count; i++)
        CloseHandle(threads[i]);
    DeleteCriticalSection(&ctx.lock);
    free(threads);
    for (size_t i = 0; i < files->count; i++) {
        if (!results[i].ok) {
            pack_preread_results_free(results, (uint32_t)files->count);
            return -1;
        }
    }
    *out_results = results;
    return 0;
#endif
}

static void summarize_file_chunks(const chunk_record_list_t* chunk_records, const u32_list_t* chunk_refs,
                                  uint32_t ref_start, uint32_t chunk_count,
                                  char* mode_summary, size_t mode_summary_size,
                                  uint64_t* out_stored) {
    mode_count_t modes[8];
    size_t mode_count = 0;
    uint64_t stored = 0;
    mode_summary[0] = '\0';
    for (uint32_t i = 0; i < chunk_count; i++) {
        uint32_t ref_pos = ref_start + i;
        uint32_t chunk_index;
        uint8_t mode;
        size_t m;
        if (ref_pos >= chunk_refs->count)
            continue;
        chunk_index = chunk_refs->items[ref_pos];
        if (chunk_index >= chunk_records->count)
            continue;
        mode = chunk_records->items[chunk_index].mode;
        stored += chunk_records->items[chunk_index].stored_size;
        for (m = 0; m < mode_count; m++) {
            if (modes[m].mode == mode) {
                modes[m].count++;
                break;
            }
        }
        if (m == mode_count && mode_count < sizeof(modes) / sizeof(modes[0])) {
            modes[mode_count].mode = mode;
            modes[mode_count].count = 1;
            mode_count++;
        }
    }
    if (mode_count == 0) {
        snprintf(mode_summary, mode_summary_size, "raw:0");
    } else {
        size_t used = 0;
        for (size_t i = 0; i < mode_count; i++) {
            int n = snprintf(mode_summary + used, used < mode_summary_size ? mode_summary_size - used : 0,
                             "%s%s:%u", i ? "," : "", litepak_mode_name(modes[i].mode), modes[i].count);
            if (n < 0)
                break;
            used += (size_t)n;
            if (used >= mode_summary_size)
                break;
        }
    }
    if (out_stored)
        *out_stored = stored;
}

static int load_sign_seed(const char* sign_key_path, uint8_t out_seed[32], int* out_has_custom) {
    FILE* fp;
    if (out_has_custom)
        *out_has_custom = 0;
    litepak_materialize_default_sign_seed(out_seed);
    if (!sign_key_path || !sign_key_path[0])
        return 0;
    fp = litepak_fopen_utf8(sign_key_path, "rb");
    if (!fp)
        return -1;
    if (fread(out_seed, 1, 32, fp) != 32) {
        fclose(fp);
        return -1;
    }
    if (out_has_custom)
        *out_has_custom = 1;
    fclose(fp);
    return 0;
}

/* ============================================================================
 * 去重 / 压缩 / 加密落盘
 * ============================================================================ */

static int chunk_dedup_find(const chunk_dedup_list_t* list,
                            uint8_t chunk_kind,
                            const uint8_t chunk_hash[32],
                            uint64_t original_size,
                            uint32_t chunk_crc32c,
                            uint32_t* out_index) {
    for (size_t i = 0; i < list->count; i++) {
        const chunk_dedup_entry_t* e = &list->items[i];
        if (e->chunk_kind != chunk_kind)
            continue;
        if (e->original_size != original_size)
            continue;
        if (e->chunk_crc32c != chunk_crc32c)
            continue;
        if (memcmp(e->chunk_hash, chunk_hash, 32) != 0)
            continue;
        *out_index = e->chunk_index;
        return 1;
    }
    return 0;
}

static int file_dedup_find(const file_dedup_list_t* list,
                           const uint8_t content_id[32],
                           uint32_t* out_chunk_ref_start,
                           uint32_t* out_chunk_count) {
    for (size_t i = 0; i < list->count; i++) {
        const file_dedup_entry_t* e = &list->items[i];
        if (memcmp(e->content_id, content_id, 32) == 0) {
            *out_chunk_ref_start = e->chunk_ref_start;
            *out_chunk_count = e->chunk_count;
            return 1;
        }
    }
    return 0;
}

static int add_chunk(FILE* fp,
                     chunk_dedup_list_t* chunk_dedup,
                     chunk_record_list_t* chunk_records,
                     const uint8_t* chunk_payload,
                     size_t chunk_len,
                     uint8_t chunk_kind,
                     const uint8_t pre_master_key[32],
                     const uint8_t full_master_key[32],
                     const char* compression,
                     const char* rel_path,
                     const uint8_t file_id[16],
                     bool allow_reuse,
                     bool whole_file_mode,
                     bool is_k9,
                     uint32_t* out_index,
                     int* out_reused) {
    uint8_t chunk_hash[32];
    uint32_t chunk_crc32c;
    uint32_t existed_index;
    buffer_t plain_for_pack;
    buffer_t packed;
    buffer_t enc;
    chunk_record_t rec;
    chunk_dedup_entry_t dedup_entry;
    uint8_t master_key[32];
    uint8_t transform_flags = is_k9 ? CHUNK_TRANSFORM_NONE : litepak_select_transform_flags(rel_path, (uint64_t)chunk_len, MODE_RAW);

    if (transform_flags != CHUNK_TRANSFORM_NONE)
        allow_reuse = false;
    litepak_chunk_hash_bytes(chunk_payload, chunk_len, chunk_hash);
    chunk_crc32c = litepak_crc32c(chunk_payload, chunk_len, 0);
    if (allow_reuse && chunk_dedup_find(chunk_dedup, chunk_kind, chunk_hash, (uint64_t)chunk_len,
                                        chunk_crc32c, &existed_index)) {
        *out_index = existed_index;
        *out_reused = 1;
        return 0;
    }

    memset(&rec, 0, sizeof(rec));
    buffer_init(&plain_for_pack);
    buffer_init(&packed);
    buffer_init(&enc);
    if (buffer_append(&plain_for_pack, chunk_payload, chunk_len) != 0)
        return -1;

    litepak_random_bytes(rec.data_nonce, LITEPAK_NONCE_SIZE);
    if (is_k9)
        memcpy(master_key, pre_master_key, 32);
    else
        memcpy(master_key, full_master_key, 32);

    if (transform_flags & CHUNK_TRANSFORM_ARX)
        litepak_apply_arx_transform(plain_for_pack.data, plain_for_pack.len, master_key, file_id, rec.data_nonce, (uint64_t)chunk_len);
    if (transform_flags & CHUNK_TRANSFORM_FEISTEL)
        litepak_apply_feistel_transform(plain_for_pack.data, plain_for_pack.len, master_key, file_id, rec.data_nonce, (uint64_t)chunk_len);

    if (litepak_compress_chunk(plain_for_pack.data, plain_for_pack.len, compression, whole_file_mode, &packed) != 0) {
        buffer_free(&plain_for_pack);
        return -1;
    }

    if (litepak_segmented_encrypt(packed.data, packed.len, master_key, rec.data_nonce,
                                  chunk_kind, (uint64_t)chunk_len, is_k9, &enc) != 0) {
        buffer_free(&plain_for_pack);
        buffer_free(&packed);
        return -1;
    }

    memcpy(rec.chunk_hash, chunk_hash, 32);
    rec.original_size = (uint64_t)chunk_len;
    rec.stored_size = (uint64_t)enc.len;
    rec.data_offset = (uint64_t)_ftelli64(fp);
    rec.chunk_crc32c = chunk_crc32c;
    rec.chunk_kind = chunk_kind;
    rec.mode = packed.len > 0 ? packed.data[0] : MODE_RAW;
    rec.transform_flags = transform_flags;
    memcpy(rec.file_id, file_id, LITEPAK_FILE_ID_SIZE);

    if (enc.len > 0 && fwrite(enc.data, 1, enc.len, fp) != enc.len) {
        buffer_free(&plain_for_pack);
        buffer_free(&packed);
        buffer_free(&enc);
        return -1;
    }
    if (chunk_record_list_push(chunk_records, &rec) != 0) {
        buffer_free(&plain_for_pack);
        buffer_free(&packed);
        buffer_free(&enc);
        return -1;
    }

    if (allow_reuse) {
        memset(&dedup_entry, 0, sizeof(dedup_entry));
        dedup_entry.chunk_kind = chunk_kind;
        memcpy(dedup_entry.chunk_hash, chunk_hash, 32);
        dedup_entry.original_size = (uint64_t)chunk_len;
        dedup_entry.chunk_crc32c = chunk_crc32c;
        dedup_entry.chunk_index = chunk_records->count - 1;
        if (chunk_dedup_list_push(chunk_dedup, &dedup_entry) != 0) {
            buffer_free(&plain_for_pack);
            buffer_free(&packed);
            buffer_free(&enc);
            return -1;
        }
    }

    *out_index = chunk_records->count - 1;
    *out_reused = 0;
    buffer_free(&plain_for_pack);
    buffer_free(&packed);
    buffer_free(&enc);
    return 0;
}

static int compress_index_zstd(const uint8_t* data, size_t len, buffer_t* out) {
    size_t bound;
    size_t ret;
    buffer_init(out);
    bound = ZSTD_compressBound(len);
    buffer_reserve(out, bound);
    ret = ZSTD_compress(out->data, bound, data, len, 19);
    if (ZSTD_isError(ret)) {
        buffer_free(out);
        return -1;
    }
    out->len = ret;
    return 0;
}

/* ============================================================================
 * 高层封包入口
 * ============================================================================ */

int litepak_pack_ex(const char* input_dir, const char* pak_path, const char* manifest_path,
                    bool dedup_mode, bool show_progress, const char* compression,
                    int cdc_avg_kb, int whole_file_threshold_kb, const char* sign_key_path,
                    int workers) {
    file_list_t files;
    chunk_record_list_t chunk_records;
    entry_list_t entries;
    u32_list_t chunk_refs;
    chunk_dedup_list_t chunk_dedup;
    file_dedup_list_t file_dedup;
    cdc_params_t cdc_params;
    uint64_t logical_size = 0;
    uint32_t total_files = 0;
    uint32_t whole_file_threshold;
    uint32_t cdc_avg_size;
    uint8_t k2[16], k6[16], k8[16], k10[16], k9_plain[16];
    uint8_t pre_master_key[32];
    uint8_t full_master_key[32];
    uint8_t sign_seed[32];
    uint16_t seed;
    FILE* fp = NULL;
    int rc = -1;
    int has_custom_sign = 0;
    uint32_t dedup_reused = 0;
    uint32_t exact_file_reused = 0;
    uint32_t whole_count = 0;
    uint32_t cdc_count = 0;
    uint32_t file_dedup_count = 0;
    double start_ts;
    pack_preread_result_t* preread_results = NULL;
    int use_preread = 0;

    buffer_t index_plain;
    buffer_t obfuscated_index;
    buffer_t index_payload;
    buffer_t index_cipher;

    file_list_init(&files);
    chunk_record_list_init(&chunk_records);
    entry_list_init(&entries);
    u32_list_init(&chunk_refs);
    chunk_dedup_list_init(&chunk_dedup);
    file_dedup_list_init(&file_dedup);
    buffer_init(&index_plain);
    buffer_init(&obfuscated_index);
    buffer_init(&index_payload);
    buffer_init(&index_cipher);

    if (!input_dir || !pak_path || !manifest_path) {
        litepak_emit_log("pack 参数不完整");
        goto cleanup;
    }

    whole_file_threshold = whole_file_threshold_kb > 0 ? (uint32_t)whole_file_threshold_kb * 1024U
                                                       : LITEPAK_WHOLE_FILE_THRESHOLD;
    cdc_avg_size = cdc_avg_kb > 0 ? (uint32_t)cdc_avg_kb * 1024U
                                  : LITEPAK_CDC_DEFAULT_AVG_SIZE;
    cdc_params = litepak_make_cdc_params(cdc_avg_size);
    start_ts = (double)GetTickCount64() / 1000.0;

    if (collect_files_recursive(&files, input_dir, "") != 0 || files.count == 0) {
        litepak_emit_log("输入目录没有可封包文件: %s", input_dir);
        goto cleanup;
    }
    qsort(files.items, files.count, sizeof(file_entry_t), cmp_file_entry_by_rel);

    total_files = (uint32_t)files.count;
    for (size_t i = 0; i < files.count; i++)
        logical_size += files.items[i].file_size;

    if (write_manifest_file(&files, manifest_path) != 0) {
        litepak_emit_log("manifest 写入失败: %s", manifest_path);
        goto cleanup;
    }

    litepak_emit_log("收集到 %u 个文件, 逻辑大小=%llu 字节", total_files, (unsigned long long)logical_size);
    litepak_emit_log("封包策略: 小文件阈值=%u KB | CDC(avg)=%u KB | 去重=%s | 压缩=%s",
                     whole_file_threshold / 1024, cdc_avg_size / 1024,
                     dedup_mode ? "on" : "off", compression ? compression : "auto");

    if (workers > 1) {
        int has_large_file = 0;
        for (size_t i = 0; i < files.count; i++) {
            if (files.items[i].file_size >= LITEPAK_LARGE_FILE_THRESHOLD) {
                has_large_file = 1;
                break;
            }
        }
        if (!has_large_file && logical_size <= 256ULL * 1024ULL * 1024ULL) {
            if (pack_preread_all(&files, workers, &preread_results) != 0) {
                litepak_emit_log("多线程预读失败");
                goto cleanup;
            }
            use_preread = preread_results != NULL;
            if (use_preread)
                litepak_emit_log("多线程预读: workers=%d", workers);
        } else {
            litepak_emit_log("多线程预读: 因大文件或总大小超过 256 MiB，回退为顺序读取");
        }
    }

    litepak_random_bytes(k2, 16);
    litepak_random_bytes(k6, 16);
    litepak_random_bytes(k8, 16);
    litepak_random_bytes(k10, 16);
    litepak_random_bytes(k9_plain, 16);
    litepak_random_bytes((uint8_t*)&seed, sizeof(seed));
    litepak_derive_pre_master_key_ex(k2, k8, true, pre_master_key);
    litepak_derive_full_master_key_ex(k2, k6, k8, k9_plain, k10, true, full_master_key);
    if (load_sign_seed(sign_key_path, sign_seed, &has_custom_sign) != 0) {
        litepak_emit_log("签名密钥读取失败: %s", sign_key_path ? sign_key_path : "");
        goto cleanup;
    }

    if (mkdir_parent_dirs(pak_path) != 0) {
        litepak_emit_log("输出目录创建失败: %s", pak_path);
        goto cleanup;
    }
    fp = litepak_fopen_utf8(pak_path, "w+b");
    if (!fp) {
        litepak_emit_log("输出文件创建失败: %s", pak_path);
        goto cleanup;
    }

    {
        uint8_t zero_header[LITEPAK_HEADER_SIZE] = {0};
        if (fwrite(zero_header, 1, sizeof(zero_header), fp) != sizeof(zero_header)) {
            litepak_emit_log("预留 header 失败");
            goto cleanup;
        }
    }

    {
        uint32_t k9_ref_start = chunk_refs.count;
        uint32_t k9_chunk_index;
        int reused = 0;
        entry_t k9_entry;
        uint8_t random8[8];
        uint8_t k9_file_id[16] = {0};
        char random_hex[17];
        char internal_name[128];

        if (add_chunk(fp, &chunk_dedup, &chunk_records,
                      k9_plain, sizeof(k9_plain),
                      CHUNK_KIND_K9,
                      pre_master_key, full_master_key,
                      "zlib", "", k9_file_id, false, false, true,
                      &k9_chunk_index, &reused) != 0) {
            litepak_emit_log("K9 内部条目写入失败");
            goto cleanup;
        }
        if (u32_list_push(&chunk_refs, k9_chunk_index) != 0)
            goto cleanup;

        litepak_random_bytes(random8, sizeof(random8));
        bytes_to_hex_upper(random8, sizeof(random8), random_hex);
        snprintf(internal_name, sizeof(internal_name), "__lpk_internal__/k9_%s", random_hex);

        memset(&k9_entry, 0, sizeof(k9_entry));
        litepak_path_hash_bytes(internal_name, k9_entry.hash_bytes);
        memcpy(k9_entry.file_id, k9_file_id, sizeof(k9_file_id));
        k9_entry.flags = ENTRY_KEY_PAYLOAD;
        k9_entry.original_size = sizeof(k9_plain);
        k9_entry.file_crc32c = litepak_crc32c(k9_plain, sizeof(k9_plain), 0);
        k9_entry.chunk_ref_start = k9_ref_start;
        k9_entry.chunk_count = 1;
        if (entry_list_push(&entries, &k9_entry) != 0)
            goto cleanup;
    }

    for (uint32_t i = 0; i < total_files; i++) {
        const file_entry_t* file = &files.items[i];
        buffer_t raw;
        uint8_t path_hash[16];
        uint8_t content_id[32];
        uint8_t file_id[16];
        uint8_t file_transform_flags;
        uint32_t ref_start;
        uint32_t chunk_count = 0;
        uint32_t reused_chunks = 0;
        entry_t entry;
        int use_whole_file;
        const char* layout;
        char mode_summary[128];
        uint64_t logical_stored = 0;

        if (use_preread) {
            raw = preread_results[i].raw;
            buffer_init(&preread_results[i].raw);
            memcpy(path_hash, preread_results[i].path_hash, sizeof(path_hash));
            memcpy(content_id, preread_results[i].content_id, sizeof(content_id));
        } else {
            buffer_init(&raw);
            if (read_file_all(file->full_path, &raw) != 0) {
                litepak_emit_log("读取文件失败: %s", file->full_path);
                buffer_free(&raw);
                goto cleanup;
            }
            litepak_path_hash_bytes(file->rel_path, path_hash);
            blake2b_full(raw.data, raw.len, content_id, 32, NULL, 0, (const uint8_t*)"LiteDedV6", 9);
        }

        litepak_derive_file_id(path_hash, content_id, (uint64_t)raw.len, file_id);
        file_transform_flags = litepak_select_transform_flags(file->rel_path, (uint64_t)raw.len, MODE_RAW);

        for (uint32_t k = 1; k < entries.count; k++) {
            if (memcmp(entries.items[k].hash_bytes, path_hash, 16) == 0) {
                litepak_emit_log("hash 碰撞: %s", file->rel_path);
                buffer_free(&raw);
                goto cleanup;
            }
        }

        ref_start = chunk_refs.count;
        use_whole_file = file->file_size <= whole_file_threshold;
        layout = use_whole_file ? "whole" : "cdc";

        if (dedup_mode && file_transform_flags == CHUNK_TRANSFORM_NONE) {
            uint32_t old_ref_start;
            uint32_t old_chunk_count;
            if (file_dedup_find(&file_dedup, content_id, &old_ref_start, &old_chunk_count)) {
                for (uint32_t r = 0; r < old_chunk_count; r++) {
                    if (u32_list_push(&chunk_refs, chunk_refs.items[old_ref_start + r]) != 0) {
                        buffer_free(&raw);
                        goto cleanup;
                    }
                }
                chunk_count = old_chunk_count;
                reused_chunks = old_chunk_count;
                exact_file_reused++;
                file_dedup_count++;
                layout = "file_dedup";
            }
        }

        if (chunk_count == 0) {
            if (use_whole_file) {
                uint32_t chunk_index;
                int reused = 0;
                if (add_chunk(fp, &chunk_dedup, &chunk_records,
                              raw.data, raw.len,
                              CHUNK_KIND_FILE,
                              pre_master_key, full_master_key,
                              compression ? compression : "auto",
                              file->rel_path, file_id,
                              dedup_mode, true, false,
                              &chunk_index, &reused) != 0) {
                    buffer_free(&raw);
                    litepak_emit_log("写入 chunk 失败: %s", file->rel_path);
                    goto cleanup;
                }
                if (u32_list_push(&chunk_refs, chunk_index) != 0) {
                    buffer_free(&raw);
                    goto cleanup;
                }
                chunk_count = 1;
                reused_chunks += reused ? 1U : 0U;
                whole_count++;
            } else {
                chunk_list_t chunks;
                chunk_list_init(&chunks);
                litepak_split_chunks_cdc(raw.data, raw.len, &cdc_params, &chunks);
                for (int c = 0; c < chunks.chunk_count; c++) {
                    uint32_t chunk_index;
                    int reused = 0;
                    if (add_chunk(fp, &chunk_dedup, &chunk_records,
                                  chunks.chunks[c], chunks.chunk_sizes[c],
                                  CHUNK_KIND_FILE,
                                  pre_master_key, full_master_key,
                                  compression ? compression : "auto",
                                  file->rel_path, file_id,
                                  dedup_mode, false, false,
                                  &chunk_index, &reused) != 0) {
                        chunk_list_free(&chunks);
                        buffer_free(&raw);
                        litepak_emit_log("写入 CDC chunk 失败: %s", file->rel_path);
                        goto cleanup;
                    }
                    if (u32_list_push(&chunk_refs, chunk_index) != 0) {
                        chunk_list_free(&chunks);
                        buffer_free(&raw);
                        goto cleanup;
                    }
                    chunk_count++;
                    reused_chunks += reused ? 1U : 0U;
                }
                chunk_list_free(&chunks);
                cdc_count++;
            }

            if (dedup_mode && file_transform_flags == CHUNK_TRANSFORM_NONE) {
                file_dedup_entry_t file_dedup_entry;
                memset(&file_dedup_entry, 0, sizeof(file_dedup_entry));
                memcpy(file_dedup_entry.content_id, content_id, 32);
                file_dedup_entry.chunk_ref_start = ref_start;
                file_dedup_entry.chunk_count = chunk_count;
                if (file_dedup_list_push(&file_dedup, &file_dedup_entry) != 0) {
                    buffer_free(&raw);
                    goto cleanup;
                }
            }
        }

        dedup_reused += reused_chunks;
        memset(&entry, 0, sizeof(entry));
        memcpy(entry.hash_bytes, path_hash, 16);
        memcpy(entry.file_id, file_id, 16);
        entry.flags = ENTRY_FILE;
        entry.original_size = file->file_size;
        entry.file_crc32c = use_preread ? preread_results[i].file_crc32c : litepak_crc32c(raw.data, raw.len, 0);
        entry.chunk_ref_start = ref_start;
        entry.chunk_count = chunk_count;
        if (entry_list_push(&entries, &entry) != 0) {
            buffer_free(&raw);
            goto cleanup;
        }

        summarize_file_chunks(&chunk_records, &chunk_refs, ref_start, chunk_count,
                              mode_summary, sizeof(mode_summary), &logical_stored);
        {
            double ratio = raw.len > 0 ? (double)logical_stored / (double)raw.len : 0.0;
            litepak_emit_log("[%u/%u] 写入 %s | layout=%s | mode=%s | chunk=%u | reused=%u | %llu/%zu (%.2f%%)",
                             i + 1, total_files, file->rel_path, layout, mode_summary,
                             chunk_count, reused_chunks,
                             (unsigned long long)logical_stored, raw.len, ratio * 100.0);
        }
        if (show_progress) {
            litepak_print_progress("封包处理中", (int)(i + 1), (int)total_files);
        }

        buffer_free(&raw);
    }

    {
        uint64_t data_end = (uint64_t)_ftelli64(fp);
        uint8_t idx_nonce[12];
        uint8_t idx_key[32];
        uint8_t header_bytes[LITEPAK_HEADER_SIZE];
        uint8_t trailer_bytes[LITEPAK_TRAILER_SIZE];
        uint8_t key_material_signature[32];
        uint16_t flags = FLAG_HAS_TRAILER | FLAG_CHUNK_INDEX | FLAG_FULL_VERIFY |
                         FLAG_AES_GCM | FLAG_ED25519_SIGNED | FLAG_INDEX_OBFUSCATED |
                         FLAG_WB_STRONG;
        uint64_t idx_offset;
        uint64_t idx_end;
        int use_index_compressed = 0;

        if (litepak_build_index(chunk_records.items, chunk_records.count,
                                chunk_refs.items, chunk_refs.count,
                                entries.items, entries.count,
                                k6, k10,
                                &cdc_params, whole_file_threshold,
                                seed, LITEPAK_V6_FEATURES, true, &index_plain) != 0) {
            litepak_emit_log("构建 index 失败");
            goto cleanup;
        }

        buffer_reserve(&obfuscated_index, index_plain.len);
        obfuscated_index.len = index_plain.len;
        litepak_obfuscate_index(index_plain.data, index_plain.len, seed, pre_master_key, obfuscated_index.data);

        if (compress_index_zstd(obfuscated_index.data, obfuscated_index.len, &index_payload) == 0) {
            use_index_compressed = 1;
            flags |= FLAG_INDEX_COMPRESSED;
        } else {
            buffer_append(&index_payload, obfuscated_index.data, obfuscated_index.len);
        }

        litepak_random_bytes(idx_nonce, sizeof(idx_nonce));
        litepak_derive_index_key(pre_master_key, (uint64_t)index_plain.len, idx_key);
        buffer_reserve(&index_cipher, index_payload.len + LITEPAK_GCM_TAG_SIZE);
        index_cipher.len = index_payload.len + LITEPAK_GCM_TAG_SIZE;
        if (aes_gcm_encrypt(index_payload.data, index_payload.len, idx_key, idx_nonce, index_cipher.data) != 0) {
            litepak_emit_log("加密 index 失败");
            goto cleanup;
        }

        idx_offset = (uint64_t)_ftelli64(fp);
        if (index_cipher.len > 0 && fwrite(index_cipher.data, 1, index_cipher.len, fp) != index_cipher.len) {
            litepak_emit_log("写入 index 失败");
            goto cleanup;
        }
        idx_end = (uint64_t)_ftelli64(fp);

        if (litepak_build_header(total_files, idx_offset, (uint64_t)index_cipher.len,
                                 (uint64_t)index_plain.len, idx_nonce, flags,
                                 k2, k8, seed, LITEPAK_V6_FEATURES, header_bytes) != 0) {
            litepak_emit_log("构建 header 失败");
            goto cleanup;
        }
        if (_fseeki64(fp, 0, SEEK_SET) != 0)
            goto cleanup;
        if (fwrite(header_bytes, 1, sizeof(header_bytes), fp) != sizeof(header_bytes))
            goto cleanup;
        if (_fseeki64(fp, (long long)idx_end, SEEK_SET) != 0)
            goto cleanup;

        litepak_compute_key_material_signature(k2, k6, k8, k9_plain, k10,
                                               LITEPAK_V6_FEATURES,
                                               pre_master_key, full_master_key,
                                               key_material_signature);
        if (litepak_build_trailer(fp, index_plain.data, index_plain.len,
                                  data_end, key_material_signature, true,
                                  has_custom_sign ? sign_seed : NULL,
                                  trailer_bytes) != 0) {
            litepak_emit_log("构建 trailer 失败");
            goto cleanup;
        }
        if (fwrite(trailer_bytes, 1, sizeof(trailer_bytes), fp) != sizeof(trailer_bytes)) {
            litepak_emit_log("写入 trailer 失败");
            goto cleanup;
        }

        litepak_emit_log("封包完成: 文件数=%u | 唯一chunk=%u | chunk引用=%u | index=%s | 输出=%s",
                         total_files,
                         chunk_records.count,
                         chunk_refs.count > 0 ? chunk_refs.count - 1 : 0,
                         use_index_compressed ? "zstd" : "raw",
                         pak_path);
        litepak_emit_log("封包汇总: whole=%u | cdc=%u | file-dedup=%u | reused=%u | 耗时=%.2fs",
                         whole_count, cdc_count, file_dedup_count, dedup_reused,
                         (double)GetTickCount64() / 1000.0 - start_ts);
    }

    rc = 0;

cleanup:
    if (fp)
        fclose(fp);
    buffer_free(&index_plain);
    buffer_free(&obfuscated_index);
    buffer_free(&index_payload);
    buffer_free(&index_cipher);
    file_list_free(&files);
    chunk_record_list_free(&chunk_records);
    entry_list_free(&entries);
    u32_list_free(&chunk_refs);
    chunk_dedup_list_free(&chunk_dedup);
    file_dedup_list_free(&file_dedup);
    pack_preread_results_free(preread_results, total_files);
    litepak_secure_bzero(sign_seed, sizeof(sign_seed));
    return rc;
}

int litepak_pack(const char* input_dir, const char* pak_path, const char* manifest_path,
                 bool dedup_mode, bool show_progress, const char* compression,
                 int cdc_avg_kb, int whole_file_threshold_kb, const char* sign_key_path) {
    return litepak_pack_ex(input_dir, pak_path, manifest_path, dedup_mode, show_progress,
                           compression, cdc_avg_kb, whole_file_threshold_kb, sign_key_path, 1);
}
