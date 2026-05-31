/*
 * LitePAK v6 工具 - C语言实现
 * 一比一复刻 LitePAK_tool.py 的全部功能
 */
#ifndef LITEPAK_H
#define LITEPAK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ============================================================================ */

#define LITEPAK_MAGIC           "LitePAK"
#define LITEPAK_MAGIC_LEN       7
#define LITEPAK_VERSION         6
#define LITEPAK_HEADER_SIZE     96
#define LITEPAK_TRAILER_SIZE    192
#define LITEPAK_PATH_HASH_SIZE  16
#define LITEPAK_FILE_ID_SIZE    16
#define LITEPAK_CHUNK_HASH_SIZE 32
#define LITEPAK_NONCE_SIZE      12
#define LITEPAK_GCM_TAG_SIZE    16
#define LITEPAK_FRAGMENT_SIZE   16
#define LITEPAK_IO_CHUNK_SIZE   (4 * 1024 * 1024)
#define LITEPAK_LARGE_FILE_THRESHOLD (64 * 1024 * 1024)
#define LITEPAK_WHOLE_FILE_THRESHOLD (1 * 1024 * 1024)
#define LITEPAK_CDC_DEFAULT_AVG_SIZE (128 * 1024)
#define LITEPAK_SEGMENT_BOUNDARY     4096
#define LITEPAK_SEGMENT_DEPEND_LEN   128
#define LITEPAK_INDEX_HASH_SIZE      16
#define LITEPAK_CHUNK_CACHE_MAX_ENTRIES 1024
#define LITEPAK_CHUNK_CACHE_MAX_BYTES   (128 * 1024 * 1024)

/* 压缩模式 */
#define MODE_RAW    0xC0
#define MODE_ZLIB   0xC1
#define MODE_ZSTD   0xC2
#define MODE_LZMA   0xC3

/* Header flags */
#define FLAG_HAS_TRAILER        0x1000
#define FLAG_INDEX_COMPRESSED   0x2000
#define FLAG_CHUNK_INDEX        0x4000
#define FLAG_FULL_VERIFY        0x8000
#define FLAG_AES_GCM            0x0100
#define FLAG_ED25519_SIGNED     0x0200
#define FLAG_INDEX_OBFUSCATED   0x0400
#define FLAG_WB_STRONG          0x0800

#define LITEPAK_V6_FEATURE_KEY_SIG       0x00000001u
#define LITEPAK_V6_FEATURE_KEYED_INDEX   0x00000002u
#define LITEPAK_V6_FEATURE_FILE_ID_MASK  0x00000004u
#define LITEPAK_V6_FEATURE_ARX_LAYER     0x00000008u
#define LITEPAK_V6_FEATURE_FEISTEL_HEAD  0x00000010u
#define LITEPAK_V6_FEATURES (LITEPAK_V6_FEATURE_KEY_SIG | LITEPAK_V6_FEATURE_KEYED_INDEX | \
                             LITEPAK_V6_FEATURE_FILE_ID_MASK | LITEPAK_V6_FEATURE_ARX_LAYER | \
                             LITEPAK_V6_FEATURE_FEISTEL_HEAD)

#define CHUNK_TRANSFORM_NONE     0x00
#define CHUNK_TRANSFORM_ARX      0x01
#define CHUNK_TRANSFORM_FEISTEL  0x02

/* Entry flags */
#define ENTRY_FILE              0xA0
#define ENTRY_DIRECTORY         0xA1
#define ENTRY_KEY_PAYLOAD       0xA2

/* Chunk kinds */
#define CHUNK_KIND_FILE         0x31
#define CHUNK_KIND_K9           0x39

/* LZMA 参数 */
#define LITEPAK_LZMA_LC         8
#define LITEPAK_LZMA_LP         0
#define LITEPAK_LZMA_PB         4
#define LITEPAK_LZMA_DICT_SIZE  (1 << 27)

/* ============================================================================
 * 数据结构
 * ============================================================================ */

typedef struct {
    uint32_t min_size;
    uint32_t avg_size;
    uint32_t max_size;
    uint64_t mask_early;
    uint64_t mask_late;
} cdc_params_t;

