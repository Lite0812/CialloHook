/*
 * LitePAK 结构层读写
 *
 * 这一层只负责：
 * 1. header / index / trailer 的序列化与反序列化
 * 2. 索引区的解密、解压与完整性校验
 * 3. trailer 的摘要链与 Ed25519 签名校验
 */
#include "litepak.h"
#include "litepak_internal.h"
#include <stdlib.h>
#include <string.h>

#include "../../third/zstd/zstd.h"

#ifndef LITEPAK_FORMAT_CFF
#define LITEPAK_FORMAT_CFF 1
#endif

#if LITEPAK_FORMAT_CFF
static uint32_t litepak_cff_mix32(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}

static int litepak_cff_opaque_true(uint32_t seed) {
    volatile uint32_t x = seed | 1u;
    return ((x * (x + 1u)) & 1u) == 0u;
}

static int litepak_cff_opaque_false(uint32_t seed) {
    volatile uint32_t x = seed | 1u;
    return (((x * x) + x + 1u) & 1u) == 0u;
}

static uint32_t litepak_cff_select_state(uint32_t seed, uint32_t real_state, uint32_t bogus_state) {
    volatile uint32_t guard = litepak_cff_mix32(seed ^ real_state ^ (bogus_state << 1));
    uint32_t selected = litepak_cff_opaque_true(guard) ? real_state : bogus_state;
    if (litepak_cff_opaque_false(guard ^ 0xD1CEB00Cu))
        selected ^= litepak_cff_mix32(guard);
    return selected;
}

static uint32_t litepak_cff_state_mask(uint32_t key) {
    return litepak_cff_mix32(key ^ 0xA9E3779Bu) ^ 0x5C3D2E1Fu;
}

static uint32_t litepak_cff_encode_state(uint32_t state, uint32_t key) {
    return state ^ litepak_cff_state_mask(key);
}

static uint32_t litepak_cff_decode_state(uint32_t state, uint32_t key) {
    return state ^ litepak_cff_state_mask(key);
}

static uint32_t litepak_cff_launder_state(uint32_t state, uint32_t key) {
    volatile uint32_t encoded = litepak_cff_encode_state(state, key);
    uint32_t decoded = litepak_cff_decode_state(encoded, key);
    if (litepak_cff_opaque_false(key ^ decoded ^ 0xB16B00B5u))
        decoded ^= litepak_cff_state_mask(key ^ 0x13579BDFu);
    return decoded;
}

#define LITEPAK_CFF_NEXT(seed, real_state, bogus_state) \
    litepak_cff_select_state((uint32_t)(seed), (uint32_t)(real_state), (uint32_t)(bogus_state))
#define LITEPAK_CFF_FAKE(seed, bogus_state, real_state) \
    (litepak_cff_opaque_false((uint32_t)(seed)) ? (uint32_t)(bogus_state) : (uint32_t)(real_state))
#define LITEPAK_CFF_ENCODE(state, key) \
    litepak_cff_encode_state((uint32_t)(state), (uint32_t)(key))
#define LITEPAK_CFF_DECODE(state, key) \
    litepak_cff_decode_state((uint32_t)(state), (uint32_t)(key))
#define LITEPAK_CFF_DISPATCH(state, key) \
    litepak_cff_launder_state((uint32_t)(state), (uint32_t)(key))
#else
#define LITEPAK_CFF_NEXT(seed, real_state, bogus_state) ((uint32_t)(real_state))
#define LITEPAK_CFF_FAKE(seed, bogus_state, real_state) ((uint32_t)(real_state))
#define LITEPAK_CFF_ENCODE(state, key) ((uint32_t)(state))
#define LITEPAK_CFF_DECODE(state, key) ((uint32_t)(state))
#define LITEPAK_CFF_DISPATCH(state, key) ((uint32_t)(state))
#endif

/* ============================================================================
 * 基础的小端读写辅助
 * ============================================================================ */

static uint16_t read_u16le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read_u64le(const uint8_t* p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

#if LITEPAK_FORMAT_CFF
static uint32_t read_u32le_cff(const uint8_t* p, uint32_t salt) {
    uint32_t v = read_u32le(p);
    volatile uint32_t mask = litepak_cff_mix32(v ^ salt ^ 0xCFF32C6Du);
    return (v ^ mask) ^ mask;
}

static uint64_t read_u64le_cff(const uint8_t* p, uint32_t salt) {
    uint64_t v = read_u64le(p);
    volatile uint32_t lo_mask = litepak_cff_mix32((uint32_t)v ^ salt ^ 0xCFF64A11u);
    volatile uint32_t hi_mask = litepak_cff_mix32((uint32_t)(v >> 32) ^ salt ^ 0xCFF64B22u);
    return (v ^ (uint64_t)lo_mask ^ ((uint64_t)hi_mask << 32)) ^ (uint64_t)lo_mask ^ ((uint64_t)hi_mask << 32);
}
#else
#define read_u32le_cff(p, salt) read_u32le((p))
#define read_u64le_cff(p, salt) read_u64le((p))
#endif

static void write_u16le(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static void write_u32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void write_u64le(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32);
    p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48);
    p[7] = (uint8_t)(v >> 56);
}

static int read_exact(FILE* fp, void* buf, size_t len) {
    return fread(buf, 1, len, fp) == len ? 0 : -1;
}

static int hash16_eq(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, LITEPAK_INDEX_HASH_SIZE) == 0;
}

/*
 * 索引区如果开启了 zstd 压缩，这里负责把密文解开后的 payload 还原成明文索引。
 * 由于某些帧不一定能直接拿到原始大小，这里按 Python 版策略做渐进扩容。
 */
