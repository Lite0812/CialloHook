/*
 * LitePAK 密钥派生
 * 白盒变换、分段密钥派生、XOR 遗留加密、segmented encrypt/decrypt
 */
#include "litepak.h"
#include "litepak_internal.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * 白盒 S-Box
 * ============================================================================ */

static const uint8_t WB_STRONG_SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t* select_wb_sbox(bool strong_wb) {
    (void)strong_wb;
    return WB_STRONG_SBOX;
}

/* ============================================================================
 * 白盒变换
 * ============================================================================ */

static uint32_t rotl32(uint32_t v, unsigned r) {
    return (v << r) | (v >> (32 - r));
}

void litepak_whitebox_transform_native_ref(const uint8_t seed_a[32], const uint8_t seed_b[32],
                                           const uint8_t* extra, size_t extra_len,
                                           bool strong_wb, uint8_t out[32]) {
    const uint8_t* sbox = select_wb_sbox(strong_wb);
    uint8_t table[4][256];
    uint8_t state[64];
    uint8_t person[16];
    size_t person_len;

    /* state[:32] = protected-domain BLAKE2b(seed_a + extra) */
    {
        blake2b_state S;
        person_len = litepak_personal_wb_a(person);
        blake2b_init_param(&S, 32, NULL, 0, person, person_len);
        blake2b_update(&S, seed_a, 32);
        blake2b_update(&S, extra, extra_len);
        blake2b_final(&S, state, 32);
        litepak_secure_bzero(&S, sizeof(S));
        litepak_secure_bzero(person, sizeof(person));
    }
    /* state[32:] = protected-domain BLAKE2b(seed_b + extra) */
    {
        blake2b_state S;
        person_len = litepak_personal_wb_b(person);
        blake2b_init_param(&S, 32, NULL, 0, person, person_len);
        blake2b_update(&S, seed_b, 32);
        blake2b_update(&S, extra, extra_len);
        blake2b_final(&S, state + 32, 32);
        litepak_secure_bzero(&S, sizeof(S));
        litepak_secure_bzero(person, sizeof(person));
    }

    for (int t = 0; t < 4; t++) {
        uint8_t seed[1 + 64];
        uint8_t digest[32];
        seed[0] = (uint8_t)t;
        memcpy(seed + 1, seed_a, 32);
        memcpy(seed + 33, seed_b, 32);
        {
            blake2b_state T;
            person_len = litepak_personal_wb_tbl(person);
            blake2b_init_param(&T, 32, NULL, 0, person, person_len);
            blake2b_update(&T, seed, sizeof(seed));
            blake2b_update(&T, extra, extra_len);
            blake2b_final(&T, digest, 32);
            litepak_secure_bzero(&T, sizeof(T));
            litepak_secure_bzero(person, sizeof(person));
        }
        for (int i = 0; i < 256; i++)
            table[t][i] = (uint8_t)(sbox[i ^ digest[i & 31]] + digest[(i + t * 7) & 31] + i * (t * 2 + 1));
        litepak_secure_bzero(seed, sizeof(seed));
        litepak_secure_bzero(digest, sizeof(digest));
    }

    for (int r = 0; r < 10; r++) {
        for (int i = 0; i < 64; i++)
            state[i] = (uint8_t)(table[(i + r) & 3][state[i]] ^ table[(i + r + 1) & 3][(state[(i + 17) & 63] + r + i) & 0xFF]);

        /* Feistel: left ^= f(right), swap */
        uint8_t* left = state;
        uint8_t* right = state + 32;

        uint8_t round_key[8] = {0};
        round_key[0] = (uint8_t)(r);
        round_key[1] = (uint8_t)(r >> 8);
        round_key[2] = (uint8_t)(r >> 16);
        round_key[3] = (uint8_t)(r >> 24);

        uint8_t f_out[32];
        person_len = litepak_personal_wb_round(person);
        blake2b_full(right, 32, f_out, 32, round_key, 8, person, person_len);

        for (int i = 0; i < 32; i++)
            left[i] = (uint8_t)(left[i] ^ f_out[i] ^ table[(r + i) & 3][right[(i * 5 + r) & 31]]);

        /* swap left and right */
        uint8_t tmp[32];
        memcpy(tmp, left, 32);
        memcpy(left, right, 32);
        memcpy(right, tmp, 32);
        litepak_secure_bzero(round_key, sizeof(round_key));
        litepak_secure_bzero(f_out, sizeof(f_out));
        litepak_secure_bzero(tmp, sizeof(tmp));
        litepak_secure_bzero(person, sizeof(person));
    }

    /* 最终哈希 */
    person_len = litepak_personal_wb_out(person);
    blake2b_full(state, 64, out, 32, NULL, 0, person, person_len);
    litepak_secure_bzero(table, sizeof(table));
    litepak_secure_bzero(state, sizeof(state));
    litepak_secure_bzero(person, sizeof(person));
}