typedef struct {
    uint8_t  version;
    uint16_t flags;
    uint32_t file_count;
    uint64_t index_offset;
    uint64_t index_encrypted_sz;
    uint64_t index_plain_sz;
    uint8_t  index_nonce[LITEPAK_NONCE_SIZE];
    uint8_t  k2[16];
    uint8_t  k8[16];
    uint16_t seed;
    uint32_t v6_features;
    bool     has_trailer;
    bool     index_compressed;
    bool     chunk_index;
    bool     aes_gcm;
    bool     ed25519_signed;
    bool     index_obfuscated;
    bool     strong_wb;
} litepak_header_t;

typedef struct {
    uint8_t  chunk_hash[LITEPAK_CHUNK_HASH_SIZE];
    uint64_t original_size;
    uint64_t stored_size;
    uint64_t data_offset;
    uint8_t  data_nonce[LITEPAK_NONCE_SIZE];
    uint32_t chunk_crc32c;
    uint8_t  chunk_kind;
    uint8_t  mode;
    uint8_t  transform_flags;
    uint8_t  file_id[LITEPAK_FILE_ID_SIZE];
} chunk_record_t;

typedef struct {
    uint8_t  hash_bytes[LITEPAK_PATH_HASH_SIZE];
    uint8_t  file_id[LITEPAK_FILE_ID_SIZE];
    uint8_t  flags;
    uint64_t original_size;
    uint32_t file_crc32c;
    uint32_t chunk_ref_start;
    uint32_t chunk_count;
} entry_t;

typedef struct {
    entry_t*        entries;
    uint32_t        entry_count;
    chunk_record_t* chunk_records;
    uint32_t        chunk_record_count;
    uint32_t*       chunk_refs;
    uint32_t        chunk_ref_count;
    uint8_t         k6[LITEPAK_FRAGMENT_SIZE];
    uint8_t         k10[LITEPAK_FRAGMENT_SIZE];
    entry_t*        k9_entry;
    uint32_t        whole_file_threshold;
    uint32_t        cdc_min_size;
    uint32_t        cdc_avg_size;
    uint32_t        cdc_max_size;
} index_meta_t;

/* 动态字节缓冲区 */
typedef struct {
    uint8_t* data;
    size_t   len;
    size_t   cap;
    bool     failed;
} buffer_t;

/* 文件收集条目 */
typedef struct {
    char*    full_path;
    char*    rel_path;
    uint64_t file_size;
} file_entry_t;

/* chunk 去重映射 */
typedef struct {
    uint8_t  chunk_kind;
    uint8_t  chunk_hash[LITEPAK_CHUNK_HASH_SIZE];
    uint64_t original_size;
    uint32_t chunk_crc32c;
    uint32_t chunk_index;
} chunk_dedup_entry_t;

/* 文件去重映射 */
typedef struct {
    uint8_t  content_id[32];
    uint32_t chunk_ref_start;
    uint32_t chunk_count;
} file_dedup_entry_t;

/* ============================================================================
 * 函数声明 - 工具函数
 * ============================================================================ */

/* CRC */
uint32_t litepak_crc32c(const uint8_t* data, size_t len, uint32_t seed);
uint8_t  litepak_crc8(const uint8_t* data, size_t len);

/* 格式化 */
void litepak_format_size(uint64_t n, char* buf, size_t buf_sz);
void litepak_format_duration(double seconds, char* buf, size_t buf_sz);

/* 进度条 */
void litepak_emit_log(const char* fmt, ...);
void litepak_print_progress(const char* prefix, int current, int total);

/* 路径 */
void litepak_normalize_relpath(const char* input, char* output, size_t out_sz);
void litepak_path_hash_bytes(const char* text, uint8_t out[LITEPAK_PATH_HASH_SIZE]);
void litepak_chunk_hash_bytes(const uint8_t* data, size_t len, uint8_t out[LITEPAK_CHUNK_HASH_SIZE]);
void litepak_hash16_personal(const uint8_t* data, size_t len, const uint8_t* person, size_t person_len, uint8_t out[LITEPAK_INDEX_HASH_SIZE]);
int litepak_utf8_to_wide_dup(const char* text, wchar_t** out_wide);
int litepak_wide_to_utf8_dup(const wchar_t* text, char** out_utf8);
FILE* litepak_fopen_utf8(const char* path, const char* mode);
int litepak_mkdir_utf8(const char* path);

/* Buffer */
void buffer_init(buffer_t* buf);
void buffer_free(buffer_t* buf);
int buffer_reserve(buffer_t* buf, size_t cap);
int buffer_append(buffer_t* buf, const void* data, size_t len);
int buffer_append_u8(buffer_t* buf, uint8_t val);
int buffer_append_u16le(buffer_t* buf, uint16_t val);
int buffer_append_u32le(buffer_t* buf, uint32_t val);
int buffer_append_u64le(buffer_t* buf, uint64_t val);