static int decompress_zstd_payload(const uint8_t* data, size_t len, size_t expected_plain, buffer_t* out) {
    size_t alloc_sz = expected_plain;
    if (alloc_sz == 0 || alloc_sz == (size_t)ZSTD_CONTENTSIZE_UNKNOWN || alloc_sz == (size_t)ZSTD_CONTENTSIZE_ERROR) {
        unsigned long long frame_size = ZSTD_getFrameContentSize(data, len);
        if (frame_size != ZSTD_CONTENTSIZE_UNKNOWN && frame_size != ZSTD_CONTENTSIZE_ERROR) {
            alloc_sz = (size_t)frame_size;
        } else {
            alloc_sz = len ? len * 4 : 256;
        }
    }
    if (alloc_sz < 256) alloc_sz = 256;

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

/* ============================================================================
 * Header 读写
 * ============================================================================ */

LITEPAK_PROTECTED_BEGIN
static LITEPAK_PROTECTED_NOINLINE int litepak_read_header_impl_protected(FILE* fp, litepak_header_t* header) {
    enum {
        H_READ = 0x2100u,
        H_MAGIC = 0x2101u,
        H_CLEAR = 0x2102u,
        H_VERSION = 0x2103u,
        H_FLAGS = 0x2104u,
        H_FIELDS = 0x2105u,
        H_CRC = 0x2106u,
        H_KEYS_FEATURES = 0x2107u,
        H_BOOLEANS = 0x2108u,
        H_DONE = 0x2109u,
        H_FAIL = 0x210Au,
        H_BOGUS_A = 0x2F01u,
        H_BOGUS_B = 0x2F02u
    };
    uint8_t raw[LITEPAK_HEADER_SIZE];
    uint32_t after_bogus = H_FAIL;
    uint32_t raw_crc = 0;
    uint32_t calc_crc = 0;
    uint32_t state_key = litepak_cff_mix32(0x48445231u ^ LITEPAK_HEADER_SIZE);
    volatile uint32_t state = H_READ;
    volatile uint32_t cff_noise = 0x48445231u;
    uint32_t cff_shadow = 0x6C706B48u;

    for (;;) {
        switch (LITEPAK_CFF_DISPATCH(state, state_key)) {
        case H_READ:
            if (read_exact(fp, raw, sizeof(raw)) != 0) {
                state = H_FAIL;
                break;
            }
            after_bogus = H_MAGIC;
            cff_noise ^= raw[0] | ((uint32_t)raw[7] << 8) | ((uint32_t)raw[92] << 16);
            state = LITEPAK_CFF_NEXT(cff_noise, H_MAGIC, H_BOGUS_A);
            break;

        case H_MAGIC:
            if (memcmp(raw, LITEPAK_MAGIC, LITEPAK_MAGIC_LEN) != 0) {
                state = H_FAIL;
                break;
            }
            after_bogus = H_CLEAR;
            state = LITEPAK_CFF_FAKE(cff_noise ^ 0x411Cu, H_BOGUS_B, H_CLEAR);
            break;

        case H_CLEAR:
            memset(header, 0, sizeof(*header));
            state = H_VERSION;
            break;

        case H_VERSION:
            header->version = raw[7];
            if (header->version != LITEPAK_VERSION) {
                state = H_FAIL;
                break;
            }
            after_bogus = H_FLAGS;
            state = LITEPAK_CFF_NEXT(cff_noise ^ header->version, H_FLAGS, H_BOGUS_B);
            break;

        case H_FLAGS:
            header->flags = read_u16le(raw + 8);
            {
                const uint16_t required_flags = FLAG_HAS_TRAILER | FLAG_CHUNK_INDEX | FLAG_FULL_VERIFY |
                                                FLAG_AES_GCM | FLAG_ED25519_SIGNED |
                                                FLAG_INDEX_OBFUSCATED | FLAG_WB_STRONG;
                if ((header->flags & required_flags) != required_flags) {
                    state = H_FAIL;
                    break;
                }
            }
            state = H_FIELDS;
            break;

        case H_FIELDS:
            header->file_count = read_u32le(raw + 10);
            header->index_offset = read_u64le(raw + 14);
            header->index_encrypted_sz = read_u64le(raw + 22);
            header->index_plain_sz = read_u64le(raw + 30);
            memcpy(header->index_nonce, raw + 38, LITEPAK_NONCE_SIZE);
            after_bogus = H_CRC;
            cff_noise ^= litepak_cff_mix32(header->file_count ^ (uint32_t)header->index_plain_sz);
            state = LITEPAK_CFF_NEXT(cff_noise, H_CRC, H_BOGUS_A);
            break;

        case H_CRC:
            calc_crc = litepak_crc32c(raw, 50, 0);
            raw_crc = read_u32le(raw + 50);
            if (calc_crc != raw_crc) {
                state = H_FAIL;
                break;
            }
            state = H_KEYS_FEATURES;
            break;

        case H_KEYS_FEATURES:
            memcpy(header->k2, raw + 54, 16);
            memcpy(header->k8, raw + 70, 16);
            header->seed = read_u16le(raw + 86);
            header->v6_features = read_u32le(raw + 88);
            if ((header->v6_features & LITEPAK_V6_FEATURES) != LITEPAK_V6_FEATURES) {
                state = H_FAIL;
                break;
            }
            if (read_u32le(raw + 92) != LITEPAK_HEADER_SIZE) {
                state = H_FAIL;
                break;
            }
            after_bogus = H_BOOLEANS;
            state = LITEPAK_CFF_NEXT(cff_noise ^ header->seed ^ header->v6_features, H_BOOLEANS, H_BOGUS_B);
            break;

        case H_BOOLEANS:
            header->has_trailer = (header->flags & FLAG_HAS_TRAILER) != 0;
            header->index_compressed = (header->flags & FLAG_INDEX_COMPRESSED) != 0;
            header->chunk_index = (header->flags & FLAG_CHUNK_INDEX) != 0;
            header->aes_gcm = (header->flags & FLAG_AES_GCM) != 0;
            header->ed25519_signed = (header->flags & FLAG_ED25519_SIGNED) != 0;
            header->index_obfuscated = (header->flags & FLAG_INDEX_OBFUSCATED) != 0;
            header->strong_wb = (header->flags & FLAG_WB_STRONG) != 0;
            state = H_DONE;
            break;

        case H_BOGUS_A:
            cff_shadow ^= litepak_cff_mix32((uint32_t)raw[3] ^ (uint32_t)raw[50] ^ cff_noise);
            cff_noise = litepak_cff_mix32(cff_shadow ^ 0x10203040u);
            state = after_bogus;
            break;

        case H_BOGUS_B:
            cff_shadow += litepak_cff_mix32((uint32_t)raw[8] ^ ((uint32_t)raw[9] << 8) ^ cff_noise);
            cff_noise ^= litepak_cff_mix32(cff_shadow ^ 0x55667788u);
            state = after_bogus;
            break;

        case H_DONE:
            return 0;

        case H_FAIL:
        default:
            return -1;
        }
    }
}
LITEPAK_PROTECTED_END

int litepak_read_header(FILE* fp, litepak_header_t* header) {
    if (litepak_codecrypt_ensure_decrypted() != 0)
        return -1;
    return litepak_read_header_impl_protected(fp, header);
}

int litepak_build_header(uint32_t file_count, uint64_t idx_offset,
                         uint64_t idx_enc_sz, uint64_t idx_plain_sz,
                         const uint8_t idx_nonce[12], uint16_t flags,
                         const uint8_t k2[16], const uint8_t k8[16],
                         uint16_t seed, uint32_t v6_features, uint8_t out[LITEPAK_HEADER_SIZE]) {
    memset(out, 0, LITEPAK_HEADER_SIZE);
    memcpy(out, LITEPAK_MAGIC, LITEPAK_MAGIC_LEN);
    out[7] = LITEPAK_VERSION;
    write_u16le(out + 8, flags);
    write_u32le(out + 10, file_count);
    write_u64le(out + 14, idx_offset);
    write_u64le(out + 22, idx_enc_sz);
    write_u64le(out + 30, idx_plain_sz);
    memcpy(out + 38, idx_nonce, LITEPAK_NONCE_SIZE);
    write_u32le(out + 50, litepak_crc32c(out, 50, 0));
    memcpy(out + 54, k2, 16);
    memcpy(out + 70, k8, 16);
    write_u16le(out + 86, seed);
    write_u32le(out + 88, v6_features);
    write_u32le(out + 92, LITEPAK_HEADER_SIZE);
    return 0;
}

/* ============================================================================
 * Index 解包
 * ============================================================================ */

/*
 * 索引区处理顺序必须和 Python 版保持一致：
 * 先读取密文 -> 再按 header 派生索引密钥 -> 解密 -> 必要时 zstd 解压 -> 最后去混淆。
 */
LITEPAK_PROTECTED_BEGIN
static LITEPAK_PROTECTED_NOINLINE int litepak_decrypt_index_impl_protected(FILE* fp, const litepak_header_t* header,
                                                     const uint8_t pre_master_key[32], buffer_t* out_plain) {
    enum {
        D_SEEK = 0x3140u,
        D_ALLOC_ENC = 0x3141u,
        D_READ_ENC = 0x3142u,
        D_DERIVE_KEY = 0x3143u,
        D_ALLOC_PAYLOAD = 0x3144u,
        D_DECRYPT = 0x3145u,
        D_PAYLOAD_ROUTE = 0x3146u,
        D_DECOMPRESS = 0x3147u,
        D_COPY_PAYLOAD = 0x3148u,
        D_SIZE_CHECK = 0x3149u,
        D_DEOBFUSCATE_ROUTE = 0x314Au,
        D_DEOBFUSCATE_ALLOC = 0x314Bu,
        D_DEOBFUSCATE_APPLY = 0x314Cu,
        D_DONE = 0x314Du,
        D_FAIL = 0x314Eu,
        D_BOGUS_MIX = 0x3F10u,
        D_BOGUS_SKIP = 0x3F11u
    };
    buffer_t enc;
    buffer_t payload;
    uint8_t idx_key[32];
    uint8_t* tmp = NULL;
    size_t tmp_len = 0;
    int rc = -1;
    uint32_t after_bogus = D_FAIL;
    uint32_t state_key = litepak_cff_mix32((uint32_t)header->seed ^ (uint32_t)header->v6_features ^ 0xDEC0DEu);
    volatile uint32_t state = D_SEEK;
    volatile uint32_t cff_noise = (uint32_t)header->seed ^ (uint32_t)header->v6_features;
    uint32_t cff_shadow = (uint32_t)header->index_encrypted_sz ^ (uint32_t)header->index_plain_sz;

    buffer_init(&enc);
    buffer_init(&payload);
    buffer_init(out_plain);
    memset(idx_key, 0, sizeof(idx_key));

    for (;;) {
        switch (LITEPAK_CFF_DISPATCH(state, state_key)) {
        case D_SEEK:
            if (_fseeki64(fp, (long long)header->index_offset, SEEK_SET) != 0) {
                state = D_FAIL;
                break;
            }
            after_bogus = D_ALLOC_ENC;
            state = LITEPAK_CFF_NEXT(cff_noise ^ 0xA531u, D_ALLOC_ENC, D_BOGUS_MIX);
            break;

        case D_ALLOC_ENC:
            if (buffer_reserve(&enc, (size_t)header->index_encrypted_sz) != 0) {
                state = D_FAIL;
                break;
            }
            enc.len = (size_t)header->index_encrypted_sz;
            state = D_READ_ENC;
            break;

        case D_READ_ENC:
            if (read_exact(fp, enc.data, enc.len) != 0) {
                state = D_FAIL;
                break;
            }
            after_bogus = D_DERIVE_KEY;
            state = LITEPAK_CFF_NEXT(cff_noise ^ enc.len, D_DERIVE_KEY, D_BOGUS_SKIP);
            break;

        case D_DERIVE_KEY:
            litepak_derive_index_key(pre_master_key, header->index_plain_sz, idx_key);
            if (enc.len < LITEPAK_GCM_TAG_SIZE) {
                state = D_FAIL;
                break;
            }
            state = D_ALLOC_PAYLOAD;
            break;

        case D_ALLOC_PAYLOAD:
            if (buffer_reserve(&payload, enc.len - LITEPAK_GCM_TAG_SIZE) != 0) {
                state = D_FAIL;
                break;
            }
            payload.len = enc.len - LITEPAK_GCM_TAG_SIZE;
            state = D_DECRYPT;
            break;

        case D_DECRYPT:
            if (aes_gcm_decrypt(enc.data, enc.len, idx_key, header->index_nonce, payload.data) != 0) {
                state = D_FAIL;
                break;
            }
            after_bogus = D_PAYLOAD_ROUTE;
            state = LITEPAK_CFF_NEXT(cff_noise ^ payload.len, D_PAYLOAD_ROUTE, D_BOGUS_MIX);
            break;

        case D_PAYLOAD_ROUTE:
            state = header->index_compressed ? D_DECOMPRESS : D_COPY_PAYLOAD;
            break;

        case D_DECOMPRESS:
            if (decompress_zstd_payload(payload.data, payload.len, (size_t)header->index_plain_sz, out_plain) != 0) {
                state = D_FAIL;
                break;
            }
            state = D_SIZE_CHECK;
            break;

        case D_COPY_PAYLOAD:
            if (buffer_append(out_plain, payload.data, payload.len) != 0) {
                state = D_FAIL;
                break;
            }
            state = D_SIZE_CHECK;
            break;

        case D_SIZE_CHECK:
            if (out_plain->len != (size_t)header->index_plain_sz) {
                state = D_FAIL;
                break;
            }
            after_bogus = D_DEOBFUSCATE_ROUTE;
            state = LITEPAK_CFF_NEXT(cff_noise ^ out_plain->len, D_DEOBFUSCATE_ROUTE, D_BOGUS_SKIP);
            break;

        case D_DEOBFUSCATE_ROUTE:
            state = header->index_obfuscated ? D_DEOBFUSCATE_ALLOC : D_DONE;
            break;

        case D_DEOBFUSCATE_ALLOC:
            tmp_len = out_plain->len;
            tmp = (uint8_t*)malloc(tmp_len);
            if (!tmp) {
                state = D_FAIL;
                break;
            }
            state = D_DEOBFUSCATE_APPLY;
            break;

        case D_DEOBFUSCATE_APPLY:
            litepak_deobfuscate_index(out_plain->data, out_plain->len, header->seed, pre_master_key, tmp);
            memcpy(out_plain->data, tmp, out_plain->len);
            state = D_DONE;
            break;

        case D_BOGUS_MIX:
            cff_shadow ^= litepak_cff_mix32((uint32_t)cff_noise ^ cff_shadow ^ (uint32_t)payload.len);
            cff_noise = litepak_cff_mix32(cff_shadow ^ (uint32_t)enc.len ^ 0xC001D00Du);
            state = after_bogus;
            break;

        case D_BOGUS_SKIP:
            cff_noise ^= litepak_cff_mix32((uint32_t)header->index_offset ^ cff_shadow ^ 0x51EED123u);
            state = after_bogus;
            break;

        case D_DONE:
            rc = 0;
            goto cleanup;

        case D_FAIL:
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

int litepak_decrypt_index(FILE* fp, const litepak_header_t* header,
                          const uint8_t pre_master_key[32], buffer_t* out_plain) {
    if (litepak_codecrypt_ensure_decrypted() != 0)
        return -1;
    return litepak_decrypt_index_impl_protected(fp, header, pre_master_key, out_plain);
}

void litepak_free_index_meta(index_meta_t* meta) {
    if (!meta) return;
    free(meta->entries);
    free(meta->chunk_records);
    free(meta->chunk_refs);
    memset(meta, 0, sizeof(*meta));
}

/*
 * 这里把 index 明文拆回内存结构，并在 strict 模式下校验
 * chunk_table / chunk_refs / entries / body 四段摘要。
 */
LITEPAK_PROTECTED_BEGIN
static LITEPAK_PROTECTED_NOINLINE int litepak_parse_index_impl_protected(const uint8_t* index_plain, size_t plain_len,
                                                bool strict, uint16_t seed, uint32_t v6_features, index_meta_t* meta) {
    enum {
        P_INIT = 0x5100u,
        P_MIN_SIZE = 0x5101u,
        P_COUNTS_HASHES = 0x5102u,
        P_K6_SIZE = 0x5103u,
        P_K6_BODY = 0x5104u,
        P_K10_SIZE = 0x5105u,
        P_K10_BODY = 0x5106u,
        P_CDC = 0x5107u,
        P_PADDING = 0x5108u,
        P_ALLOC = 0x5109u,
        P_CHUNK_INIT = 0x510Au,
        P_CHUNK_NEXT = 0x510Bu,
        P_REFS_INIT = 0x510Cu,
        P_REFS_NEXT = 0x510Du,
        P_ENTRIES_INIT = 0x510Eu,
        P_ENTRIES_NEXT = 0x510Fu,
        P_HASH_CHUNK_TABLE = 0x5110u,
        P_HASH_REFS = 0x5111u,
        P_HASH_ENTRIES = 0x5112u,
        P_HASH_BODY = 0x5113u,
        P_DONE = 0x5114u,
        P_FAIL = 0x5115u,
        P_BOGUS_MIX_A = 0x5F01u,
        P_BOGUS_MIX_B = 0x5F02u,
        P_BOGUS_SKIP = 0x5F03u
    };
    size_t pos = 0;
    uint32_t entry_count = 0;
    uint32_t chunk_count = 0;
    uint32_t chunk_ref_count = 0;
    uint32_t k6_size = 0;
    uint32_t k10_size = 0;
    uint32_t pad_size = 0;
    uint32_t i = 0;
    uint8_t body_hash[LITEPAK_INDEX_HASH_SIZE];
    uint8_t chunk_table_hash[LITEPAK_INDEX_HASH_SIZE];
    uint8_t chunk_refs_hash[LITEPAK_INDEX_HASH_SIZE];
    uint8_t entries_hash[LITEPAK_INDEX_HASH_SIZE];
    uint8_t actual_hash[LITEPAK_INDEX_HASH_SIZE];
    size_t chunk_table_start = 0;
    size_t chunk_table_end = 0;
    size_t refs_start = 0;
    size_t refs_end = 0;
    size_t entries_start = 0;
    size_t entries_end = 0;
    int allocated = 0;
    uint32_t after_bogus = P_FAIL;
    uint32_t state_key = litepak_cff_mix32((uint32_t)plain_len ^ ((uint32_t)seed << 16) ^ v6_features ^ 0x1D3A5E7Fu);
    volatile uint32_t state = P_INIT;
    volatile uint32_t cff_noise = (uint32_t)plain_len ^ ((uint32_t)seed << 16) ^ v6_features;
    uint32_t cff_shadow = 0xC6A110u ^ (uint32_t)seed;
    chunk_record_t* rec = NULL;
    entry_t* e = NULL;

    for (;;) {
        switch (LITEPAK_CFF_DISPATCH(state, state_key)) {
        case P_INIT:
            memset(meta, 0, sizeof(*meta));
            memset(body_hash, 0, sizeof(body_hash));
            memset(chunk_table_hash, 0, sizeof(chunk_table_hash));
            memset(chunk_refs_hash, 0, sizeof(chunk_refs_hash));
            memset(entries_hash, 0, sizeof(entries_hash));
            state = P_MIN_SIZE;
            break;

        case P_MIN_SIZE:
            if (plain_len < 12 + 4 * LITEPAK_INDEX_HASH_SIZE + 4 + 16 + 1 + 4 + 16 + 1 + 16) {
                state = P_FAIL;
                break;
            }
            after_bogus = P_COUNTS_HASHES;
            state = LITEPAK_CFF_NEXT(cff_noise ^ 0x1001u, P_COUNTS_HASHES, P_BOGUS_MIX_A);
            break;

        case P_COUNTS_HASHES:
            entry_count = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            chunk_count = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            chunk_ref_count = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            memcpy(body_hash, index_plain + pos, LITEPAK_INDEX_HASH_SIZE); pos += LITEPAK_INDEX_HASH_SIZE;
            memcpy(chunk_table_hash, index_plain + pos, LITEPAK_INDEX_HASH_SIZE); pos += LITEPAK_INDEX_HASH_SIZE;
            memcpy(chunk_refs_hash, index_plain + pos, LITEPAK_INDEX_HASH_SIZE); pos += LITEPAK_INDEX_HASH_SIZE;
            memcpy(entries_hash, index_plain + pos, LITEPAK_INDEX_HASH_SIZE); pos += LITEPAK_INDEX_HASH_SIZE;
            cff_noise ^= litepak_cff_mix32(entry_count ^ chunk_count ^ chunk_ref_count);
            state = P_K6_SIZE;
            break;

        case P_K6_SIZE:
            if (pos > plain_len || plain_len - pos < 4) {
                state = P_FAIL;
                break;
            }
            k6_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            after_bogus = P_K6_BODY;
            state = LITEPAK_CFF_NEXT(cff_noise ^ k6_size ^ 0xA6u, P_K6_BODY, P_BOGUS_SKIP);
            break;

        case P_K6_BODY:
            if (k6_size > sizeof(meta->k6) || pos > plain_len || k6_size > plain_len - pos || plain_len - pos - k6_size < 1) {
                state = P_FAIL;
                break;
            }
            memcpy(meta->k6, index_plain + pos, k6_size);
            pos += k6_size;
            if (litepak_crc8(meta->k6, k6_size) != index_plain[pos]) {
                state = P_FAIL;
                break;
            }
            pos += 1;
            state = P_K10_SIZE;
            break;

        case P_K10_SIZE:
            if (pos > plain_len || plain_len - pos < 4) {
                state = P_FAIL;
                break;
            }
            k10_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            after_bogus = P_K10_BODY;
            state = LITEPAK_CFF_NEXT(cff_noise ^ k10_size ^ 0xA10u, P_K10_BODY, P_BOGUS_MIX_B);
            break;

        case P_K10_BODY:
            if (k10_size > sizeof(meta->k10) || pos > plain_len || k10_size > plain_len - pos || plain_len - pos - k10_size < 1) {
                state = P_FAIL;
                break;
            }
            memcpy(meta->k10, index_plain + pos, k10_size);
            pos += k10_size;
            if (litepak_crc8(meta->k10, k10_size) != index_plain[pos]) {
                state = P_FAIL;
                break;
            }
            pos += 1;
            state = P_CDC;
            break;

        case P_CDC:
            if (pos > plain_len || plain_len - pos < 16) {
                state = P_FAIL;
                break;
            }
            meta->whole_file_threshold = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            meta->cdc_min_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            meta->cdc_avg_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            meta->cdc_max_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            after_bogus = P_PADDING;
            state = LITEPAK_CFF_NEXT(cff_noise ^ meta->cdc_avg_size, P_PADDING, P_BOGUS_MIX_A);
            break;

        case P_PADDING:
            if (pos > plain_len || plain_len - pos < 4) {
                state = P_FAIL;
                break;
            }
            pad_size = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            if (pos > plain_len || pad_size > plain_len - pos) {
                state = P_FAIL;
                break;
            }
            pos += pad_size;
            state = P_ALLOC;
            break;

        case P_ALLOC:
            meta->entry_count = entry_count;
            meta->chunk_record_count = chunk_count;
            meta->chunk_ref_count = chunk_ref_count;
            meta->entries = (entry_t*)calloc(entry_count ? entry_count : 1, sizeof(entry_t));
            meta->chunk_records = (chunk_record_t*)calloc(chunk_count ? chunk_count : 1, sizeof(chunk_record_t));
            meta->chunk_refs = (uint32_t*)calloc(chunk_ref_count ? chunk_ref_count : 1, sizeof(uint32_t));
            allocated = 1;
            if (!meta->entries || !meta->chunk_records || !meta->chunk_refs) {
                state = P_FAIL;
                break;
            }
            after_bogus = P_CHUNK_INIT;
            state = LITEPAK_CFF_NEXT(cff_noise ^ entry_count, P_CHUNK_INIT, P_BOGUS_SKIP);
            break;

        case P_CHUNK_INIT:
            chunk_table_start = pos;
            i = 0;
            state = P_CHUNK_NEXT;
            break;

        case P_CHUNK_NEXT:
            if (i >= chunk_count) {
                chunk_table_end = pos;
                state = P_REFS_INIT;
                break;
            }
            if (pos > plain_len || plain_len - pos < 91) {
                state = P_FAIL;
                break;
            }
            rec = &meta->chunk_records[i];
            memcpy(rec->chunk_hash, index_plain + pos, LITEPAK_CHUNK_HASH_SIZE); pos += LITEPAK_CHUNK_HASH_SIZE;
            rec->original_size = read_u64le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 8;
            rec->stored_size = read_u64le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 8;
            rec->data_offset = read_u64le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 8;
            memcpy(rec->data_nonce, index_plain + pos, LITEPAK_NONCE_SIZE); pos += LITEPAK_NONCE_SIZE;
            rec->chunk_crc32c = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            rec->chunk_kind = index_plain[pos++];
            rec->mode = index_plain[pos++];
            rec->transform_flags = index_plain[pos++];
            memcpy(rec->file_id, index_plain + pos, LITEPAK_FILE_ID_SIZE); pos += LITEPAK_FILE_ID_SIZE;
            i++;
            after_bogus = P_CHUNK_NEXT;
            state = LITEPAK_CFF_NEXT(cff_noise ^ i ^ rec->chunk_crc32c, P_CHUNK_NEXT, P_BOGUS_MIX_B);
            break;

        case P_REFS_INIT:
            refs_start = pos;
            i = 0;
            state = P_REFS_NEXT;
            break;

        case P_REFS_NEXT:
            if (i >= chunk_ref_count) {
                refs_end = pos;
                state = P_ENTRIES_INIT;
                break;
            }
            if (pos > plain_len || plain_len - pos < 4) {
                state = P_FAIL;
                break;
            }
            meta->chunk_refs[i] = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos);
            pos += 4;
            cff_noise ^= litepak_cff_mix32(meta->chunk_refs[i] ^ i);
            i++;
            after_bogus = P_REFS_NEXT;
            state = LITEPAK_CFF_NEXT(cff_noise ^ 0xC0DEu, P_REFS_NEXT, P_BOGUS_SKIP);
            break;

        case P_ENTRIES_INIT:
            entries_start = pos;
            i = 0;
            state = P_ENTRIES_NEXT;
            break;

        case P_ENTRIES_NEXT:
            if (i >= entry_count) {
                entries_end = pos;
                state = P_HASH_CHUNK_TABLE;
                break;
            }
            if (pos > plain_len || plain_len - pos < 53) {
                state = P_FAIL;
                break;
            }
            e = &meta->entries[i];
            memcpy(e->hash_bytes, index_plain + pos, LITEPAK_PATH_HASH_SIZE); pos += LITEPAK_PATH_HASH_SIZE;
            memcpy(e->file_id, index_plain + pos, LITEPAK_FILE_ID_SIZE); pos += LITEPAK_FILE_ID_SIZE;
            e->flags = index_plain[pos++];
            e->original_size = read_u64le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 8;
            e->file_crc32c = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            e->chunk_ref_start = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            e->chunk_count = read_u32le_cff(index_plain + pos, cff_noise ^ (uint32_t)pos); pos += 4;
            litepak_deobfuscate_entry(e, seed, v6_features);
            if (e->flags == ENTRY_KEY_PAYLOAD)
                meta->k9_entry = e;
            i++;
            after_bogus = P_ENTRIES_NEXT;
            state = LITEPAK_CFF_NEXT(cff_noise ^ i ^ e->file_crc32c, P_ENTRIES_NEXT, P_BOGUS_MIX_A);
            break;

        case P_HASH_CHUNK_TABLE:
            litepak_hash16_personal(index_plain + chunk_table_start, chunk_table_end - chunk_table_start,
                                    (const uint8_t*)"LiteCTH6", 8, actual_hash);
            if (!hash16_eq(actual_hash, chunk_table_hash)) {
                if (strict) {
                    state = P_FAIL;
                    break;
                }
                litepak_emit_log("  chunk_table_hash 不匹配");
            }
            after_bogus = P_HASH_REFS;
            state = LITEPAK_CFF_NEXT(cff_noise ^ 0xC7A6u, P_HASH_REFS, P_BOGUS_SKIP);
            break;

        case P_HASH_REFS:
            litepak_hash16_personal(index_plain + refs_start, refs_end - refs_start,
                                    (const uint8_t*)"LiteCRH6", 8, actual_hash);
            if (!hash16_eq(actual_hash, chunk_refs_hash)) {
                if (strict) {
                    state = P_FAIL;
                    break;
                }
                litepak_emit_log("  chunk_refs_hash 不匹配");
            }
            state = P_HASH_ENTRIES;
            break;

        case P_HASH_ENTRIES:
            litepak_hash16_personal(index_plain + entries_start, entries_end - entries_start,
                                    (const uint8_t*)"LiteENH6", 8, actual_hash);
            if (!hash16_eq(actual_hash, entries_hash)) {
                if (strict) {
                    state = P_FAIL;
                    break;
                }
                litepak_emit_log("  entries_hash 不匹配");
            }
            after_bogus = P_HASH_BODY;
            state = LITEPAK_CFF_NEXT(cff_noise ^ 0xE17E5u, P_HASH_BODY, P_BOGUS_MIX_B);
            break;

        case P_HASH_BODY:
            litepak_hash16_personal(index_plain + 12 + LITEPAK_INDEX_HASH_SIZE,
                                    plain_len - (12 + LITEPAK_INDEX_HASH_SIZE),
                                    (const uint8_t*)"LiteBDH6", 8, actual_hash);
            if (!hash16_eq(actual_hash, body_hash)) {
                if (strict) {
                    state = P_FAIL;
                    break;
                }
                litepak_emit_log("  index_body_hash 不匹配");
            }
            state = P_DONE;
            break;

        case P_BOGUS_MIX_A:
            cff_shadow ^= litepak_cff_mix32((uint32_t)cff_noise ^ (uint32_t)pos ^ entry_count ^ seed);
            cff_noise = litepak_cff_mix32(cff_shadow ^ chunk_count ^ v6_features ^ 0xA5A55A5Au);
            state = after_bogus;
            break;

        case P_BOGUS_MIX_B:
            cff_shadow += litepak_cff_mix32((uint32_t)plain_len ^ chunk_ref_count ^ (uint32_t)i ^ 0x3C6EF372u);
            cff_noise ^= litepak_cff_mix32(cff_shadow ^ (uint32_t)entries_start ^ 0x9E3779B9u);
            state = after_bogus;
            break;

        case P_BOGUS_SKIP:
            cff_noise ^= litepak_cff_mix32((uint32_t)pos ^ cff_shadow ^ 0xBADC0DEu);
            state = after_bogus;
            break;

        case P_DONE:
            return 0;

        case P_FAIL:
            if (allocated)
                litepak_free_index_meta(meta);
            return -1;

        default:
            if (allocated)
                litepak_free_index_meta(meta);
            return -1;
        }
    }
}
LITEPAK_PROTECTED_END

int litepak_parse_index(const uint8_t* index_plain, size_t plain_len,
                        bool strict, uint16_t seed, uint32_t v6_features, index_meta_t* meta) {
    if (litepak_codecrypt_ensure_decrypted() != 0)
        return -1;
    return litepak_parse_index_impl_protected(index_plain, plain_len, strict, seed, v6_features, meta);
}

/* ============================================================================
 * Index / Trailer 构建与校验
 * ============================================================================ */

/*
 * 生成 index 明文时，字段顺序、摘要顺序、padding 位置都必须与 Python 版完全一致，
 * 否则后续的 header/trailer 校验链会全部失效。
 */
int litepak_build_index(const chunk_record_t* chunk_records, uint32_t chunk_count,
                        const uint32_t* chunk_refs, uint32_t ref_count,
                        const entry_t* entries, uint32_t entry_count,
                        const uint8_t k6[16], const uint8_t k10[16],
                        const cdc_params_t* cdc_params, uint32_t whole_file_threshold,
                        uint16_t seed, uint32_t v6_features, bool obfuscate_entries, buffer_t* out) {
    buffer_t chunk_table, refs_blob, entries_blob;
    uint8_t hash_buf[LITEPAK_INDEX_HASH_SIZE];

    buffer_init(&chunk_table);
    buffer_init(&refs_blob);
    buffer_init(&entries_blob);
    buffer_init(out);

    for (uint32_t i = 0; i < chunk_count; i++) {
        const chunk_record_t* rec = &chunk_records[i];
        buffer_append(&chunk_table, rec->chunk_hash, LITEPAK_CHUNK_HASH_SIZE);
        buffer_append_u64le(&chunk_table, rec->original_size);
        buffer_append_u64le(&chunk_table, rec->stored_size);
        buffer_append_u64le(&chunk_table, rec->data_offset);
        buffer_append(&chunk_table, rec->data_nonce, LITEPAK_NONCE_SIZE);
        buffer_append_u32le(&chunk_table, rec->chunk_crc32c);
        buffer_append_u8(&chunk_table, rec->chunk_kind);
        buffer_append_u8(&chunk_table, rec->mode);
        buffer_append_u8(&chunk_table, rec->transform_flags);
        buffer_append(&chunk_table, rec->file_id, LITEPAK_FILE_ID_SIZE);
    }

    for (uint32_t i = 0; i < ref_count; i++)
        buffer_append_u32le(&refs_blob, chunk_refs[i]);

    for (uint32_t i = 0; i < entry_count; i++) {
        entry_t e = entries[i];
        if (obfuscate_entries)
            litepak_obfuscate_entry(&e, seed, v6_features);
        buffer_append(&entries_blob, e.hash_bytes, LITEPAK_PATH_HASH_SIZE);
        buffer_append(&entries_blob, e.file_id, LITEPAK_FILE_ID_SIZE);
        buffer_append_u8(&entries_blob, e.flags);
        buffer_append_u64le(&entries_blob, e.original_size);
        buffer_append_u32le(&entries_blob, e.file_crc32c);
        buffer_append_u32le(&entries_blob, e.chunk_ref_start);
        buffer_append_u32le(&entries_blob, e.chunk_count);
    }

    buffer_append_u32le(out, entry_count);
    buffer_append_u32le(out, chunk_count);
    buffer_append_u32le(out, ref_count);
    {
        size_t body_hash_pos = out->len;
        buffer_append(out, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", LITEPAK_INDEX_HASH_SIZE);

        litepak_hash16_personal(chunk_table.data, chunk_table.len, (const uint8_t*)"LiteCTH6", 8, hash_buf);
        buffer_append(out, hash_buf, LITEPAK_INDEX_HASH_SIZE);
        litepak_hash16_personal(refs_blob.data, refs_blob.len, (const uint8_t*)"LiteCRH6", 8, hash_buf);
        buffer_append(out, hash_buf, LITEPAK_INDEX_HASH_SIZE);
        litepak_hash16_personal(entries_blob.data, entries_blob.len, (const uint8_t*)"LiteENH6", 8, hash_buf);
        buffer_append(out, hash_buf, LITEPAK_INDEX_HASH_SIZE);

        buffer_append_u32le(out, 16);
        buffer_append(out, k6, 16);
        buffer_append_u8(out, litepak_crc8(k6, 16));
        buffer_append_u32le(out, 16);
        buffer_append(out, k10, 16);
        buffer_append_u8(out, litepak_crc8(k10, 16));
        buffer_append_u32le(out, whole_file_threshold);
        buffer_append_u32le(out, cdc_params->min_size);
        buffer_append_u32le(out, cdc_params->avg_size);
        buffer_append_u32le(out, cdc_params->max_size);

        {
            size_t pad_size_pos = out->len;
            uint32_t pad_size;
            buffer_append_u32le(out, 0);
            pad_size = (uint32_t)((16 - (out->len % 16)) % 16);
            write_u32le(out->data + pad_size_pos, pad_size);
            if (pad_size > 0) {
                size_t old_len = out->len;
                buffer_reserve(out, out->len + pad_size);
                litepak_random_bytes(out->data + old_len, pad_size);
                out->len += pad_size;
            }
        }

        buffer_append(out, chunk_table.data, chunk_table.len);
        buffer_append(out, refs_blob.data, refs_blob.len);
        buffer_append(out, entries_blob.data, entries_blob.len);
        litepak_hash16_personal(out->data + body_hash_pos + LITEPAK_INDEX_HASH_SIZE,
                                out->len - (body_hash_pos + LITEPAK_INDEX_HASH_SIZE),
                                (const uint8_t*)"LiteBDH6", 8, out->data + body_hash_pos);
    }

    buffer_free(&chunk_table);
    buffer_free(&refs_blob);
    buffer_free(&entries_blob);
    return 0;
}

#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
void litepak_sign_trailer(const uint8_t* trailer_data, size_t len,
                          const uint8_t* sign_key, uint8_t signature[64]) {
    uint8_t default_seed[32];
    const uint8_t* seed = sign_key;
    if (!seed) {
        litepak_materialize_default_sign_seed(default_seed);
        seed = default_seed;
    }
    ed25519_sign(trailer_data, len, seed, signature);
    if (!sign_key)
        litepak_secure_bzero(default_seed, sizeof(default_seed));
}
#endif

int litepak_verify_trailer_sig(const uint8_t* trailer_data, size_t len,
                               const uint8_t signature[64], const uint8_t* verify_public_key) {
    uint8_t default_public_key[32];
    const uint8_t* public_key = verify_public_key;
    uint8_t zeros[64] = {0};
    int ok;
    if (memcmp(signature, zeros, 64) == 0)
        return 0;
    if (!public_key) {
        litepak_materialize_default_sign_public_key(default_public_key);
        public_key = default_public_key;
    }
    ok = ed25519_verify(trailer_data, len, public_key, signature) == 0;
    if (!verify_public_key)
        litepak_secure_bzero(default_public_key, sizeof(default_public_key));
    litepak_secure_bzero(zeros, sizeof(zeros));
    return ok;
}

int litepak_build_trailer(FILE* fp, const uint8_t* index_plain, size_t index_plain_len,
                          uint64_t data_end, const uint8_t key_material_signature[32],
                          bool sign, const uint8_t* sign_key,
                          uint8_t out[LITEPAK_TRAILER_SIZE]) {
    long long saved_pos;
    uint8_t header_bytes[50];
    uint8_t index_plain_hash[32];
    uint8_t data_area_hash[32];
    uint8_t header_chain_full[32];
    uint8_t trailer_self_hash[32];
    buffer_t data_area;

    buffer_init(&data_area);
    saved_pos = _ftelli64(fp);
    if (_fseeki64(fp, 0, SEEK_SET) != 0)
        return -1;
    if (read_exact(fp, header_bytes, sizeof(header_bytes)) != 0)
        return -1;

    blake2b_full(index_plain, index_plain_len, index_plain_hash, 32, NULL, 0, (const uint8_t*)"LiteIHv6", 8);

    if (_fseeki64(fp, LITEPAK_HEADER_SIZE, SEEK_SET) != 0)
        return -1;
    if (data_end > LITEPAK_HEADER_SIZE) {
        size_t area_size = (size_t)(data_end - LITEPAK_HEADER_SIZE);
        buffer_reserve(&data_area, area_size);
        data_area.len = area_size;
        if (read_exact(fp, data_area.data, area_size) != 0) {
            buffer_free(&data_area);
            return -1;
        }
        blake2b_full(data_area.data, data_area.len, data_area_hash, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    } else {
        blake2b_full("", 0, data_area_hash, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    }

    {
        uint8_t header_chain_input[50 + 32];
        memcpy(header_chain_input, header_bytes, 50);
        memcpy(header_chain_input + 50, index_plain_hash, 32);
        blake2b_full(header_chain_input, sizeof(header_chain_input), header_chain_full, 32, NULL, 0,
                     (const uint8_t*)"LiteHCv6", 8);
    }

    memset(out, 0, LITEPAK_TRAILER_SIZE);
    memcpy(out + 0, index_plain_hash, 32);
    memcpy(out + 32, data_area_hash, 32);
    memcpy(out + 64, header_chain_full, 8);
    memcpy(out + 72, "LiteTRLR", 8);
    write_u64le(out + 80, LITEPAK_TRAILER_SIZE);
    memcpy(out + 88, key_material_signature, 32);
    blake2b_full(out, 120, trailer_self_hash, 32, NULL, 0, (const uint8_t*)"LiteTRv6", 8);
    memcpy(out + 120, trailer_self_hash, 8);
    if (sign) {
#ifdef LITEPAK_ENABLE_PRIVATE_SIGNING
        litepak_sign_trailer(out, 128, sign_key, out + 128);
#else
        buffer_free(&data_area);
        _fseeki64(fp, saved_pos, SEEK_SET);
        return -1;
#endif
    }

    buffer_free(&data_area);
    _fseeki64(fp, saved_pos, SEEK_SET);
    return 0;
}

int litepak_read_trailer(FILE* fp, const uint8_t* index_plain, size_t index_plain_len,
                         const litepak_header_t* header, const uint8_t* verify_public_key,
                         const uint8_t expected_key_material_signature[32]) {
    uint8_t raw[LITEPAK_TRAILER_SIZE];
    uint8_t hash_buf[32];
    uint8_t expected8[8];
    uint8_t index_plain_hash[32];
    buffer_t data_area;

    buffer_init(&data_area);
    if (_fseeki64(fp, -(long long)LITEPAK_TRAILER_SIZE, SEEK_END) != 0)
        return 0;
    if (read_exact(fp, raw, sizeof(raw)) != 0)
        return 0;

    if (memcmp(raw + 72, "LiteTRLR", 8) != 0)
        return 0;
    if (read_u64le(raw + 80) != LITEPAK_TRAILER_SIZE)
        return 0;
    if (expected_key_material_signature && memcmp(raw + 88, expected_key_material_signature, 32) != 0)
        return 0;

    blake2b_full(raw, 120, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteTRv6", 8);
    if (memcmp(hash_buf, raw + 120, 8) != 0)
        return 0;

    blake2b_full(index_plain, index_plain_len, index_plain_hash, 32, NULL, 0, (const uint8_t*)"LiteIHv6", 8);
    if (memcmp(index_plain_hash, raw + 0, 32) != 0)
        return 0;

    if (_fseeki64(fp, LITEPAK_HEADER_SIZE, SEEK_SET) != 0)
        return 0;
    if (header->index_offset > LITEPAK_HEADER_SIZE) {
        size_t area_size = (size_t)(header->index_offset - LITEPAK_HEADER_SIZE);
        buffer_reserve(&data_area, area_size);
        data_area.len = area_size;
        if (read_exact(fp, data_area.data, area_size) != 0) {
            buffer_free(&data_area);
            return 0;
        }
        blake2b_full(data_area.data, data_area.len, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    } else {
        blake2b_full("", 0, hash_buf, 32, NULL, 0, (const uint8_t*)"LiteDHv6", 8);
    }
    if (memcmp(hash_buf, raw + 32, 32) != 0) {
        buffer_free(&data_area);
        return 0;
    }

    {
        uint8_t header_bytes[50];
        uint8_t header_chain_input[82];
        if (_fseeki64(fp, 0, SEEK_SET) != 0) {
            buffer_free(&data_area);
            return 0;
        }
        if (read_exact(fp, header_bytes, 50) != 0) {
            buffer_free(&data_area);
            return 0;
        }
        memcpy(header_chain_input, header_bytes, 50);
        memcpy(header_chain_input + 50, index_plain_hash, 32);
        blake2b_full(header_chain_input, sizeof(header_chain_input), hash_buf, 32, NULL, 0,
                     (const uint8_t*)"LiteHCv6", 8);
        memcpy(expected8, hash_buf, 8);
        if (memcmp(expected8, raw + 64, 8) != 0) {
            buffer_free(&data_area);
            return 0;
        }
    }

    if (header->ed25519_signed && !litepak_verify_trailer_sig(raw, 128, raw + 128, verify_public_key)) {
        buffer_free(&data_area);
        return 0;
    }

    buffer_free(&data_area);
    return 1;
}