static void whitebox_transform(const uint8_t seed_a[32], const uint8_t seed_b[32],
                               const uint8_t* extra, size_t extra_len,
                               bool strong_wb, uint8_t out[32]) {
#ifndef LITEPAK_USE_NATIVE_TRANSFORM
    if (litepak_whitebox_transform_vm(seed_a, seed_b, extra, extra_len, strong_wb, out) == 0)
        return;
#endif
    litepak_whitebox_transform_native_ref(seed_a, seed_b, extra, extra_len, strong_wb, out);
}

/* ============================================================================
 * 密钥派生函数
 * ============================================================================ */

void litepak_derive_pre_master_key_ex(const uint8_t k2[16], const uint8_t k8[16],
                                       bool strong_wb, uint8_t out[32]) {
    /* extra = MAGIC + VERSION + k2 + k8 */
    uint8_t extra[7 + 1 + 16 + 16];
    uint8_t root_a[32];
    uint8_t root_b[32];
    uint8_t raw[32];
    uint8_t person[16];
    size_t person_len;
    blake2b_state S;

    memcpy(extra, LITEPAK_MAGIC, 7);
    extra[7] = LITEPAK_VERSION;
    memcpy(extra + 8, k2, 16);
    memcpy(extra + 24, k8, 16);

    litepak_materialize_root_seed_a(root_a);
    litepak_materialize_root_seed_b(root_b);
    whitebox_transform(root_a, root_b, extra, sizeof(extra), strong_wb, raw);

    person_len = litepak_personal_pre(person);
    blake2b_init_param(&S, 32, NULL, 0, person, person_len);
    blake2b_update(&S, raw, 32);
    blake2b_update(&S, k2, 16);
    blake2b_update(&S, k8, 16);
    blake2b_final(&S, out, 32);

    litepak_secure_bzero(root_a, sizeof(root_a));
    litepak_secure_bzero(root_b, sizeof(root_b));
    litepak_secure_bzero(extra, sizeof(extra));
    litepak_secure_bzero(raw, sizeof(raw));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_pre_master_key(const uint8_t k2[16], const uint8_t k8[16], uint8_t out[32]) {
    litepak_derive_pre_master_key_ex(k2, k8, true, out);
}

void litepak_derive_full_master_key_ex(const uint8_t k2[16], const uint8_t k6[16],
                                        const uint8_t k8[16], const uint8_t k9[16],
                                        const uint8_t k10[16], bool strong_wb, uint8_t out[32]) {
    /* extra = MAGIC + VERSION + k2 + k6 + k8 + k9 + k10 */
    uint8_t extra[7 + 1 + 16 * 5];
    uint8_t root_a[32];
    uint8_t root_b[32];
    uint8_t raw[32];
    uint8_t person[16];
    size_t person_len;
    blake2b_state S;

    memcpy(extra, LITEPAK_MAGIC, 7);
    extra[7] = LITEPAK_VERSION;
    memcpy(extra + 8, k2, 16);
    memcpy(extra + 24, k6, 16);
    memcpy(extra + 40, k8, 16);
    memcpy(extra + 56, k9, 16);
    memcpy(extra + 72, k10, 16);

    litepak_materialize_root_seed_a(root_a);
    litepak_materialize_root_seed_b(root_b);
    whitebox_transform(root_b, root_a, extra, sizeof(extra), strong_wb, raw);

    person_len = litepak_personal_full(person);
    blake2b_init_param(&S, 32, NULL, 0, person, person_len);
    blake2b_update(&S, raw, 32);
    blake2b_update(&S, k2, 16);
    blake2b_update(&S, k6, 16);
    blake2b_update(&S, k8, 16);
    blake2b_update(&S, k9, 16);
    blake2b_update(&S, k10, 16);
    blake2b_final(&S, out, 32);

    litepak_secure_bzero(root_a, sizeof(root_a));
    litepak_secure_bzero(root_b, sizeof(root_b));
    litepak_secure_bzero(extra, sizeof(extra));
    litepak_secure_bzero(raw, sizeof(raw));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_full_master_key(const uint8_t k2[16], const uint8_t k6[16],
                                     const uint8_t k8[16], const uint8_t k9[16],
                                     const uint8_t k10[16], uint8_t out[32]) {
    litepak_derive_full_master_key_ex(k2, k6, k8, k9, k10, true, out);
}

void litepak_derive_file_id(const uint8_t path_hash[16], const uint8_t content_id[32],
                            uint64_t original_size, uint8_t out[16]) {
    blake2b_state S;
    uint8_t sz_buf[8];
    for (int i = 0; i < 8; i++) sz_buf[i] = (uint8_t)(original_size >> (i * 8));
    blake2b_init_param(&S, 16, NULL, 0, (const uint8_t*)"LiteFileV6", 10);
    blake2b_update(&S, path_hash, 16);
    blake2b_update(&S, content_id, 32);
    blake2b_update(&S, sz_buf, 8);
    blake2b_final(&S, out, 16);
}

void litepak_compute_key_material_signature(const uint8_t k2[16], const uint8_t k6[16],
                                             const uint8_t k8[16], const uint8_t k9[16],
                                             const uint8_t k10[16], uint32_t v6_features,
                                             const uint8_t pre_master_key[32],
                                             const uint8_t full_master_key[32], uint8_t out[32]) {
    uint8_t pre_hash[32];
    uint8_t full_hash[32];
    uint8_t feature_buf[4];
    blake2b_state S;
    uint8_t person[16];
    size_t person_len;
    person_len = litepak_personal_kpre(person);
    blake2b_full(pre_master_key, 32, pre_hash, 32, NULL, 0, person, person_len);
    litepak_secure_bzero(person, sizeof(person));
    person_len = litepak_personal_kfull(person);
    blake2b_full(full_master_key, 32, full_hash, 32, NULL, 0, person, person_len);
    litepak_secure_bzero(person, sizeof(person));
    feature_buf[0] = (uint8_t)v6_features;
    feature_buf[1] = (uint8_t)(v6_features >> 8);
    feature_buf[2] = (uint8_t)(v6_features >> 16);
    feature_buf[3] = (uint8_t)(v6_features >> 24);
    person_len = litepak_personal_kms(person);
    blake2b_init_param(&S, 32, NULL, 0, person, person_len);
    blake2b_update(&S, k2, 16);
    blake2b_update(&S, k6, 16);
    blake2b_update(&S, k8, 16);
    blake2b_update(&S, k9, 16);
    blake2b_update(&S, k10, 16);
    blake2b_update(&S, feature_buf, 4);
    blake2b_update(&S, pre_hash, 32);
    blake2b_update(&S, full_hash, 32);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(pre_hash, sizeof(pre_hash));
    litepak_secure_bzero(full_hash, sizeof(full_hash));
    litepak_secure_bzero(feature_buf, sizeof(feature_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_index_key(const uint8_t pre_master_key[32], uint64_t plain_sz, uint8_t out[32]) {
    blake2b_state S;
    uint8_t person[16];
    size_t person_len = litepak_personal_idk(person);
    blake2b_init_param(&S, 32, pre_master_key, 32, person, person_len);
    blake2b_update(&S, (const uint8_t*)"INDEX", 5);
    blake2b_update(&S, (const uint8_t*)"__cdc_idx__", 11);
    uint8_t sz_buf[8];
    for (int i = 0; i < 8; i++) sz_buf[i] = (uint8_t)(plain_sz >> (i*8));
    blake2b_update(&S, sz_buf, 8);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(sz_buf, sizeof(sz_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_k9_chunk_key(const uint8_t pre_master_key[32], const uint8_t nonce[12],
                                  uint64_t plain_sz, uint8_t chunk_kind, uint8_t out[32]) {
    blake2b_state S;
    uint8_t person[16];
    size_t person_len = litepak_personal_k9(person);
    blake2b_init_param(&S, 32, pre_master_key, 32, person, person_len);
    blake2b_update(&S, (const uint8_t*)"K9CH", 4);
    blake2b_update(&S, nonce, 12);
    uint8_t sz_buf[8];
    for (int i = 0; i < 8; i++) sz_buf[i] = (uint8_t)(plain_sz >> (i*8));
    blake2b_update(&S, sz_buf, 8);
    blake2b_update(&S, &chunk_kind, 1);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(sz_buf, sizeof(sz_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_file_chunk_key(const uint8_t full_master_key[32], const uint8_t nonce[12],
                                    uint64_t plain_sz, uint8_t chunk_kind, uint8_t out[32]) {
    blake2b_state S;
    uint8_t person[16];
    size_t person_len = litepak_personal_data(person);
    blake2b_init_param(&S, 32, full_master_key, 32, person, person_len);
    blake2b_update(&S, (const uint8_t*)"CHNK", 4);
    blake2b_update(&S, nonce, 12);
    uint8_t sz_buf[8];
    for (int i = 0; i < 8; i++) sz_buf[i] = (uint8_t)(plain_sz >> (i*8));
    blake2b_update(&S, sz_buf, 8);
    blake2b_update(&S, &chunk_kind, 1);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(sz_buf, sizeof(sz_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_segment_open_key(const uint8_t master_key[32], const uint8_t nonce[12],
                                      uint8_t chunk_kind, uint8_t out[32]) {
    blake2b_state S;
    uint8_t person[16];
    size_t person_len = litepak_personal_open(person);
    blake2b_init_param(&S, 32, master_key, 32, person, person_len);
    blake2b_update(&S, (const uint8_t*)"OPEN", 4);
    blake2b_update(&S, nonce, 12);
    uint8_t boundary_buf[4];
    uint32_t boundary = LITEPAK_SEGMENT_BOUNDARY;
    boundary_buf[0] = (uint8_t)(boundary);
    boundary_buf[1] = (uint8_t)(boundary >> 8);
    boundary_buf[2] = (uint8_t)(boundary >> 16);
    boundary_buf[3] = (uint8_t)(boundary >> 24);
    blake2b_update(&S, boundary_buf, 4);
    blake2b_update(&S, &chunk_kind, 1);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(boundary_buf, sizeof(boundary_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_derive_segment_read_key(const uint8_t master_key[32], const uint8_t nonce[12],
                                      uint64_t remaining_len, const uint8_t open_key[32],
                                      const uint8_t* first_plain, uint8_t chunk_kind, uint8_t out[32]) {
    uint8_t depend[LITEPAK_SEGMENT_DEPEND_LEN];
    memset(depend, 0, LITEPAK_SEGMENT_DEPEND_LEN);
    if (first_plain) {
        size_t copy_len = LITEPAK_SEGMENT_DEPEND_LEN;
        memcpy(depend, first_plain, copy_len);
    }

    blake2b_state S;
    uint8_t person[16];
    size_t person_len = litepak_personal_read(person);
    blake2b_init_param(&S, 32, master_key, 32, person, person_len);
    blake2b_update(&S, (const uint8_t*)"READ", 4);
    blake2b_update(&S, nonce, 12);
    uint8_t len_buf[8];
    for (int i = 0; i < 8; i++) len_buf[i] = (uint8_t)(remaining_len >> (i*8));
    blake2b_update(&S, len_buf, 8);
    blake2b_update(&S, open_key, 32);
    blake2b_update(&S, depend, LITEPAK_SEGMENT_DEPEND_LEN);
    blake2b_update(&S, &chunk_kind, 1);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(depend, sizeof(depend));
    litepak_secure_bzero(len_buf, sizeof(len_buf));
    litepak_secure_bzero(person, sizeof(person));
    litepak_secure_bzero(&S, sizeof(S));
}

static void derive_transform_stream(const uint8_t full_master_key[32], const uint8_t file_id[16],
                                    const uint8_t nonce[12], uint64_t original_size,
                                    const uint8_t* person, size_t person_len, uint8_t out[32]) {
    blake2b_state S;
    uint8_t sz_buf[8];
    for (int i = 0; i < 8; i++) sz_buf[i] = (uint8_t)(original_size >> (i * 8));
    blake2b_init_param(&S, 32, full_master_key, 32, person, person_len);
    blake2b_update(&S, file_id, 16);
    blake2b_update(&S, nonce, 12);
    blake2b_update(&S, sz_buf, 8);
    blake2b_final(&S, out, 32);
    litepak_secure_bzero(sz_buf, sizeof(sz_buf));
    litepak_secure_bzero(&S, sizeof(S));
}

void litepak_apply_arx_transform(uint8_t* data, size_t len, const uint8_t full_master_key[32],
                                  const uint8_t file_id[16], const uint8_t nonce[12], uint64_t original_size) {
    uint8_t key[32];
    uint8_t person[16];
    size_t person_len = litepak_personal_arx(person);
    uint32_t a, b, c, d;
    derive_transform_stream(full_master_key, file_id, nonce, original_size, person, person_len, key);
    a = (uint32_t)key[0] | ((uint32_t)key[1] << 8) | ((uint32_t)key[2] << 16) | ((uint32_t)key[3] << 24);
    b = (uint32_t)key[4] | ((uint32_t)key[5] << 8) | ((uint32_t)key[6] << 16) | ((uint32_t)key[7] << 24);
    c = (uint32_t)key[8] | ((uint32_t)key[9] << 8) | ((uint32_t)key[10] << 16) | ((uint32_t)key[11] << 24);
    d = (uint32_t)key[12] | ((uint32_t)key[13] << 8) | ((uint32_t)key[14] << 16) | ((uint32_t)key[15] << 24);
    for (size_t i = 0; i < len; i++) {
        a += b ^ (uint32_t)i;
        b = rotl32(b + c + a, 7);
        c ^= d + b;
        d = rotl32(d ^ a ^ c, 11);
        data[i] ^= (uint8_t)(a ^ (b >> 8) ^ (c >> 16) ^ (d >> 24));
    }
    litepak_secure_bzero(key, sizeof(key));
    litepak_secure_bzero(person, sizeof(person));
}

void litepak_apply_feistel_transform(uint8_t* data, size_t len, const uint8_t full_master_key[32],
                                      const uint8_t file_id[16], const uint8_t nonce[12], uint64_t original_size) {
    uint8_t key[32];
    uint8_t person[16];
    size_t person_len;
    size_t limit = len < LITEPAK_SEGMENT_BOUNDARY ? len : LITEPAK_SEGMENT_BOUNDARY;
    size_t blocks = limit / 256;
    if (blocks < 2)
        return;
    person_len = litepak_personal_fst(person);
    derive_transform_stream(full_master_key, file_id, nonce, original_size, person, person_len, key);
    for (size_t i = 0; i + 1 < blocks; i += 2) {
        uint8_t tmp[256];
        uint8_t round = key[i & 31];
        for (size_t j = 0; j < 256; j++) {
            uint8_t mask = (uint8_t)(key[(j + round) & 31] + (uint8_t)j);
            data[i * 256 + j] ^= mask;
            data[(i + 1) * 256 + j] ^= mask;
        }
        memcpy(tmp, data + i * 256, 256);
        memcpy(data + i * 256, data + (i + 1) * 256, 256);
        memcpy(data + (i + 1) * 256, tmp, 256);
        litepak_secure_bzero(tmp, sizeof(tmp));
    }
    litepak_secure_bzero(key, sizeof(key));
    litepak_secure_bzero(person, sizeof(person));
}

static int ext_eq(const char* ext, const char* value) {
#ifdef _WIN32
    return _stricmp(ext, value) == 0;
#else
    return strcasecmp(ext, value) == 0;
#endif
}

uint8_t litepak_select_transform_flags(const char* rel_path, uint64_t original_size, uint8_t mode) {
    const char* ext = rel_path ? strrchr(rel_path, '.') : NULL;
    if (ext && original_size <= LITEPAK_WHOLE_FILE_THRESHOLD) {
        const char* script_exts[] = { ".ini", ".json", ".txt", ".ks", ".lua", ".js", ".cfg", ".xml", ".yaml", ".yml" };
        for (size_t i = 0; i < sizeof(script_exts) / sizeof(script_exts[0]); i++) {
            if (ext_eq(ext, script_exts[i]))
                return CHUNK_TRANSFORM_ARX;
        }
    }
    if (ext && original_size >= LITEPAK_SEGMENT_BOUNDARY && mode == MODE_RAW) {
        const char* media_exts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".ogg", ".mp3", ".wav", ".mp4", ".avi", ".mkv", ".dat", ".bin" };
        for (size_t i = 0; i < sizeof(media_exts) / sizeof(media_exts[0]); i++) {
            if (ext_eq(ext, media_exts[i]))
                return CHUNK_TRANSFORM_FEISTEL;
        }
    }
    return CHUNK_TRANSFORM_NONE;
}

/* ============================================================================
 * 分段加密/解密
 * ============================================================================ */

int litepak_segmented_encrypt(const uint8_t* plain_packed, size_t packed_len,
                               const uint8_t master_key[32], const uint8_t nonce[12],
                               uint8_t chunk_kind, uint64_t original_size, bool is_k9,
                               buffer_t* out) {
    if (is_k9) {
        uint8_t key[32];
        litepak_derive_k9_chunk_key(master_key, nonce, original_size, chunk_kind, key);
        if (buffer_reserve(out, packed_len + LITEPAK_GCM_TAG_SIZE) != 0) return -1;
        out->len = packed_len + LITEPAK_GCM_TAG_SIZE;
        return aes_gcm_encrypt(plain_packed, packed_len, key, nonce, out->data);
    }

    if (packed_len <= LITEPAK_SEGMENT_BOUNDARY) {
        uint8_t open_key[32];
        litepak_derive_segment_open_key(master_key, nonce, chunk_kind, open_key);
        if (buffer_reserve(out, packed_len + LITEPAK_GCM_TAG_SIZE) != 0) return -1;
        out->len = packed_len + LITEPAK_GCM_TAG_SIZE;
        return aes_gcm_encrypt(plain_packed, packed_len, open_key, nonce, out->data);
    }

    /* 大块：两段 */
    size_t seg1_len = LITEPAK_SEGMENT_BOUNDARY;
    size_t seg2_len = packed_len - LITEPAK_SEGMENT_BOUNDARY;
    const uint8_t* seg1 = plain_packed;
    const uint8_t* seg2 = plain_packed + LITEPAK_SEGMENT_BOUNDARY;
    size_t enc1_len = seg1_len + LITEPAK_GCM_TAG_SIZE;
    size_t enc2_len = seg2_len + LITEPAK_GCM_TAG_SIZE;
    uint8_t* enc1;
    uint8_t* enc2;

    uint8_t open_key[32];
    litepak_derive_segment_open_key(master_key, nonce, chunk_kind, open_key);

    enc1 = (uint8_t*)malloc(enc1_len);
    if (!enc1) return -1;
    if (aes_gcm_encrypt(seg1, seg1_len, open_key, nonce, enc1) != 0) {
        free(enc1);
        return -1;
    }

    /* derive second segment nonce in the protected domain */
    uint8_t nonce2_input[12 + 4];
    memcpy(nonce2_input, nonce, 12);
    memcpy(nonce2_input + 12, "SEG2", 4);
    uint8_t nonce2[LITEPAK_NONCE_SIZE];
    {
        uint8_t person[16];
        size_t person_len = litepak_personal_n2(person);
        blake2b_full(nonce2_input, 16, nonce2, LITEPAK_NONCE_SIZE, NULL, 0, person, person_len);
        litepak_secure_bzero(person, sizeof(person));
    }

    uint8_t read_key[32];
    litepak_derive_segment_read_key(master_key, nonce, seg2_len, open_key, seg1, chunk_kind, read_key);

    enc2 = (uint8_t*)malloc(enc2_len);
    if (!enc2) {
        free(enc1);
        return -1;
    }
    if (aes_gcm_encrypt(seg2, seg2_len, read_key, nonce2, enc2) != 0) {
        free(enc1);
        free(enc2);
        return -1;
    }

    /* 输出: [4-byte enc1_len][enc1][enc2] */
    if (buffer_reserve(out, 4 + enc1_len + enc2_len) != 0) {
        free(enc1);
        free(enc2);
        return -1;
    }
    out->len = 0;
    if (buffer_append_u32le(out, (uint32_t)enc1_len) != 0 ||
        buffer_append(out, enc1, enc1_len) != 0 ||
        buffer_append(out, enc2, enc2_len) != 0) {
        free(enc1);
        free(enc2);
        return -1;
    }

    free(enc1);
    free(enc2);
    return 0;
}

int litepak_segmented_decrypt(const uint8_t* enc_data, size_t enc_len,
                               const uint8_t master_key[32], const uint8_t nonce[12],
                               uint64_t original_size, uint8_t chunk_kind, bool is_k9,
                               buffer_t* out) {
    if (is_k9) {
        uint8_t key[32];
        litepak_derive_k9_chunk_key(master_key, nonce, original_size, chunk_kind, key);
        if (enc_len < LITEPAK_GCM_TAG_SIZE) return -1;
        size_t pt_len = enc_len - LITEPAK_GCM_TAG_SIZE;
        if (buffer_reserve(out, pt_len) != 0) return -1;
        out->len = pt_len;
        return aes_gcm_decrypt(enc_data, enc_len, key, nonce, out->data);
    }

    size_t single_max = LITEPAK_SEGMENT_BOUNDARY + LITEPAK_GCM_TAG_SIZE;
    if (enc_len <= single_max) {
        uint8_t open_key[32];
        litepak_derive_segment_open_key(master_key, nonce, chunk_kind, open_key);
        if (enc_len < LITEPAK_GCM_TAG_SIZE) return -1;
        size_t pt_len = enc_len - LITEPAK_GCM_TAG_SIZE;
        if (buffer_reserve(out, pt_len) != 0) return -1;
        out->len = pt_len;
        return aes_gcm_decrypt(enc_data, enc_len, open_key, nonce, out->data);
    }

    /* 两段解密 */
    if (enc_len < 4) return -1;
    uint32_t seg1_enc_len = (uint32_t)enc_data[0] | ((uint32_t)enc_data[1] << 8) |
                            ((uint32_t)enc_data[2] << 16) | ((uint32_t)enc_data[3] << 24);
    if (4 + seg1_enc_len > enc_len) return -1;

    const uint8_t* enc1 = enc_data + 4;
    size_t enc2_len = enc_len - 4 - seg1_enc_len;
    const uint8_t* enc2 = enc_data + 4 + seg1_enc_len;

    uint8_t open_key[32];
    litepak_derive_segment_open_key(master_key, nonce, chunk_kind, open_key);

    if (seg1_enc_len < LITEPAK_GCM_TAG_SIZE) return -1;
    size_t seg1_pt_len = seg1_enc_len - LITEPAK_GCM_TAG_SIZE;
    if (seg1_pt_len < LITEPAK_SEGMENT_DEPEND_LEN)
        return -1;
    uint8_t* seg1 = (uint8_t*)malloc(seg1_pt_len > 0 ? seg1_pt_len : 1);
    if (!seg1) return -1;
    if (aes_gcm_decrypt(enc1, seg1_enc_len, open_key, nonce, seg1) != 0) {
        free(seg1);
        return -1;
    }

    uint8_t nonce2_input[16];
    memcpy(nonce2_input, nonce, 12);
    memcpy(nonce2_input + 12, "SEG2", 4);
    uint8_t nonce2[LITEPAK_NONCE_SIZE];
    {
        uint8_t person[16];
        size_t person_len = litepak_personal_n2(person);
        blake2b_full(nonce2_input, 16, nonce2, LITEPAK_NONCE_SIZE, NULL, 0, person, person_len);
        litepak_secure_bzero(person, sizeof(person));
    }

    if (enc2_len < LITEPAK_GCM_TAG_SIZE) { free(seg1); return -1; }
    size_t seg2_pt_len = enc2_len - LITEPAK_GCM_TAG_SIZE;

    uint8_t read_key[32];
    litepak_derive_segment_read_key(master_key, nonce, seg2_pt_len, open_key, seg1, chunk_kind, read_key);

    uint8_t* seg2 = (uint8_t*)malloc(seg2_pt_len > 0 ? seg2_pt_len : 1);
    if (!seg2) {
        free(seg1);
        return -1;
    }
    if (aes_gcm_decrypt(enc2, enc2_len, read_key, nonce2, seg2) != 0) {
        free(seg1);
        free(seg2);
        return -1;
    }

    if (buffer_reserve(out, seg1_pt_len + seg2_pt_len) != 0) {
        free(seg1);
        free(seg2);
        return -1;
    }
    out->len = 0;
    if (buffer_append(out, seg1, seg1_pt_len) != 0 ||
        buffer_append(out, seg2, seg2_pt_len) != 0) {
        free(seg1);
        free(seg2);
        return -1;
    }

    free(seg1);
    free(seg2);
    return 0;
}