/* 随机数 */
void litepak_random_bytes(uint8_t* out, size_t len);
void litepak_wait_for_any_key(void);

/* ============================================================================
 * 函数声明 - BLAKE2b
 * ============================================================================ */

typedef struct {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t  buf[128];
    size_t   buflen;
    size_t   outlen;
} blake2b_state;

int blake2b_init(blake2b_state* S, size_t outlen);
int blake2b_init_key(blake2b_state* S, size_t outlen, const void* key, size_t keylen);
int blake2b_init_param(blake2b_state* S, size_t outlen, const void* key, size_t keylen,
                       const uint8_t* personal, size_t personal_len);
int blake2b_update(blake2b_state* S, const void* in, size_t inlen);
int blake2b_final(blake2b_state* S, void* out, size_t outlen);

void blake2b_full(const void* in, size_t inlen, void* out, size_t outlen,
                  const void* key, size_t keylen,
                  const uint8_t* personal, size_t personal_len);

/* ============================================================================
 * 函数声明 - AES-256-GCM
 * ============================================================================ */

int aes_gcm_encrypt(const uint8_t* plaintext, size_t pt_len,
                    const uint8_t key[32], const uint8_t nonce[12],
                    uint8_t* ciphertext_and_tag);

int aes_gcm_decrypt(const uint8_t* ciphertext_and_tag, size_t ct_tag_len,
                    const uint8_t key[32], const uint8_t nonce[12],
                    uint8_t* plaintext);

/* ============================================================================
 * 函数声明 - Ed25519
 * ============================================================================ */

void ed25519_sign(const uint8_t* message, size_t msg_len,
                  const uint8_t seed[32], uint8_t signature[64]);

int ed25519_verify(const uint8_t* message, size_t msg_len,
                   const uint8_t* public_key, const uint8_t signature[64]);

void ed25519_seed_to_public(const uint8_t seed[32], uint8_t public_key[32]);

/* ============================================================================
 * 函数声明 - LitePRNG
 * ============================================================================ */

typedef struct {
    uint32_t mt[624];
    int      mti;
} lite_prng_t;

void lite_prng_init(lite_prng_t* rng, uint32_t seed);
uint32_t lite_prng_next_u32(lite_prng_t* rng);
void lite_prng_permutation(lite_prng_t* rng, uint32_t* perm, int n);
void lite_prng_inverse_permutation(const uint32_t* perm, uint32_t* inv, int n);

/* ============================================================================
 * 函数声明 - 索引混淆
 * ============================================================================ */

void litepak_obfuscate_index(const uint8_t* data, size_t len, uint16_t seed,
                             const uint8_t pre_master_key[32], uint8_t* out);
void litepak_deobfuscate_index(const uint8_t* data, size_t len, uint16_t seed,
                               const uint8_t pre_master_key[32], uint8_t* out);

/* 条目字段混淆 */
void litepak_obfuscate_entry(entry_t* e, uint16_t seed, uint32_t v6_features);
void litepak_deobfuscate_entry(entry_t* e, uint16_t seed, uint32_t v6_features);

/* ============================================================================
 * 函数声明 - 密钥派生
 * ============================================================================ */

void litepak_derive_pre_master_key(const uint8_t k2[16], const uint8_t k8[16], uint8_t out[32]);
void litepak_derive_pre_master_key_ex(const uint8_t k2[16], const uint8_t k8[16],
                                       bool strong_wb, uint8_t out[32]);
void litepak_derive_full_master_key(const uint8_t k2[16], const uint8_t k6[16],
                                     const uint8_t k8[16], const uint8_t k9[16],
                                     const uint8_t k10[16], uint8_t out[32]);
void litepak_derive_full_master_key_ex(const uint8_t k2[16], const uint8_t k6[16],
                                        const uint8_t k8[16], const uint8_t k9[16],
                                        const uint8_t k10[16], bool strong_wb, uint8_t out[32]);
void litepak_derive_index_key(const uint8_t pre_master_key[32], uint64_t plain_sz, uint8_t out[32]);
void litepak_derive_k9_chunk_key(const uint8_t pre_master_key[32], const uint8_t nonce[12],
                                  uint64_t plain_sz, uint8_t chunk_kind, uint8_t out[32]);
void litepak_derive_file_chunk_key(const uint8_t full_master_key[32], const uint8_t nonce[12],
                                    uint64_t plain_sz, uint8_t chunk_kind, uint8_t out[32]);
void litepak_derive_segment_open_key(const uint8_t master_key[32], const uint8_t nonce[12],
                                      uint8_t chunk_kind, uint8_t out[32]);
void litepak_derive_segment_read_key(const uint8_t master_key[32], const uint8_t nonce[12],
                                      uint64_t remaining_len, const uint8_t open_key[32],
                                      const uint8_t* first_plain, uint8_t chunk_kind, uint8_t out[32]);
void litepak_derive_file_id(const uint8_t path_hash[16], const uint8_t content_id[32],
                            uint64_t original_size, uint8_t out[16]);
void litepak_compute_key_material_signature(const uint8_t k2[16], const uint8_t k6[16],
                                             const uint8_t k8[16], const uint8_t k9[16],
                                             const uint8_t k10[16], uint32_t v6_features,
                                             const uint8_t pre_master_key[32],
                                             const uint8_t full_master_key[32], uint8_t out[32]);
void litepak_apply_arx_transform(uint8_t* data, size_t len, const uint8_t full_master_key[32],
                                  const uint8_t file_id[16], const uint8_t nonce[12], uint64_t original_size);
void litepak_apply_feistel_transform(uint8_t* data, size_t len, const uint8_t full_master_key[32],
                                      const uint8_t file_id[16], const uint8_t nonce[12], uint64_t original_size);
uint8_t litepak_select_transform_flags(const char* rel_path, uint64_t original_size, uint8_t mode);

/* 分段加密/解密 */
int litepak_segmented_encrypt(const uint8_t* plain_packed, size_t packed_len,
                               const uint8_t master_key[32], const uint8_t nonce[12],
                               uint8_t chunk_kind, uint64_t original_size, bool is_k9,
                               buffer_t* out);
int litepak_segmented_decrypt(const uint8_t* enc_data, size_t enc_len,
                               const uint8_t master_key[32], const uint8_t nonce[12],
                               uint64_t original_size, uint8_t chunk_kind, bool is_k9,
                               buffer_t* out);

/* ============================================================================
 * 函数声明 - 压缩
 * ============================================================================ */

int litepak_compress_chunk(const uint8_t* data, size_t len, const char* method,
                           bool whole_file_mode, buffer_t* out);
int litepak_decompress_chunk(const uint8_t* data, size_t len, size_t expected_size, buffer_t* out);
const char* litepak_mode_name(uint8_t mode);

/* ============================================================================
 * 函数声明 - CDC 分块
 * ============================================================================ */

cdc_params_t litepak_make_cdc_params(uint32_t avg_size);

typedef struct {
    uint8_t** chunks;
    size_t*   chunk_sizes;
    int       chunk_count;
    int       chunk_cap;
} chunk_list_t;

void chunk_list_init(chunk_list_t* cl);
void chunk_list_free(chunk_list_t* cl);
void chunk_list_add(chunk_list_t* cl, const uint8_t* data, size_t len);

void litepak_split_chunks_cdc(const uint8_t* data, size_t len,
                               const cdc_params_t* params, chunk_list_t* out);

/* ============================================================================
 * 函数声明 - Header/Index/Trailer
 * ============================================================================ */

int litepak_read_header(FILE* fp, litepak_header_t* header);
int litepak_decrypt_index(FILE* fp, const litepak_header_t* header,
                          const uint8_t pre_master_key[32], buffer_t* out_plain);
int litepak_parse_index(const uint8_t* index_plain, size_t plain_len,
                        bool strict, uint16_t seed, uint32_t v6_features, index_meta_t* meta);
void litepak_free_index_meta(index_meta_t* meta);

int litepak_build_header(uint32_t file_count, uint64_t idx_offset,
                         uint64_t idx_enc_sz, uint64_t idx_plain_sz,
                         const uint8_t idx_nonce[12], uint16_t flags,
                         const uint8_t k2[16], const uint8_t k8[16],
                         uint16_t seed, uint32_t v6_features, uint8_t out[LITEPAK_HEADER_SIZE]);

int litepak_build_index(const chunk_record_t* chunk_records, uint32_t chunk_count,
                        const uint32_t* chunk_refs, uint32_t ref_count,
                        const entry_t* entries, uint32_t entry_count,
                        const uint8_t k6[16], const uint8_t k10[16],
                        const cdc_params_t* cdc_params, uint32_t whole_file_threshold,
                        uint16_t seed, uint32_t v6_features, bool obfuscate_entries, buffer_t* out);

int litepak_build_trailer(FILE* fp, const uint8_t* index_plain, size_t index_plain_len,
                          uint64_t data_end, const uint8_t key_material_signature[32],
                          bool sign, const uint8_t* sign_key,
                          uint8_t out[LITEPAK_TRAILER_SIZE]);

int litepak_read_trailer(FILE* fp, const uint8_t* index_plain, size_t index_plain_len,
                         const litepak_header_t* header, const uint8_t* verify_public_key,
                         const uint8_t expected_key_material_signature[32]);

/* Ed25519 签名/验证 trailer: signing uses a private seed, verification uses a public key. */
#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
void litepak_sign_trailer(const uint8_t* trailer_data, size_t len,
                          const uint8_t* sign_key, uint8_t signature[64]);
#endif
int litepak_verify_trailer_sig(const uint8_t* trailer_data, size_t len,
                               const uint8_t signature[64], const uint8_t* verify_public_key);

/* ============================================================================
 * 函数声明 - VFS只读接口
 * ============================================================================ */

typedef struct litepak_vfs_handle litepak_vfs_handle_t;

typedef struct {
    uint8_t  hash_bytes[LITEPAK_PATH_HASH_SIZE];
    uint8_t  flags;
    uint64_t original_size;
    const char* rel_path;
} litepak_vfs_entry_info_t;

int litepak_vfs_open_path(const char* pak_path, const char* manifest_path,
                          litepak_vfs_handle_t** out_handle);
int litepak_vfs_open_memory(const void* data, size_t size, const char* archive_tag,
                            const char* manifest_path, litepak_vfs_handle_t** out_handle);
void litepak_vfs_close(litepak_vfs_handle_t* handle);
int litepak_vfs_get_entry_count(litepak_vfs_handle_t* handle, size_t* out_count);
int litepak_vfs_get_entry(litepak_vfs_handle_t* handle, size_t index,
                          litepak_vfs_entry_info_t* out_info);
int litepak_vfs_read_file_by_hash(litepak_vfs_handle_t* handle,
                                  const uint8_t hash[LITEPAK_PATH_HASH_SIZE],
                                  uint8_t** out_data, size_t* out_size);
int litepak_vfs_query_file_by_hash(litepak_vfs_handle_t* handle,
                                   const uint8_t hash[LITEPAK_PATH_HASH_SIZE],
                                   uint64_t* out_size);
void litepak_vfs_free_bytes(uint8_t* data);

/* ============================================================================
 * 函数声明 - 高层操作
 * ============================================================================ */

/* pack sign_key_path: 32-byte Ed25519 private seed file. */
int litepak_pack(const char* input_dir, const char* pak_path, const char* manifest_path,
                 bool dedup_mode, bool show_progress, const char* compression,
                 int cdc_avg_kb, int whole_file_threshold_kb, const char* sign_key_path);
int litepak_pack_ex(const char* input_dir, const char* pak_path, const char* manifest_path,
                    bool dedup_mode, bool show_progress, const char* compression,
                    int cdc_avg_kb, int whole_file_threshold_kb, const char* sign_key_path,
                    int workers);

int litepak_unpack(const char* pak_path, const char* output_dir,
                   const char* manifest_path, bool show_progress,
                   bool verify_trailer);
int litepak_unpack_ex(const char* pak_path, const char* output_dir,
                      const char* manifest_path, bool show_progress,
                      bool verify_trailer, int workers);
/* read-side verify_key_path: 32-byte Ed25519 public key file. */
int litepak_unpack_ex2(const char* pak_path, const char* output_dir,
                       const char* manifest_path, bool show_progress,
                       bool verify_trailer, int workers, const char* verify_key_path);

int litepak_info(const char* pak_path);
int litepak_info_ex(const char* pak_path, const char* verify_key_path);
int litepak_verify(const char* pak_path);
int litepak_verify_ex(const char* pak_path, const char* verify_key_path);
int litepak_extract_by_name(const char* pak_path, const char* rel_name,
                            const char* output_path);

void litepak_auto_drag_mode(int argc, char** argv);

/* ============================================================================
 * GEAR 表（CDC 用）
 * ============================================================================ */

const uint64_t* litepak_get_gear_table(void);

/* Root material is internal and materialized on demand. */

#ifdef __cplusplus
}
#endif

#endif /* LITEPAK_H */
